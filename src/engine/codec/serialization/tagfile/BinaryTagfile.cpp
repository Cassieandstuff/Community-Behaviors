#include <codec/serialization/tagfile/BinaryTagfile.h>

#include <codec/serialization/HavokIo.h>
#include <codec/serialization/packfile/BinaryWriterEx.h>
#include <codec/serialization/packfile/PackFileSerializer.h>
#include <codec/serialization/packfile/PackFileTypes.h>
#include <interface/reflection/HavokSchema.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <deque>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace CB::engine::codec {

namespace {

using io::FieldValue;
using io::SchemaObject;
using schema::ClassSchema;
using schema::Field;
using schema::FieldKind;
using schema::Scalar;
using schema::SchemaRegistry;

constexpr std::uint8_t kMagic[8] = { 0x1e, 0x0d, 0xb0, 0xca, 0xce, 0xfa, 0x11, 0xd0 };

// A member's type: the kind in the low nibble, plus the array / tuple flags.
enum Kind : int { kVoid = 0, kByte, kInt, kReal, kVec4, kVec8, kVec12, kVec16, kObject, kStruct, kString };
constexpr int kKindMask = 0x0f;
constexpr int kArray    = 0x10;
constexpr int kTuple    = 0x20;

// The stream's tags.
enum Tag : int { kFileInfo = 1, kClass, kObjectTag, kObjectRemembered, kBackref, kNull, kEnd };

[[noreturn]] void fail(const std::string& why) { throw std::runtime_error("binary tagfile: " + why); }

// Nesting bound for the object / struct recursion: a malformed file nesting without end throws instead of overflowing
// the stack (the same bound as HavokIo's IoRecurseGuard).
struct DepthGuard {
    explicit DepthGuard(int& d) : m_d(d) { if (++m_d > 256) fail("objects nest too deep"); }
    ~DepthGuard() { --m_d; }
    int& m_d;
};

struct TagMember {
    std::string name;
    int         type  = kVoid;
    int         tuple = 0;      // element count when type & kTuple
    std::string cls;            // the class an object / struct member names
};

struct TagClass {
    std::string            name;
    int                    parent = 0;   // index into the class list ([0] = none)
    std::vector<TagMember> members;      // this class's own, in stream order
};

struct TagObject;

// One stored value; which member is meaningful follows the member's type.
struct TagValue {
    std::int64_t               i = 0;        // byte / int
    std::vector<float>         f;            // real (1 float) / vecN (4N floats; a vec4 array element may store 3)
    std::string                s;            // string
    int                        obj = -1;     // object: an index into Reader::objects (-1 = null), or ...
    bool                       byIndex = false;   // ... (version 3) an index into Reader::remembered, resolved later
    std::shared_ptr<TagObject> st;           // struct, inline
    std::vector<TagValue>      elems;        // array / tuple
};

struct TagObject {
    int                                 cls = 0;
    std::vector<std::optional<TagValue>> vals;   // per flattened member (parent's first); nullopt = not stored
};

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> d) : m_d(d) {}

    int                                     version = -1;
    std::deque<TagClass>                    classes{ TagClass{} };   // a deque: Flat() keeps pointers to members
    std::vector<std::shared_ptr<TagObject>> objects;              // every object, in stream order
    std::vector<int>                        remembered{ -1 };     // remembered index -> objects index ([0] = null)
    int                                     root = -1;            // objects index of the first top-level object

    void Parse() {
        if (!IsBinaryTagfile(m_d)) fail("no tagfile magic");
        m_p = sizeof kMagic;
        for (;;) {
            if (m_p == m_d.size()) {
                if (version == 0) break;   // version 0 has no end tag
                fail("the stream ends before its end tag");
            }
            const std::int64_t tag = Int();
            if (version < 0 && tag != kFileInfo) fail("the stream doesn't start with its file info");
            switch (tag) {
                case kFileInfo:
                    version = static_cast<int>(Int());
                    if (version != 0 && version != 3) fail("version " + std::to_string(version) + " is not supported (0 and 3 are)");
                    break;
                case kClass: ReadClass(); break;
                case kObjectTag:
                case kObjectRemembered: {
                    const int at = ReadObject(static_cast<int>(Int()));
                    if (tag == kObjectRemembered) remembered.push_back(at);
                    if (root < 0) root = at;
                    break;
                }
                case kEnd: return;
                default: fail("unexpected tag " + std::to_string(tag) + " at byte " + std::to_string(m_p));
            }
        }
    }

    // The object a pointer value names, or -1 for null.
    int Target(const TagValue& v) const {
        if (!v.byIndex) return v.obj;
        if (v.obj < 0 || static_cast<std::size_t>(v.obj) >= remembered.size()) fail("object index " + std::to_string(v.obj) + " out of range");
        return remembered[static_cast<std::size_t>(v.obj)];
    }

    // A class's members, its parents' first.
    const std::vector<const TagMember*>& Flat(int ci) {
        if (ci <= 0 || static_cast<std::size_t>(ci) >= classes.size()) fail("class index " + std::to_string(ci) + " out of range");
        if (m_flat.size() < classes.size()) m_flat.resize(classes.size());
        auto& out = m_flat[static_cast<std::size_t>(ci)];
        if (out) return *out;
        std::vector<int> chain;
        for (int c = ci; c > 0; c = classes[static_cast<std::size_t>(c)].parent) {
            if (chain.size() > classes.size() || c >= static_cast<int>(classes.size())) fail("bad parent chain of " + classes[static_cast<std::size_t>(ci)].name);
            chain.push_back(c);
        }
        out.emplace();
        for (auto it = chain.rbegin(); it != chain.rend(); ++it)
            for (const TagMember& m : classes[static_cast<std::size_t>(*it)].members) out->push_back(&m);
        return *out;
    }

private:
    std::uint8_t Byte() {
        if (m_p >= m_d.size()) fail("unexpected end of the stream");
        return m_d[m_p++];
    }
    // Bit 0 of the first byte is the sign, bits 1-6 the low six bits; bit 7 of every byte says another follows.
    std::int64_t Int() {
        std::uint8_t  b   = Byte();
        const bool    neg = b & 1;
        std::uint64_t v   = (b >> 1) & 0x3f;
        for (int shift = 6; b & 0x80; shift += 7) {
            if (shift > 62) fail("an integer is too long at byte " + std::to_string(m_p));
            b = Byte();
            v |= static_cast<std::uint64_t>(b & 0x7f) << shift;
        }
        return neg ? -static_cast<std::int64_t>(v) : static_cast<std::int64_t>(v);
    }
    float Real() {
        if (m_d.size() - m_p < 4) fail("unexpected end of the stream");
        float out;
        std::memcpy(&out, m_d.data() + m_p, 4);   // little-endian, as the host
        m_p += 4;
        return out;
    }
    // A positive length is a new string, remembered; zero or less is -index into the strings seen so far, which
    // start as "" and null (null reads as "", as a packfile's missing string pointer does).
    std::string String() {
        const std::int64_t n = Int();
        if (n > 0) {
            if (static_cast<std::uint64_t>(n) > m_d.size() - m_p) fail("a string runs past the end of the stream");
            std::string s(reinterpret_cast<const char*>(m_d.data() + m_p), static_cast<std::size_t>(n));
            m_p += static_cast<std::size_t>(n);
            m_strings.push_back(s);
            return s;
        }
        if (static_cast<std::size_t>(-n) >= m_strings.size()) fail("string index " + std::to_string(-n) + " out of range");
        return m_strings[static_cast<std::size_t>(-n)];
    }
    std::vector<bool> Bits(std::size_t n) {
        std::vector<bool> out(n);
        std::uint8_t      b = 0;
        for (std::size_t i = 0; i < n; ++i) {
            if (i % 8 == 0) b = Byte();
            out[i] = (b >> (i % 8)) & 1;
        }
        return out;
    }
    int ClassIndex(const std::string& name) const {
        for (std::size_t i = 1; i < classes.size(); ++i)
            if (classes[i].name == name) return static_cast<int>(i);
        fail("struct class '" + name + "' is not described");
    }

    void ReadClass() {
        TagClass c;
        c.name = String();
        (void)Int();   // the class version: members are matched by name, so it isn't needed
        c.parent = static_cast<int>(Int());
        if (c.parent < 0 || static_cast<std::size_t>(c.parent) >= classes.size()) fail("class '" + c.name + "' names an unknown parent");
        const std::int64_t n = Int();
        if (n < 0) fail("class '" + c.name + "' has a negative member count");
        for (std::int64_t k = 0; k < n; ++k) {
            TagMember m;
            m.name = String();
            m.type = static_cast<int>(Int());
            if (m.type & kTuple) m.tuple = static_cast<int>(Int());
            const int kind = m.type & kKindMask;
            if (kind == kObject || kind == kStruct) m.cls = String();
            c.members.push_back(std::move(m));
        }
        classes.push_back(std::move(c));
    }

    // An object's body (presence bits, then each stored value) as a new entry of `objects`; returns its index.
    int ReadObject(int ci) {
        const int at = static_cast<int>(objects.size());
        objects.push_back(std::make_shared<TagObject>());
        auto o = objects.back();
        o->cls = ci;
        Body(*o);
        return at;
    }
    void Body(TagObject& o) {
        DepthGuard  guard(m_depth);
        const auto& members = Flat(o.cls);
        const auto  present = Bits(members.size());
        o.vals.assign(members.size(), std::nullopt);
        for (std::size_t i = 0; i < members.size(); ++i)
            if (present[i]) o.vals[i] = Value(*members[i]);
    }

    TagValue Value(const TagMember& m) {
        const int kind = m.type & kKindMask;
        if ((m.type & kTuple) && (m.type & kArray)) fail("member '" + m.name + "' is both a tuple and an array");
        if (m.type & kTuple) {
            TagValue v;
            for (int k = 0; k < m.tuple; ++k) v.elems.push_back(Single(kind, m.cls));
            return v;
        }
        if (m.type & kArray) return Elements(kind, m.cls, Count());
        return Single(kind, m.cls);
    }
    std::size_t Count() {
        const std::int64_t n = Int();
        if (n < 0 || static_cast<std::uint64_t>(n) > m_d.size()) fail("bad array count " + std::to_string(n));
        return static_cast<std::size_t>(n);
    }

    TagValue Single(int kind, const std::string& cls) {
        TagValue v;
        switch (kind) {
            case kByte:   v.i = Byte(); break;
            case kInt:    v.i = Int(); break;
            case kReal:   v.f.push_back(Real()); break;
            case kVec4: case kVec8: case kVec12: case kVec16:
                for (int k = 0; k < 4 * (kind - kVec4 + 1); ++k) v.f.push_back(Real());
                break;
            case kObject: v = Pointer(); break;
            case kStruct: {
                v.st      = std::make_shared<TagObject>();
                v.st->cls = ClassIndex(cls);
                Body(*v.st);
                break;
            }
            case kString: v.s = String(); break;
            default: fail("member type " + std::to_string(kind) + " is not stored");
        }
        return v;
    }

    // Version 3: an index into the remembered objects. Version 0: the object itself, inline, or a back reference.
    TagValue Pointer() {
        TagValue v;
        if (version >= 3) {
            v.obj     = static_cast<int>(Int());
            v.byIndex = true;
            return v;
        }
        switch (const std::int64_t tag = Int()) {
            case kObjectTag:
            case kObjectRemembered: {
                v.obj = ReadObject(static_cast<int>(Int()));
                if (tag == kObjectRemembered) remembered.push_back(v.obj);
                break;
            }
            case kBackref: {   // 0 = the first object remembered
                const std::int64_t k = Int();
                if (k < 0 || static_cast<std::size_t>(k) + 1 >= remembered.size()) fail("back reference " + std::to_string(k) + " out of range");
                v.obj = remembered[static_cast<std::size_t>(k) + 1];
                break;
            }
            case kNull: break;
            default: fail("unexpected pointer tag " + std::to_string(tag) + " at byte " + std::to_string(m_p));
        }
        return v;
    }

    // `n` array elements. Ints carry their width first (version 3); a vec4 array its stored component count (3 or 4);
    // a struct array is stored by column: presence bits for the struct's members, then each stored member's values for
    // every element.
    TagValue Elements(int kind, const std::string& cls, std::size_t n) {
        TagValue v;
        v.elems.resize(n);
        switch (kind) {
            case kByte:  for (auto& e : v.elems) e.i = Byte(); break;
            case kInt:
                if (version >= 3) (void)Int();   // the element width (4 or 8); the schema field decides the written width
                for (auto& e : v.elems) e.i = Int();
                break;
            case kReal:  for (auto& e : v.elems) e.f.push_back(Real()); break;
            case kVec4: {
                const std::int64_t comps = Int();
                if (comps != 3 && comps != 4) fail("a vec4 array stores " + std::to_string(comps) + " components");
                for (auto& e : v.elems)
                    for (std::int64_t k = 0; k < comps; ++k) e.f.push_back(Real());
                break;
            }
            case kVec8: case kVec12: case kVec16:
                for (auto& e : v.elems)
                    for (int k = 0; k < 4 * (kind - kVec4 + 1); ++k) e.f.push_back(Real());
                break;
            case kObject: for (auto& e : v.elems) e = Pointer(); break;
            case kString: for (auto& e : v.elems) e.s = String(); break;
            case kStruct: {
                const int   ci      = ClassIndex(cls);
                const auto& members = Flat(ci);
                const auto  present = Bits(members.size());
                for (auto& e : v.elems) {
                    e.st      = std::make_shared<TagObject>();
                    e.st->cls = ci;
                    e.st->vals.assign(members.size(), std::nullopt);
                }
                for (std::size_t i = 0; i < members.size(); ++i) {
                    if (!present[i]) continue;
                    const TagMember& m    = *members[i];
                    const int        mk   = m.type & kKindMask;
                    for (std::size_t k = 0; k < n; ++k) {
                        // tuples and arrays are stored per element; any other member as one column
                        if (m.type & (kTuple | kArray)) v.elems[k].st->vals[i] = Value(m);
                    }
                    if (m.type & (kTuple | kArray)) continue;
                    TagValue column = Elements(mk, m.cls, n);
                    for (std::size_t k = 0; k < n; ++k) v.elems[k].st->vals[i] = std::move(column.elems[k]);
                }
                break;
            }
            default: fail("array of member type " + std::to_string(kind) + " is not stored");
        }
        return v;
    }

    std::span<const std::uint8_t>                          m_d;
    std::size_t                                            m_p = 0;
    int                                                    m_depth = 0;
    std::vector<std::string>                               m_strings{ std::string(), std::string() };
    std::vector<std::optional<std::vector<const TagMember*>>> m_flat;
};

// A member name as the schema may spell it: lower case, no underscores (`m_target_bRca` -> `targetBRca`).
std::string looseName(std::string_view s) {
    std::string out;
    for (const char c : s)
        if (c != '_') out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

int repeat(const Field& f) { return f.count > 0 ? f.count : 1; }

// Builds the SchemaObject graph from the parsed stream: each tagfile object once (shared references stay shared), its
// stored members written onto the schema fields of the same name.
class Builder {
public:
    Builder(Reader& r, const SchemaRegistry& reg, bool tolerate)
        : m_r(r), m_reg(reg), m_tolerate(tolerate), m_built(r.objects.size()) {}

    std::shared_ptr<IHavokObject> Object(int at) {
        if (at < 0) return nullptr;
        auto& slot = m_built[static_cast<std::size_t>(at)];
        if (slot.done) return slot.obj;
        slot.done = true;   // set before the fill, so a reference back to this object finds it
        const std::string& name = m_r.classes[static_cast<std::size_t>(m_r.objects[static_cast<std::size_t>(at)]->cls)].name;
        const ClassSchema* cs   = m_reg.Find(name);
        if (!cs) {
            if (m_tolerate) return nullptr;
            throw std::runtime_error("Havok class not registered: " + name);
        }
        auto so  = std::make_shared<SchemaObject>(&m_reg, cs);
        slot.obj = so;
        so->Init();
        Fill(*so, *m_r.objects[static_cast<std::size_t>(at)]);
        return so;
    }

private:
    struct Slot { bool done = false; std::shared_ptr<IHavokObject> obj; };

    void Fill(SchemaObject& so, const TagObject& o) {
        DepthGuard  guard(m_depth);
        const auto& members = m_r.Flat(o.cls);
        const auto& fields  = so.Fields();
        std::vector<bool> used(fields.size());
        for (std::size_t i = 0; i < members.size(); ++i) {
            if (!o.vals[i]) continue;
            const std::string& mn = members[i]->name;
            std::optional<std::size_t> at = so.IndexOf(mn);
            if (at && used[*at]) at.reset();
            if (!at) {
                const std::string loose = looseName(mn);
                for (std::size_t k = 0; k < fields.size() && !at; ++k)
                    if (!used[k] && fields[k] && !fields[k]->name.empty() && looseName(fields[k]->name) == loose) at = k;
            }
            if (!at) throw std::runtime_error("binary tagfile: " + std::string(so.ClassName()) + "." + mn +
                                              " is stored, but the schema has no such field");
            used[*at] = true;
            Set(so, *fields[*at], so.FieldAt(*at), *members[i], *o.vals[i]);
        }
    }

    [[noreturn]] void mismatch(const SchemaObject& so, const Field& f, const TagMember& m) const {
        throw std::runtime_error("binary tagfile: " + std::string(so.ClassName()) + "." + f.name + ": stored as type " +
                                 std::to_string(m.type) + ", which doesn't fit the schema field");
    }

    // A tuple's elements, or the single value as a list of one.
    static std::vector<const TagValue*> items(const TagMember& m, const TagValue& v) {
        std::vector<const TagValue*> out;
        if (m.type & kTuple) for (const auto& e : v.elems) out.push_back(&e);
        else                 out.push_back(&v);
        return out;
    }

    // One number at the schema scalar's width; `real` says the tagfile stored a float.
    static void putScalar(std::vector<std::uint8_t>& raw, std::size_t at, Scalar s, bool real, const TagValue& v) {
        const std::size_t w = static_cast<std::size_t>(schema::ScalarWidth(s));
        if (raw.size() < at + w) raw.resize(at + w, 0);
        if (s == Scalar::Float || s == Scalar::Half) {
            const float   x = real ? (v.f.empty() ? 0.0f : v.f[0]) : static_cast<float>(v.i);
            std::uint32_t bits;
            std::memcpy(&bits, &x, 4);
            if (s == Scalar::Half) { const std::uint16_t h = static_cast<std::uint16_t>(bits >> 16); std::memcpy(raw.data() + at, &h, 2); }   // as SchemaObject::SetFloat
            else                   std::memcpy(raw.data() + at, &bits, 4);
            return;
        }
        const std::int64_t i = real ? static_cast<std::int64_t>(v.f.empty() ? 0.0f : v.f[0]) : v.i;
        if (s == Scalar::Bool) { raw[at] = i != 0; return; }
        std::memcpy(raw.data() + at, &i, w);   // the low bytes (little-endian); a flags value keeps its bits
    }

    // Floats as raw bytes, `per` floats an element (a 3-component vec4 array element gets w = 0).
    static void putFloats(std::vector<std::uint8_t>& raw, const std::vector<float>& f, std::size_t per) {
        const std::size_t at = raw.size();
        raw.resize(at + per * 4, 0);
        std::memcpy(raw.data() + at, f.data(), std::min(f.size(), per) * 4);
    }

    void Set(SchemaObject& so, const Field& f, FieldValue& fv, const TagMember& m, const TagValue& v) {
        const int  kind  = m.type & kKindMask;
        const bool array = m.type & kArray;
        switch (f.kind) {
            case FieldKind::Scalar: {
                if (array || (kind != kByte && kind != kInt && kind != kReal)) mismatch(so, f, m);
                const auto xs = items(m, v);
                if (static_cast<int>(xs.size()) != repeat(f)) mismatch(so, f, m);
                const std::size_t w = static_cast<std::size_t>(schema::ScalarWidth(f.scalar));
                for (std::size_t k = 0; k < xs.size(); ++k) putScalar(fv.raw, k * w, f.scalar, kind == kReal, *xs[k]);
                break;
            }
            case FieldKind::BoolArray: {
                if (array || (kind != kByte && kind != kInt)) mismatch(so, f, m);
                const auto xs = items(m, v);
                if (static_cast<int>(xs.size()) != f.count) mismatch(so, f, m);
                for (std::size_t k = 0; k < xs.size(); ++k) fv.raw[k] = xs[k]->i != 0;
                break;
            }
            case FieldKind::Vector4:
            case FieldKind::Quaternion:
            case FieldKind::QsTransform: {
                if (array || kind < kVec4 || kind > kVec16) mismatch(so, f, m);
                std::vector<float> all;
                for (const TagValue* x : items(m, v)) all.insert(all.end(), x->f.begin(), x->f.end());
                if (all.size() * 4 != fv.raw.size()) mismatch(so, f, m);
                std::memcpy(fv.raw.data(), all.data(), fv.raw.size());
                break;
            }
            case FieldKind::String:
            case FieldKind::CString:
                if (array || kind != kString) mismatch(so, f, m);
                fv.str = v.s;
                break;
            case FieldKind::StringArray:
                if (!array || kind != kString) mismatch(so, f, m);
                fv.strs.clear();
                for (const auto& e : v.elems) fv.strs.push_back(e.s);
                break;
            case FieldKind::Ptr:
                if (array || kind != kObject) mismatch(so, f, m);
                if (repeat(f) == 1 && !(m.type & kTuple)) { fv.obj = Object(m_r.Target(v)); break; }
                if (static_cast<int>(v.elems.size()) != repeat(f)) mismatch(so, f, m);
                fv.objs.clear();
                for (const auto& e : v.elems) fv.objs.push_back(Object(m_r.Target(e)));
                break;
            case FieldKind::PtrArray:
                if (!array || kind != kObject) mismatch(so, f, m);
                fv.objs.clear();
                for (const auto& e : v.elems) fv.objs.push_back(Object(m_r.Target(e)));
                break;
            case FieldKind::Vec4Array:
            case FieldKind::QsTransformArray: {
                const bool q = f.kind == FieldKind::QsTransformArray;
                if (!array || kind != (q ? kVec12 : kVec4)) mismatch(so, f, m);
                fv.raw.clear();
                for (const auto& e : v.elems) putFloats(fv.raw, e.f, q ? 12 : 4);
                break;
            }
            case FieldKind::ScalarArray: {
                if (!array || (kind != kByte && kind != kInt && kind != kReal)) mismatch(so, f, m);
                const std::size_t w = static_cast<std::size_t>(schema::ScalarWidth(f.scalar));
                fv.raw.assign(v.elems.size() * w, 0);
                for (std::size_t k = 0; k < v.elems.size(); ++k) putScalar(fv.raw, k * w, f.scalar, kind == kReal, v.elems[k]);
                break;
            }
            case FieldKind::Struct:
                if (array || (kind != kStruct && kind != kObject)) mismatch(so, f, m);
                fv.obj = Struct(f, m, v);
                break;
            case FieldKind::StructArray:
                if (!array || (kind != kStruct && kind != kObject)) mismatch(so, f, m);
                fv.objs.clear();
                for (const auto& e : v.elems) fv.objs.push_back(Struct(f, m, e));
                break;
            case FieldKind::Vtable:
            case FieldKind::EmptyArray:
            case FieldKind::EmptyPtr:
            case FieldKind::Pad:
            case FieldKind::Skip:
            case FieldKind::Text:
                mismatch(so, f, m);   // the schema keeps no value here, but the file stores one
        }
    }

    // An inline struct. An older class version may store it by pointer (hkaAnimation::annotationTracks was an array of
    // hkaAnnotationTrack pointers); the object it points to, of the struct's own class, is taken by value.
    std::shared_ptr<IHavokObject> Struct(const Field& f, const TagMember& m, const TagValue& v) {
        const ClassSchema* es = m_reg.Find(f.ref);
        if (!es) throw std::runtime_error("binary tagfile: unresolved struct ref '" + f.ref + "'");
        const TagObject* o = v.st.get();
        if ((m.type & kKindMask) == kObject) {
            const int at = m_r.Target(v);
            if (at < 0) throw std::runtime_error("binary tagfile: " + f.name + ": a null pointer where the schema keeps the struct by value");
            o = m_r.objects[static_cast<std::size_t>(at)].get();
            const std::string& cls = m_r.classes[static_cast<std::size_t>(o->cls)].name;
            if (cls != f.ref) throw std::runtime_error("binary tagfile: " + f.name + ": points to a " + cls + ", where the schema keeps a " + f.ref + " by value");
        }
        if (!o) throw std::runtime_error("binary tagfile: " + f.name + ": no struct value");
        auto e = std::make_shared<SchemaObject>(&m_reg, es);
        e->Init();
        Fill(*e, *o);
        return e;
    }

    Reader&               m_r;
    const SchemaRegistry& m_reg;
    bool                  m_tolerate;
    std::vector<Slot>     m_built;
    int                   m_depth = 0;
};

} // namespace

bool IsBinaryTagfile(std::span<const std::uint8_t> bytes) noexcept {
    return bytes.size() >= sizeof kMagic && std::memcmp(bytes.data(), kMagic, sizeof kMagic) == 0;
}

std::shared_ptr<IHavokObject> ReadBinaryTagfile(std::span<const std::uint8_t> bytes, const SchemaRegistry& reg,
                                                bool tolerateUnregistered) {
    Reader r(bytes);
    r.Parse();
    if (r.root < 0) fail("no objects");
    return Builder(r, reg, tolerateUnregistered).Object(r.root);
}

std::vector<std::uint8_t> BinaryTagfileToPackfile(std::span<const std::uint8_t> bytes, const SchemaRegistry* reg,
                                                  bool tolerateUnregistered) {
    if (!reg) reg = schema::SharedRegistry();
    if (!reg) fail("no schema registry (" + schema::SharedRegistryError() + ")");
    const auto root = ReadBinaryTagfile(bytes, *reg, tolerateUnregistered);
    if (!root) fail("the root object's class is not registered");
    const HKXHeader    header = HKXHeader::SkyrimSE();
    BinaryWriterEx     bw(header.Endian == 0, header.PointerSize == 8);
    PackFileSerializer ser;
    ser.Serialize(root, bw, header);
    return bw.Data();
}

} // namespace CB::engine::codec
