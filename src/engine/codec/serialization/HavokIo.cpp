#include <codec/serialization/HavokIo.h>

#include <cstring>
#include <stdexcept>

namespace CB::engine::codec::io {

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

int repeat(const Field& f) { return f.count > 0 ? f.count : 1; }

bool isIntScalar(schema::Scalar s) {
    using schema::Scalar;
    return s != Scalar::Float && s != Scalar::Half && s != Scalar::Bool;
}
bool isSignedScalar(schema::Scalar s) {
    using schema::Scalar;
    return s == Scalar::Int8 || s == Scalar::Int16 || s == Scalar::Int32 || s == Scalar::Int64;
}

[[noreturn]] void typeError(const ClassSchema* cs, std::string_view name, const std::string& why) {
    throw std::runtime_error("havok-io: " + cs->name + "." + std::string(name) + ": " + why);
}

/// @brief The byte offset of element `k` of a fixed-repeat scalar, bounds-checked against the stored bytes.
std::size_t scalarSlot(const ClassSchema* cs, const Field& f, const FieldValue& v, std::string_view name, int k) {
    const std::size_t w = static_cast<std::size_t>(schema::ScalarWidth(f.scalar));
    if (k < 0 || k >= repeat(f)) typeError(cs, name, "element " + std::to_string(k) + " out of range");
    const std::size_t at = w * static_cast<std::size_t>(k);
    if (v.raw.size() < at + w) typeError(cs, name, "value not populated");
    return at;
}

} // namespace

SchemaObject::SchemaObject(const schema::SchemaRegistry* reg, const schema::ClassSchema* cs)
    : m_reg(reg), m_schema(cs) {
    m_layout = reg ? reg->Layout(cs) : nullptr;
    if (!m_layout && reg && cs) {
        /// @note A class whose parent chain doesn't resolve stays layout-less here, so the use site throws the
        ///       real reason (Layout() re-runs Flatten) — the same error, at the same point, as before caching.
        try {
            auto own = std::make_shared<schema::FieldLayout>();
            own->fields = reg->Flatten(cs);
            for (std::size_t i = 0; i < own->fields.size(); ++i) own->index.emplace(own->fields[i]->name, i);
            m_ownLayout = std::move(own);
            m_layout    = m_ownLayout.get();
        } catch (const std::exception&) {}
    }
}

const schema::FieldLayout& SchemaObject::Layout() const {
    if (!m_layout) (void)m_reg->Flatten(m_schema);   // throws the unresolved/cyclic parent reason
    if (!m_layout) throw std::runtime_error("havok-io: no field layout for '" + m_schema->name + "'");
    return *m_layout;
}

std::optional<std::size_t> SchemaObject::IndexOf(std::string_view name) const {
    const auto& idx = Layout().index;
    const auto  it  = idx.find(name);
    if (it == idx.end()) return std::nullopt;
    return it->second;
}

const FieldValue* SchemaObject::FindField(std::string_view name) const {
    const auto i = IndexOf(name);
    if (!i || m_fields.size() != Layout().fields.size()) return nullptr;
    return &m_fields[*i];
}

std::pair<const Field*, const FieldValue*> SchemaObject::typed(std::string_view name, const char* what) const {
    const auto i = IndexOf(name);
    if (!i) typeError(m_schema, name, "no such field");
    const Field* f = Layout().fields[*i];
    const std::string_view w(what);
    const bool ok =
        (w == "int"         && f->kind == FieldKind::Scalar && isIntScalar(f->scalar)) ||
        (w == "float"       && f->kind == FieldKind::Scalar && (f->scalar == schema::Scalar::Float || f->scalar == schema::Scalar::Half)) ||
        (w == "floatbits"   && f->kind == FieldKind::Scalar && f->scalar == schema::Scalar::Float) ||
        (w == "bool"        && f->kind == FieldKind::Scalar && f->scalar == schema::Scalar::Bool) ||
        (w == "ScalarArray" && f->kind == FieldKind::ScalarArray) ||
        (w == "string"      && (f->kind == FieldKind::String || f->kind == FieldKind::CString)) ||
        (w == "child"       && (f->kind == FieldKind::Ptr || f->kind == FieldKind::Struct)) ||
        (w == "children"    && (f->kind == FieldKind::PtrArray || f->kind == FieldKind::StructArray ||
                                (f->kind == FieldKind::Ptr && repeat(*f) > 1)));
    if (!ok) typeError(m_schema, name, std::string("not a ") + what + " field");
    const FieldValue* v = m_fields.size() == Layout().fields.size() ? &m_fields[*i] : nullptr;
    return { f, v };
}

void SchemaObject::checkArrayWidth(const Field& f, std::size_t elem, std::string_view name) const {
    if (elem != static_cast<std::size_t>(schema::ScalarWidth(f.scalar)))
        typeError(m_schema, name, "element size " + std::to_string(elem) + " != schema width " +
                                  std::to_string(schema::ScalarWidth(f.scalar)));
}

std::int64_t SchemaObject::GetInt(std::string_view name, int k) const {
    const auto [f, v] = typed(name, "int");
    if (!v) typeError(m_schema, name, "value not populated");
    const std::size_t at = scalarSlot(m_schema, *f, *v, name, k);
    const int         w  = schema::ScalarWidth(f->scalar);
    std::uint64_t u = 0;
    std::memcpy(&u, v->raw.data() + at, static_cast<std::size_t>(w));
    if (isSignedScalar(f->scalar) && w < 8) {
        const std::uint64_t sign = std::uint64_t{ 1 } << (w * 8 - 1);
        u = (u ^ sign) - sign;   // sign-extend
    }
    return static_cast<std::int64_t>(u);
}

void SchemaObject::SetInt(std::string_view name, std::int64_t v, int k) {
    const auto [f, cur] = typed(name, "int");
    (void)cur;
    const int w = schema::ScalarWidth(f->scalar);
    if (w < 8) {
        const bool fits = isSignedScalar(f->scalar)
            ? (v >= -(std::int64_t{ 1 } << (w * 8 - 1)) && v < (std::int64_t{ 1 } << (w * 8 - 1)))
            : (v >= 0 && v < (std::int64_t{ 1 } << (w * 8)));
        if (!fits) typeError(m_schema, name, std::to_string(v) + " doesn't fit a " + std::to_string(w * 8) + "-bit field");
    } else if (!isSignedScalar(f->scalar) && v < 0) {
        typeError(m_schema, name, std::to_string(v) + " doesn't fit an unsigned 64-bit field");
    }
    FieldValue& fv = FieldRef(name);
    if (fv.raw.size() < static_cast<std::size_t>(w) * repeat(*f)) fv.raw.resize(static_cast<std::size_t>(w) * repeat(*f), 0);
    const std::size_t at = scalarSlot(m_schema, *f, fv, name, k);
    std::memcpy(fv.raw.data() + at, &v, static_cast<std::size_t>(w));   // little-endian low bytes
}

float SchemaObject::GetFloat(std::string_view name, int k) const {
    const auto [f, v] = typed(name, "float");
    if (!v) typeError(m_schema, name, "value not populated");
    const std::size_t at = scalarSlot(m_schema, *f, *v, name, k);
    std::uint32_t bits = 0;
    if (f->scalar == schema::Scalar::Half) {
        std::uint16_t h = 0;
        std::memcpy(&h, v->raw.data() + at, 2);
        bits = static_cast<std::uint32_t>(h) << 16;
    } else {
        std::memcpy(&bits, v->raw.data() + at, 4);
    }
    float out;
    std::memcpy(&out, &bits, 4);
    return out;
}

void SchemaObject::SetFloat(std::string_view name, float value, int k) {
    const auto [f, cur] = typed(name, "float");
    (void)cur;
    const std::size_t w = static_cast<std::size_t>(schema::ScalarWidth(f->scalar));
    FieldValue& fv = FieldRef(name);
    if (fv.raw.size() < w * repeat(*f)) fv.raw.resize(w * repeat(*f), 0);
    const std::size_t at = scalarSlot(m_schema, *f, fv, name, k);
    std::uint32_t bits;
    std::memcpy(&bits, &value, 4);
    if (f->scalar == schema::Scalar::Half) {
        const std::uint16_t h = static_cast<std::uint16_t>(bits >> 16);
        std::memcpy(fv.raw.data() + at, &h, 2);
    } else {
        std::memcpy(fv.raw.data() + at, &bits, 4);
    }
}

std::uint32_t SchemaObject::GetFloatBits(std::string_view name, int k) const {
    const auto [f, v] = typed(name, "floatbits");
    if (!v) typeError(m_schema, name, "value not populated");
    std::uint32_t bits = 0;
    std::memcpy(&bits, v->raw.data() + scalarSlot(m_schema, *f, *v, name, k), 4);
    return bits;
}

void SchemaObject::SetFloatBits(std::string_view name, std::uint32_t bits, int k) {
    const auto [f, cur] = typed(name, "floatbits");
    (void)cur;
    FieldValue& fv = FieldRef(name);
    if (fv.raw.size() < 4u * repeat(*f)) fv.raw.resize(4u * repeat(*f), 0);
    std::memcpy(fv.raw.data() + scalarSlot(m_schema, *f, fv, name, k), &bits, 4);
}

bool SchemaObject::GetBool(std::string_view name, int k) const {
    const auto [f, v] = typed(name, "bool");
    if (!v) typeError(m_schema, name, "value not populated");
    return v->raw[scalarSlot(m_schema, *f, *v, name, k)] != 0;
}

void SchemaObject::SetBool(std::string_view name, bool value, int k) {
    const auto [f, cur] = typed(name, "bool");
    (void)cur;
    FieldValue& fv = FieldRef(name);
    if (fv.raw.size() < static_cast<std::size_t>(repeat(*f))) fv.raw.resize(static_cast<std::size_t>(repeat(*f)), 0);
    fv.raw[scalarSlot(m_schema, *f, fv, name, k)] = value ? 1 : 0;
}

const std::string& SchemaObject::Str(std::string_view name) const {
    const auto [f, v] = typed(name, "string");
    (void)f;
    if (!v) typeError(m_schema, name, "value not populated");
    return v->str;
}

std::string& SchemaObject::Str(std::string_view name) {
    (void)typed(name, "string");
    return FieldRef(name).str;
}

SchemaObject* SchemaObject::Child(std::string_view name) const {
    const auto [f, v] = typed(name, "child");
    (void)f;
    return v ? dynamic_cast<SchemaObject*>(v->obj.get()) : nullptr;
}

const std::vector<std::shared_ptr<IHavokObject>>& SchemaObject::Children(std::string_view name) const {
    static const std::vector<std::shared_ptr<IHavokObject>> kNone;
    const auto [f, v] = typed(name, "children");
    (void)f;
    return v ? v->objs : kNone;
}

// ── Construction: build a COMPLETE default object — correctly-sized zero bytes for every fixed
// field, position-computed Pad, default-initialized inline Structs, empty arrays/pointers. A caller
// then overwrites specific fields via FieldRef(); untouched fields serialize as valid defaults. The
// cursor walk mirrors SchemaRegistry::ComputeSize so Pad lands at the same offset as Read/Write. ──
void SchemaObject::Init() {
    IoRecurseGuard _rg;   // bound the inline-Struct recursion below (cyclic struct ref -> throw, not overflow)
    const std::vector<const Field*>& fields = Fields();
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
            case FieldKind::Text:        break;                                  // metadata text line (MetaText), no bytes
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

bool SchemaObject::HasField(std::string_view name) const {
    return IndexOf(name).has_value();
}

FieldValue& SchemaObject::FieldRef(std::string_view name) {
    const std::vector<const Field*>& fields = Fields();
    if (m_fields.size() != fields.size()) m_fields.assign(fields.size(), FieldValue{});  // lazy Init
    if (const auto i = IndexOf(name)) return m_fields[*i];
    throw std::runtime_error("havok-io: SchemaObject '" + m_schema->name + "' has no field '" + std::string(name) + "'");
}

void SchemaObject::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    IoRecurseGuard _rg;   // bound the inline-Struct recursion below (cyclic struct ref -> throw, not overflow)
    const std::vector<const Field*>& fields = Fields();
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
    const std::vector<const Field*>& fields = Fields();
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

std::shared_ptr<SchemaObject> DeserializeSchema(std::span<const std::uint8_t> bytes, const SchemaRegistry& reg,
                                                bool tolerateUnregistered) {
    PackFileDeserializer des;
    des.ObjectFactory = MakeSchemaFactory(reg);
    if (tolerateUnregistered) des.SetTolerateUnregistered(true);
    BinaryReaderEx br(/*bigEndian*/ false, /*uSizeLong*/ true, bytes);   // Deserialize re-reads both from the header
    return AsSchema(des.Deserialize(br));
}

std::shared_ptr<SchemaObject> FindVariant(const SchemaObject& root, std::string_view cls) {
    if (!root.HasField("namedVariants")) return nullptr;
    for (const auto& nvObj : root.Children("namedVariants")) {
        const auto nv = AsSchema(nvObj);
        if (!nv || nv->Str("className") != cls) continue;
        const FieldValue* v = nv->FindField("variant");
        return v ? AsSchema(v->obj) : nullptr;
    }
    return nullptr;
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

} // namespace CB::engine::codec::io
