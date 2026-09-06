#include <havok-io/HavokIo.h>

#include <stdexcept>
#include <unordered_map>

namespace havok::io {

using schema::ClassSchema;
using schema::Field;
using schema::FieldKind;
using schema::SchemaRegistry;

namespace {

std::size_t alignUp(std::size_t p, int a) {
    return a <= 0 ? p : ((p + static_cast<std::size_t>(a) - 1) / static_cast<std::size_t>(a)) * static_cast<std::size_t>(a);
}

// Recursion guard for Init()/Read()'s inline-Struct recursion: a malformed schema whose struct
// (transitively) references itself would recurse unboundedly -> stack overflow. Schemas are
// community-contributable, so a cyclic descriptor is reachable input; convert it to a diagnosable throw.
thread_local int g_ioRecurse = 0;
struct IoRecurseGuard {
    IoRecurseGuard()  { if (++g_ioRecurse > 256) throw std::runtime_error("havok-io: schema recursion too deep (cyclic struct/parent ref?)"); }
    ~IoRecurseGuard() { --g_ioRecurse; }
};

// Flatten a class's serialized field list: parent's fields first (recursively), then this class's own.
// `depth` bounds the parent chain so a cyclic `parent:` (A->B->A) throws instead of overflowing.
void flatten(const SchemaRegistry& reg, const ClassSchema* cs, std::vector<const Field*>& out, int depth = 0) {
    if (!cs) return;
    if (depth > 256) throw std::runtime_error("havok-io: schema parent chain too deep (cycle?) at '" + cs->name + "'");
    if (!cs->parent.empty()) {
        const ClassSchema* p = reg.Find(cs->parent);
        if (!p) throw std::runtime_error("havok-io: unresolved parent '" + cs->parent + "' of '" + cs->name + "'");
        flatten(reg, p, out, depth + 1);
    }
    for (const Field& f : cs->fields) out.push_back(&f);
}

int repeat(const Field& f) { return f.count > 0 ? f.count : 1; }

} // namespace

std::vector<const Field*> SchemaObject::Fields() const {
    std::vector<const Field*> out;
    flatten(*m_reg, m_schema, out);
    return out;
}

// ── Construction: build a COMPLETE default object — correctly-sized zero bytes for every fixed
// field, position-computed Pad, default-initialized inline Structs, empty arrays/pointers. A caller
// then overwrites specific fields via FieldRef(); untouched fields serialize as valid defaults. The
// cursor walk mirrors SchemaRegistry::ComputeSize so Pad lands at the same offset as Read/Write. ──
void SchemaObject::Init() {
    IoRecurseGuard _rg;   // bound the inline-Struct recursion below (cyclic struct ref -> throw, not overflow)
    std::vector<const Field*> fields;
    flatten(*m_reg, m_schema, fields);
    m_fields.assign(fields.size(), FieldValue{});
    std::size_t cur = 0;   // struct-relative offset (objects start 16-aligned, so this matches Read's pad)
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const Field& f = *fields[i];
        FieldValue&  v = m_fields[i];
        const int    n = repeat(f);
        auto raw = [&](std::size_t w) { v.raw.assign(w, std::uint8_t{0}); cur += w; };
        switch (f.kind) {
            case FieldKind::Vtable:      cur += 8; break;                        // vtable = 0 (default), no raw
            case FieldKind::Scalar:      raw(static_cast<std::size_t>(schema::ScalarWidth(f.scalar)) * n); break;
            case FieldKind::Vector4:     raw(16 * static_cast<std::size_t>(n)); break;
            case FieldKind::Quaternion:  raw(16 * static_cast<std::size_t>(n)); break;
            case FieldKind::QsTransform: raw(48 * static_cast<std::size_t>(n)); break;
            case FieldKind::BoolArray:   raw(static_cast<std::size_t>(f.count)); break;
            case FieldKind::Skip:        raw(static_cast<std::size_t>(f.count)); break;
            case FieldKind::Pad:         raw(alignUp(cur, f.count) - cur); break;
            case FieldKind::String:
            case FieldKind::CString:     cur += 8; break;                        // pointer to string, default ""
            case FieldKind::Ptr:         cur += 8 * static_cast<std::size_t>(n); break;   // null pointer(s)
            case FieldKind::EmptyPtr:    cur += 8; break;
            case FieldKind::StringArray:
            case FieldKind::PtrArray:
            case FieldKind::StructArray:
            case FieldKind::Vec4Array:
            case FieldKind::QsTransformArray:
            case FieldKind::ScalarArray:
            case FieldKind::EmptyArray:  cur += 16; break;                       // hkArray header, default empty
            case FieldKind::Struct: {                                            // inline struct: default-init it
                const ClassSchema* es = m_reg->Find(f.ref);
                if (es) { auto e = std::make_shared<SchemaObject>(m_reg, es); e->Init(); v.obj = e; }
                const int inner = m_reg->ComputeSize(f.ref);
                cur += (inner > 0 ? static_cast<std::size_t>(inner) : 0);
                break;
            }
        }
    }
}

bool SchemaObject::HasField(const std::string& name) const {
    std::vector<const Field*> fields;
    flatten(*m_reg, m_schema, fields);
    for (const Field* f : fields) if (f->name == name) return true;
    return false;
}

FieldValue& SchemaObject::FieldRef(const std::string& name) {
    std::vector<const Field*> fields;
    flatten(*m_reg, m_schema, fields);
    if (m_fields.size() != fields.size()) m_fields.assign(fields.size(), FieldValue{});  // lazy Init
    for (std::size_t i = 0; i < fields.size(); ++i)
        if (fields[i]->name == name) return m_fields[i];
    throw std::runtime_error("havok-io: SchemaObject '" + m_schema->name + "' has no field '" + name + "'");
}

void SchemaObject::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    IoRecurseGuard _rg;   // bound the inline-Struct recursion below (cyclic struct ref -> throw, not overflow)
    std::vector<const Field*> fields;
    flatten(*m_reg, m_schema, fields);
    m_fields.clear();
    m_fields.resize(fields.size());

    for (std::size_t i = 0; i < fields.size(); ++i) {
        const Field& f = *fields[i];
        FieldValue&  v = m_fields[i];
        const int    n = repeat(f);

        switch (f.kind) {
            case FieldKind::Vtable:      v.vtable = br.ReadUSize(); break;
            case FieldKind::Scalar:      v.raw = br.ReadBytes(static_cast<std::size_t>(schema::ScalarWidth(f.scalar)) * n); break;
            case FieldKind::Vector4:     v.raw = br.ReadBytes(static_cast<std::size_t>(16) * n); break;
            case FieldKind::Quaternion:  v.raw = br.ReadBytes(static_cast<std::size_t>(16) * n); break;
            case FieldKind::QsTransform: v.raw = br.ReadBytes(static_cast<std::size_t>(48) * n); break;
            case FieldKind::BoolArray:   v.raw = br.ReadBytes(static_cast<std::size_t>(f.count)); break;
            case FieldKind::Skip:        v.raw = br.ReadBytes(static_cast<std::size_t>(f.count)); break;
            case FieldKind::Pad:         v.raw = br.ReadBytes(alignUp(br.Position(), f.count) - br.Position()); break;

            case FieldKind::String:      v.str = des.ReadStringPointer(br); break;
            case FieldKind::CString:     v.str = des.ReadCString(br); break;
            case FieldKind::StringArray: v.strs = des.ReadStringPointerArray(br); break;

            case FieldKind::Ptr:
                if (n == 1) {
                    v.obj = des.ReadClassPointer<IHavokObject>(br);
                } else {   // fixed inline pointer array (std::array<T*, N>): N consecutive pointer slots
                    v.objs.resize(n);
                    for (int k = 0; k < n; ++k) v.objs[k] = des.ReadClassPointer<IHavokObject>(br);
                }
                break;
            case FieldKind::PtrArray:    v.objs = des.ReadClassPointerArray<IHavokObject>(br); break;

            case FieldKind::Vec4Array:        v.raw = des.ReadRawArray(br, 16); break;
            case FieldKind::QsTransformArray: v.raw = des.ReadRawArray(br, 48); break;
            case FieldKind::ScalarArray:      v.raw = des.ReadRawArray(br, schema::ScalarWidth(f.scalar)); break;

            case FieldKind::EmptyArray:  des.ReadEmptyArray(br); break;
            case FieldKind::EmptyPtr:    des.ReadEmptyPointer(br); break;

            case FieldKind::Struct: {
                const ClassSchema* es = m_reg->Find(f.ref);
                if (!es) throw std::runtime_error("havok-io: unresolved struct ref '" + f.ref + "'");
                auto e = std::make_shared<SchemaObject>(m_reg, es);
                e->Read(des, br);
                v.obj = e;
                break;
            }
            case FieldKind::StructArray: {
                const ClassSchema* es = m_reg->Find(f.ref);
                if (!es) throw std::runtime_error("havok-io: unresolved structarray ref '" + f.ref + "'");
                const SchemaRegistry* reg = m_reg;
                v.objs = des.ReadStructArrayGeneric(br, [reg, es]() -> std::shared_ptr<IHavokObject> {
                    return std::make_shared<SchemaObject>(reg, es);
                });
                break;
            }
        }
    }
}

void SchemaObject::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    std::vector<const Field*> fields;
    flatten(*m_reg, m_schema, fields);
    // m_fields was filled by Read in the same flattened order.

    for (std::size_t i = 0; i < fields.size(); ++i) {
        const Field&      f = *fields[i];
        const FieldValue& v = m_fields[i];
        const int         n = repeat(f);

        switch (f.kind) {
            case FieldKind::Vtable:      bw.WriteUSize(v.vtable); break;
            case FieldKind::Scalar:
            case FieldKind::Vector4:
            case FieldKind::Quaternion:
            case FieldKind::QsTransform:
            case FieldKind::BoolArray:
            case FieldKind::Skip:
            case FieldKind::Pad:         bw.WriteBytes(v.raw); break;

            case FieldKind::String:      s.WriteStringPointer(bw, v.str); break;
            case FieldKind::CString:     s.WriteCString(bw, v.str); break;
            case FieldKind::StringArray: s.WriteStringPointerArray(bw, v.strs); break;

            case FieldKind::Ptr:
                if (n == 1) s.WriteClassPointer(bw, v.obj);
                else for (int k = 0; k < n; ++k) s.WriteClassPointer(bw, k < static_cast<int>(v.objs.size()) ? v.objs[k] : nullptr);
                break;
            case FieldKind::PtrArray:    s.WriteClassPointerArrayGeneric(bw, v.objs); break;

            case FieldKind::Vec4Array:        s.WriteRawArray(bw, v.raw, 16); break;
            case FieldKind::QsTransformArray: s.WriteRawArray(bw, v.raw, 48); break;
            case FieldKind::ScalarArray:      s.WriteRawArray(bw, v.raw, schema::ScalarWidth(f.scalar)); break;

            case FieldKind::EmptyArray:  s.WriteVoidArray(bw); break;
            case FieldKind::EmptyPtr:    s.WriteVoidPointer(bw); break;

            case FieldKind::Struct:      if (v.obj) v.obj->Write(s, bw); break;
            case FieldKind::StructArray: s.WriteStructArrayGeneric(bw, v.objs); break;
        }
    }
}

std::function<std::shared_ptr<IHavokObject>(const std::string&)>
MakeSchemaFactory(const SchemaRegistry& reg) {
    const SchemaRegistry* r = &reg;
    return [r](const std::string& name) -> std::shared_ptr<IHavokObject> {
        const ClassSchema* cs = r->Find(name);
        if (!cs) return nullptr;   // deserializer treats null as "unregistered"
        return std::make_shared<SchemaObject>(r, cs);
    };
}

bool RoundtripHkx(const std::vector<std::uint8_t>& in,
                  const SchemaRegistry&            reg,
                  std::vector<std::uint8_t>&       out,
                  std::string&                     err) {
    try {
        BinaryReaderEx br(in);
        PackFileDeserializer des;
        des.ObjectFactory = MakeSchemaFactory(reg);
        std::shared_ptr<IHavokObject> root = des.Deserialize(br);
        if (!root) { err = "havok-io: null root object"; return false; }

        PackFileSerializer ser;
        BinaryWriterEx bw(des._header.Endian == 0, des._header.PointerSize == 8);
        ser.Serialize(root, bw, des._header);
        out = bw.Data();
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

namespace {
// Deep-copy a SchemaObject graph using ONLY the construction API (Init + FieldRef/FieldAt) — no Read.
// Proves the from-scratch build path is byte-faithful: a rebuilt graph must re-serialize identically.
// memo handles sharing + cycles (an object referenced N times is rebuilt once).
std::shared_ptr<IHavokObject> rebuildObj(
    const std::shared_ptr<IHavokObject>& src, const SchemaRegistry& reg,
    std::unordered_map<const IHavokObject*, std::shared_ptr<IHavokObject>>& memo) {
    if (!src) return nullptr;
    if (auto it = memo.find(src.get()); it != memo.end()) return it->second;
    const auto* so = dynamic_cast<const SchemaObject*>(src.get());
    if (!so) return src;   // non-schema object (shouldn't occur on the generic path) — share as-is
    auto fresh = std::make_shared<SchemaObject>(&reg, so->Schema());
    fresh->Init();
    memo[src.get()] = fresh;
    const auto& vals = so->Values();
    for (std::size_t i = 0; i < vals.size(); ++i) {
        FieldValue&       d = fresh->FieldAt(i);
        const FieldValue& s = vals[i];
        d.raw = s.raw; d.vtable = s.vtable; d.str = s.str; d.strs = s.strs;
        if (s.obj) d.obj = rebuildObj(s.obj, reg, memo);
        d.objs.reserve(s.objs.size());
        for (const auto& c : s.objs) d.objs.push_back(rebuildObj(c, reg, memo));
    }
    return fresh;
}
} // namespace

bool RebuildHkx(const std::vector<std::uint8_t>& in,
                const SchemaRegistry&            reg,
                std::vector<std::uint8_t>&       out,
                std::string&                     err) {
    try {
        BinaryReaderEx br(in);
        PackFileDeserializer des;
        des.ObjectFactory = MakeSchemaFactory(reg);
        std::shared_ptr<IHavokObject> root = des.Deserialize(br);
        if (!root) { err = "havok-io: null root object"; return false; }

        std::unordered_map<const IHavokObject*, std::shared_ptr<IHavokObject>> memo;
        std::shared_ptr<IHavokObject> rebuilt = rebuildObj(root, reg, memo);

        PackFileSerializer ser;
        BinaryWriterEx bw(des._header.Endian == 0, des._header.PointerSize == 8);
        ser.Serialize(rebuilt, bw, des._header);
        out = bw.Data();
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

} // namespace havok::io
