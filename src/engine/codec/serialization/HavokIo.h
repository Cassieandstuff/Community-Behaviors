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
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace CB::engine::codec::io {

// One field's value, tagged by its schema FieldKind (the SchemaObject knows the kind, so the value
// store is a plain aggregate — at most one member is meaningful per field). Empty-slot/pad/skip and
// vtable carry raw bytes / a scalar; string(s) and object(s) carry the fixup-resolved payloads.
struct FieldValue {
    std::vector<std::uint8_t>                          raw;    // Scalar / Vector4 / Quaternion / QsTransform / BoolArray / Skip / Pad / raw arrays
    std::uint64_t                                      vtable = 0;  // Vtable (ReadUSize)
    std::string                                        str;    // String / CString
    std::vector<std::string>                           strs;   // StringArray
    std::shared_ptr<IHavokObject>                      obj;    // Ptr / inline Struct
    std::vector<std::shared_ptr<IHavokObject>>         objs;   // PtrArray / StructArray
};

// A generic Havok object: identity + field values come from a ClassSchema, not a C++ type.
class SchemaObject : public IHavokObject {
public:
    SchemaObject(const schema::SchemaRegistry* reg, const schema::ClassSchema* cs)
        : m_reg(reg), m_schema(cs) {}

    std::uint32_t Signature() const noexcept override { return m_schema->signature; }
    const char*   ClassName() const noexcept override { return m_schema->name.c_str(); }
    void Read(PackFileDeserializer& des, BinaryReaderEx& br) override;
    void Write(PackFileSerializer& s, BinaryWriterEx& bw) const override;

    const schema::ClassSchema* Schema() const noexcept { return m_schema; }

    // Field access for consumers (the .hky emit). `Fields()` is the flattened, parent-first
    // field list (schema order); `Values()` is the parallel value store (same indices). Populated by
    // Read; also available after construction for a caller that built the graph via Read.
    std::vector<const schema::Field*> Fields() const;
    const std::vector<FieldValue>&    Values() const noexcept { return m_fields; }

    // ── Construction (build a graph WITHOUT reading bytes — the compile direction) ──
    // Read fills m_fields; a from-scratch builder calls Init() to size the value store to the flattened
    // field list (all default/empty, vtable=0), then sets fields via Field(name)/FieldAt(i). The result
    // serializes through the same Write() path, so the compile output is byte-for-byte the serializer's
    // (no typed hk* classes involved). Field(name) throws if the name isn't in this class's flattened set.
    void        Init();
    FieldValue& FieldRef(const std::string& name);
    FieldValue& FieldAt(std::size_t i) { return m_fields.at(i); }
    bool        HasField(const std::string& name) const;

private:
    const schema::SchemaRegistry* m_reg;
    const schema::ClassSchema*    m_schema;
    std::vector<FieldValue>       m_fields;   // in serialized order (parent fields first)
};

// A factory that builds a SchemaObject for any class the registry knows, else nullptr (which the
// deserializer treats as "unregistered"). Install via `des.ObjectFactory = MakeSchemaFactory(reg);`.
std::function<std::shared_ptr<IHavokObject>(const std::string&)>
MakeSchemaFactory(const schema::SchemaRegistry& reg);

// Read a packfile fully through the schema path and write it back. On success `out` holds the
// re-serialized bytes; a byte-identical `out == in` is the hkx round-trip check. Returns false
// with `err` set on any parse/framing/schema error.
bool RoundtripHkx(const std::vector<std::uint8_t>& in,
                  const schema::SchemaRegistry&    reg,
                  std::vector<std::uint8_t>&       out,
                  std::string&                     err);

} // namespace CB::engine::codec::io
