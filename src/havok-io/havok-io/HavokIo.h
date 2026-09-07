#pragma once
// havok-io — the generic, schema-driven Havok packfile reader/writer (havok-core v2 rewrite, Stage 2).
//
// ONE interpreter replaces the hand-written per-class ClassRead/ClassWrite: a `SchemaObject` (a
// generic `IHavokObject`) walks its `havok::schema::ClassSchema` field list, driving havok-core's
// EXISTING packfile framing (sections, fixups, BinaryReaderEx/WriterEx) through the same public
// primitives the typed classes used. The framing (which object is which class, pointer/array fixups)
// is layout-independent and salvaged wholesale; only the per-object field walk becomes data-driven.
//
// Faithfulness note: fields with no fixup (scalars, inline vec4/quaternion/qstransform, bool arrays,
// and — critically — `skip`/`pad` regions) are captured as RAW BYTES and written back verbatim. This
// preserves non-zero SERIALIZE_IGNORED bytes that the typed path's zero-filling `Skip` discarded, so
// havok-io is strictly MORE byte-faithful than the old serde (it is expected to CLOSE Tier-A gaps).
//
// Single public header (monorepo rule): consumers write  #include <havok-io/HavokIo.h>  and nothing
// else.

#include <havok-schema/HavokSchema.h>

#include "havok/classes/IHavokObject.h"
#include "havok/core/BinaryReaderEx.h"
#include "havok/core/BinaryWriterEx.h"
#include "havok/core/PackFileDeserializer.h"
#include "havok/core/PackFileSerializer.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace havok::io {

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

    // Field access for consumers (havok-model's .hky emit). `Fields()` is the flattened, parent-first
    // field list (schema order); `Values()` is the parallel value store (same indices). Populated by
    // Read; also available after construction for a caller that built the graph via Read.
    std::vector<const schema::Field*> Fields() const;
    const std::vector<FieldValue>&    Values() const noexcept { return m_fields; }
    const schema::SchemaRegistry*     Reg() const noexcept { return m_reg; }

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

// Read a packfile fully through havok-io (generic path) and write it back. On success `out` holds the
// re-serialized bytes; a byte-identical `out == in` is the Stage-2 hkx-roundtrip gate. Returns false
// with `err` set on any parse/framing/schema error.
bool RoundtripHkx(const std::vector<std::uint8_t>& in,
                  const schema::SchemaRegistry&    reg,
                  std::vector<std::uint8_t>&       out,
                  std::string&                     err);

// Like RoundtripHkx, but deep-REBUILDS the object graph via the construction API (Init +
// FieldRef/FieldAt, no Read) before serializing — the migration-foundation gate. A byte-identical
// `out == in` proves from-scratch SchemaObject construction is byte-faithful, so a builder that
// emits SchemaObjects (replacing the typed hk* builders) can be trusted to the same standard.
bool RebuildHkx(const std::vector<std::uint8_t>& in,
                const schema::SchemaRegistry&    reg,
                std::vector<std::uint8_t>&       out,
                std::string&                     err);

} // namespace havok::io
