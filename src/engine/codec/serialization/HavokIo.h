#pragma once
// HavokIo — the generic, schema-driven Havok packfile reader/writer.
//
// ONE interpreter for every class: a `SchemaObject` (the `IHavokObject` implementation) walks its
// `CB::engine::schema::ClassSchema` field list, driving the packfile framing (sections, fixups,
// BinaryReaderEx/WriterEx). The framing (which object is which class, pointer/array fixups) is
// layout-independent; only the per-object field walk is data-driven.
//
// Faithfulness note: fields with no fixup (scalars, inline vec4/quaternion/qstransform, bool arrays,
// and — critically — `skip`/`pad` regions) are captured as RAW BYTES and written back verbatim, so
// non-zero SERIALIZE_IGNORED bytes survive a round-trip (a zero-filling skip would discard them).

#include <interface/reflection/HavokSchema.h>

#include <codec/serialization/packfile/IHavokObject.h>
#include <codec/serialization/packfile/BinaryReaderEx.h>
#include <codec/serialization/packfile/BinaryWriterEx.h>
#include <codec/serialization/packfile/PackFileDeserializer.h>
#include <codec/serialization/packfile/PackFileSerializer.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CB::engine::codec::io {

// One field's value, tagged by its schema FieldKind (the SchemaObject knows the kind, so the value
// store is a plain aggregate — at most one member is meaningful per field). Empty-slot/pad/skip and
// vtable carry raw bytes / a scalar; string(s) and object(s) carry the fixup-resolved payloads.
struct FieldValue {
    std::vector<std::uint8_t>                          raw;    // Scalar / Vector4 / Quaternion / QsTransform / BoolArray / Skip / Pad / raw arrays
    std::uint64_t                                      vtable = 0;  // Vtable (ReadUSize)
    std::string                                        str;    // String / CString
    bool                                               strNull = false;  ///< a String read as the null pointer: written back as one while `str` is empty
    std::vector<std::string>                           strs;   // StringArray
    std::shared_ptr<IHavokObject>                      obj;    // Ptr / inline Struct
    std::vector<std::shared_ptr<IHavokObject>>         objs;   // PtrArray / StructArray
};

// A generic Havok object: identity + field values come from a ClassSchema, not a C++ type.
class SchemaObject : public IHavokObject {
public:
    SchemaObject(const schema::SchemaRegistry* reg, const schema::ClassSchema* cs);

    std::uint32_t Signature() const noexcept override { return m_schema->signature; }
    std::uint32_t VanillaSignature() const noexcept override {
        if (m_schema->vanillaSignature) return m_schema->vanillaSignature;
        return m_schema->name.rfind("CB", 0) == 0 ? 0 : m_schema->signature;
    }
    const char*   ClassName() const noexcept override { return m_schema->name.c_str(); }
    void Read(PackFileDeserializer& des, BinaryReaderEx& br) override;
    void Write(PackFileSerializer& s, BinaryWriterEx& bw) const override;

    const schema::ClassSchema* Schema() const noexcept { return m_schema; }

    // Field access for consumers (the .hky emit). `Fields()` is the flattened, parent-first
    // field list (schema order); `Values()` is the parallel value store (same indices). Populated by
    // Read; also available after construction for a caller that built the graph via Read.
    const std::vector<const schema::Field*>& Fields() const { return Layout().fields; }
    const std::vector<FieldValue>&           Values() const noexcept { return m_fields; }

    // ── Construction (build a graph WITHOUT reading bytes — the compile direction) ──
    // Read fills m_fields; a from-scratch builder calls Init() to size the value store to the flattened
    // field list (all default/empty, vtable=0), then sets fields via Field(name)/FieldAt(i). The result
    // serializes through the same Write() path, so the compile output is byte-for-byte the serializer's
    // (no typed hk* classes involved). Field(name) throws if the name isn't in this class's flattened set.
    void        Init();
    FieldValue& FieldRef(std::string_view name);
    FieldValue& FieldAt(std::size_t i) { return m_fields.at(i); }
    bool        HasField(std::string_view name) const;
    /// @brief Index of `name` in Fields()/Values(), or nullopt.
    std::optional<std::size_t> IndexOf(std::string_view name) const;
    /// @brief Read-only field value, or nullptr when the class has no such field or the store isn't populated.
    const FieldValue* FindField(std::string_view name) const;

    // ── Typed access, driven by the field's schema type ──
    /// @brief The width, signedness and kind come from the schema, never the caller; a kind mismatch, a
    ///        missing field or an out-of-range `k` (the inline repeat index) throws std::runtime_error.
    /// @note  Raw bytes stay available (FieldRef(name).raw) for what must pass through untouched: NaN payload
    ///        bits beyond Get/SetFloatBits, Half truncation, skip/pad regions, bulk blobs.
    std::int64_t GetInt(std::string_view name, int k = 0) const;          ///< integer Scalar, sign/zero-extended
    void         SetInt(std::string_view name, std::int64_t v, int k = 0);///< @warning throws if `v` doesn't fit the width
    float        GetFloat(std::string_view name, int k = 0) const;        ///< Float, or Half (upper 16 bits of a float)
    void         SetFloat(std::string_view name, float v, int k = 0);     ///< Half truncates to the upper 16 bits
    std::uint32_t GetFloatBits(std::string_view name, int k = 0) const;   ///< Float only: the exact bit pattern
    void          SetFloatBits(std::string_view name, std::uint32_t bits, int k = 0);
    bool         GetBool(std::string_view name, int k = 0) const;
    void         SetBool(std::string_view name, bool v, int k = 0);
    /// @brief A ScalarArray's elements. @pre sizeof(T) == the schema scalar width (checked).
    template <class T> std::vector<T> GetArray(std::string_view name) const;
    template <class T> void           SetArray(std::string_view name, const std::vector<T>& v);
    /// @brief String / CString value.
    const std::string& Str(std::string_view name) const;
    std::string&       Str(std::string_view name);
    /// @brief The SchemaObject behind a Ptr / inline Struct field (nullptr when unset).
    SchemaObject*      Child(std::string_view name) const;
    /// @brief The objects of a PtrArray / StructArray (or a fixed inline Ptr array).
    const std::vector<std::shared_ptr<IHavokObject>>& Children(std::string_view name) const;

private:
    const schema::FieldLayout& Layout() const;
    /// @brief The schema field + its populated value for a typed accessor, kind-checked.
    std::pair<const schema::Field*, const FieldValue*> typed(std::string_view name, const char* what) const;
    void checkArrayWidth(const schema::Field& f, std::size_t elem, std::string_view name) const;

    const schema::SchemaRegistry*              m_reg;
    const schema::ClassSchema*                 m_schema;
    const schema::FieldLayout*                 m_layout = nullptr;   ///< the registry's cache, or m_ownLayout
    std::shared_ptr<const schema::FieldLayout> m_ownLayout;          ///< only for a class the registry didn't cache
    std::vector<FieldValue>                    m_fields;   // in serialized order (parent fields first)
};

template <class T>
std::vector<T> SchemaObject::GetArray(std::string_view name) const {
    const auto [f, v] = typed(name, "ScalarArray");
    checkArrayWidth(*f, sizeof(T), name);
    if (!v) throw std::runtime_error("havok-io: " + m_schema->name + "." + std::string(name) + ": value not populated");
    std::vector<T> out(v->raw.size() / sizeof(T));
    if (!out.empty()) std::memcpy(out.data(), v->raw.data(), out.size() * sizeof(T));
    return out;
}

template <class T>
void SchemaObject::SetArray(std::string_view name, const std::vector<T>& v) {
    const auto [f, cur] = typed(name, "ScalarArray");
    (void)cur;
    checkArrayWidth(*f, sizeof(T), name);
    auto& raw = FieldRef(name).raw;
    raw.resize(v.size() * sizeof(T));
    if (!v.empty()) std::memcpy(raw.data(), v.data(), raw.size());
}

// A factory that builds a SchemaObject for any class the registry knows, else nullptr (which the
// deserializer treats as "unregistered"). Install via `des.ObjectFactory = MakeSchemaFactory(reg);`.
std::function<std::shared_ptr<IHavokObject>(const std::string&)>
MakeSchemaFactory(const schema::SchemaRegistry& reg);

/// @brief The SchemaObject behind a graph pointer, or nullptr.
inline std::shared_ptr<SchemaObject> AsSchema(const std::shared_ptr<IHavokObject>& o) {
    return std::dynamic_pointer_cast<SchemaObject>(o);
}

/// @brief Whether pointer member `f` (a Ptr or PtrArray) may hold `target`: its class is the member's `ref` or derives
///        from it. A null target, a member with no `ref` or one the registry doesn't know, and an object that isn't a
///        SchemaObject are all admitted: there's nothing to judge them against.
/// @note  The game reads a pointer as its declared class. A modifier list's entry that is really a clip trigger array
///        gets a modifier's virtual call and crashes, so the writer leaves such a pointer out (SchemaObject::Write).
bool AdmitsPointer(const schema::SchemaRegistry& reg, const schema::Field& f, const IHavokObject* target);
/// @brief `<class> '<name>'` (just the class when it has no name), for messages about an object.
std::string ObjectLabel(const IHavokObject* o);

/// @brief Deserialize a whole packfile through the schema path.
/// @return The root object (nullptr when the root isn't a SchemaObject).
/// @param tolerateUnregistered An object of a class the registry lacks reads as null instead of throwing.
/// @warning Throws std::runtime_error on a framing or schema error.
std::shared_ptr<SchemaObject> DeserializeSchema(std::span<const std::uint8_t> bytes, const schema::SchemaRegistry& reg,
                                                bool tolerateUnregistered = false);

/// @brief The `variant` object of the hkRootLevelContainer named variant whose `className` is `cls`
///        (e.g. "hkaAnimationContainer"), or nullptr.
std::shared_ptr<SchemaObject> FindVariant(const SchemaObject& root, std::string_view cls);

// Read a packfile fully through the schema path and write it back. On success `out` holds the
// re-serialized bytes; a byte-identical `out == in` is the hkx round-trip check. Returns false
// with `err` set on any parse/framing/schema error. `vanillaSignatures`: see PackFileSerializer.
bool RoundtripHkx(const std::vector<std::uint8_t>& in,
                  const schema::SchemaRegistry&    reg,
                  std::vector<std::uint8_t>&       out,
                  std::string&                     err,
                  bool                             vanillaSignatures = false);

} // namespace CB::engine::codec::io
