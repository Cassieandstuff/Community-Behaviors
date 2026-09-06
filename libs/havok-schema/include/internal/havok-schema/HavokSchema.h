#pragma once
// havok-schema — the loader/model for the Havok/ class schema (the source of truth for what a Havok
// class IS in the SSE/AE packfile). PASSIVE: it parses descriptors into an in-memory ClassSchema and
// computes the serialized size by walking fields. It knows NOTHING of read/write/transpile/convert/
// compile — those are CONSUMERS of this schema (havok-io serde, the merge engine, codegen). See
// Havok/README.md for the descriptor format. Single public header (the monorepo new-library rule):
// consumers write  #include <havok-schema/HavokSchema.h>  and nothing else.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace havok::schema {

enum class FieldKind {
    Vtable,       // ReadUSize   — the runtime vtable slot (8 B, written 0)
    Scalar,       // a scalar (see Scalar)
    String,       // hkStringPtr (8 B)
    CString,      // char* CString (8 B) — ReadCString
    Quaternion,   // inline hkQuaternion (16 B) — ReadQuaternion
    Ptr,          // class pointer (8 B); `ref` = target class
    StructArray,  // hkArray<T>  (16 B); `ref` = element class
    PtrArray,     // hkArray<T*> (16 B); `ref` = element class
    Vec4Array,    // hkArray<Vector4> (16 B)
    QsTransformArray, // hkArray<hkQsTransform> (16 B) — ReadQSTransformArray
    StringArray,  // hkArray<hkStringPtr> (16 B) — ReadStringPointerArray
    ScalarArray,  // hkArray<scalar> (16 B); `scalar` = element type (float/int16/byte/…)
    Struct,       // inline value member; `ref` = its class (size added recursively)
    Vector4,      // inline hkVector4 (16 B)      — ReadVector4
    QsTransform,  // inline hkQsTransform (48 B)  — ReadQSTransform
    BoolArray,    // C-style bool[N] (N B); `count` = N
    EmptyArray,   // void-typed SERIALIZE_IGNORED hkArray slot (16 B, no value)
    EmptyPtr,     // void-typed SERIALIZE_IGNORED pointer slot (8 B, no value)
    Pad,          // align the cursor to `count`-byte boundary
    Skip          // advance the cursor `count` bytes
};

enum class Scalar { Int8, Byte, UInt16, Int16, UInt32, Int32, UInt64, Int64, Float, Half, Bool };

enum class RuntimeSource { Game, Self };   // game = engine vtable (Address Library); self = our reimplementation

struct Field {
    FieldKind   kind    = FieldKind::Skip;
    std::string name;                    // real fields (Scalar/String/Ptr/…)
    Scalar      scalar  = Scalar::Int32; // when kind == Scalar
    std::string ref;                     // Ptr/StructArray/PtrArray/Struct target class
    int         count   = 0;             // BoolArray element count; Pad = align boundary; Skip = byte count;
                                         // Scalar/Ptr/Vector4 = fixed inline repeat count (std::array<T,N>), default 1
    bool        ignored = false;         // typed SERIALIZE_IGNORED (documentation only — no serde effect)
    std::string merge;                   // per-field merge strategy (consumed by the merge engine, not serde)
    std::string enumName;                // Scalar enum type (e.g. "PlaybackMode") — the .hky YAML renders
                                         // the value by name (MODE_SINGLE_PLAY); no serde/size effect.
    bool        isFlags = false;         // enum is an OR-able bitfield → .hky renders via FormatFlags
                                         // (A|B) rather than a single name. (.hky-only; no serde effect.)
    bool        hkyEmit = false;         // force-emit this field in the .hky even though it is `ignored`
                                         // (SERIALIZE_IGNORED but semantically authored, e.g.
                                         // hkbBlendingTransitionEffect.initializeCharacterPose).
    std::string eventRef;                // scalar is an event index → .hky also emits `<eventRef>: '<name>'`
    std::string varRef;                  // scalar is a variable index → .hky also emits `<varRef>: '<name>'`
};

struct ClassSchema {
    std::string        name;
    std::string        parent;               // empty for a root class
    std::uint32_t      signature = 0;        // the class CRC (HK_CLASS_ID sig)
    int                size      = 0;        // DECLARED serialized size (bytes, SSE 64-bit) — the ground-truth cross-check
    std::vector<Field> fields;               // this class's OWN fields, in serialized order (parent's come first, from its schema)
    RuntimeSource      runtime   = RuntimeSource::Game;
};

// Serialized byte width of a scalar (SSE 64-bit).
int ScalarWidth(Scalar s);

// ── Schema version — the editor <-> compiler contract stamp ──────────────────────────────────────
// The ONE identity of the Havok/ class-layout + merge-policy contract (Havok/SCHEMA.yaml carries a
// single `schema_version:`). A produced .hky echoes it into its manifest; the compiler gates ingest
// on it. SemVer over WIRE + MERGE compatibility, with an optional `-rc.N` prerelease. See the
// schema-version-stamp scheme for the full rationale.
struct SchemaVersion {
    int         major = 0;
    int         minor = 0;
    int         patch = 0;
    std::string prerelease;      // e.g. "rc.1"; empty = a final release
    bool        valid = false;   // false = absent / malformed

    bool        IsPrerelease() const { return !prerelease.empty(); }
    std::string Str() const;     // canonical "MAJOR.MINOR.PATCH[-prerelease]"

    // Parse "1.0.0-rc.1" (quotes/whitespace tolerated). A malformed/empty string yields valid=false.
    static SchemaVersion Parse(const std::string& s);
};

enum class SchemaCompat { Ok, Refuse };

// Gate an AUTHORED-against version against the CURRENT (compiler/tree) version, per the scheme:
//   * either side unparseable              -> Refuse
//   * either side is a prerelease (-rc.N)  -> EXACT match or Refuse (candidate-frozen, not frozen)
//   * else major mismatch                  -> Refuse (old bytes mis-parse)
//   * else authored.minor > current.minor  -> Refuse (author used classes/fields we don't have)
//   * else                                 -> Ok    (compiler is a superset; patch ignored)
// On Refuse, `why` is filled with a human-readable reason.
SchemaCompat CheckSchemaCompat(const SchemaVersion& authored, const SchemaVersion& current,
                               std::string& why);

class SchemaRegistry {
public:
    // Load every *.yaml/*.yml under `root` (recursively) as a ClassSchema. On a parse error returns
    // false with `err` set. Non-yaml files (README, etc.) are ignored.
    bool LoadDir(const std::string& root, std::string& err);

    const ClassSchema* Find(const std::string& name) const;
    const std::map<std::string, ClassSchema>& All() const { return m_byName; }

    // The raw `schema_version:` string LoadDir read from Havok/SCHEMA.yaml (empty when the tree
    // carried no stamp — a legacy/unversioned tree). This is what THIS load of the tree speaks:
    // the compiler's "current" version for the ingest gate. Parse it with SchemaVersion::Parse.
    const std::string& SchemaVersionString() const { return m_schemaVersion; }

    // Whether a SCHEMA.yaml stamp FILE was seen at all during LoadDir, regardless of whether a
    // version could be read out of it. Lets a consumer tell a truly UNSTAMPED tree (no file →
    // lenient) apart from a PRESENT-but-unreadable stamp (malformed YAML / missing key → an
    // integrity concern to refuse), which both leave SchemaVersionString() empty.
    bool SchemaStampPresent() const { return m_schemaStampPresent; }

    // The `merge:` tag declared for field `field` on class `className` (e.g. "compose"/"guarded"),
    // or "" when the class/field is unknown or the field carries no tag. THE single tag query both
    // merge adapters share — the runtime ryml merge and the converter xml merge ask this one method,
    // so the "which arrays compose" rule is answered identically and can never drift. Only the
    // class's OWN fields are consulted (the tagged arrays are direct members of their class).
    std::string MergeTag(const std::string& className, const std::string& field) const;

    // Field-walk serialized size for `className`: parent fields (recursively) then this class's,
    // honoring scalar widths + pad(align)/skip and inline Struct refs. Returns the computed byte
    // size, or -1 if a parent (or a Struct field's `ref`) is unresolved (`err` explains).
    // `depth` bounds the parent + inline-Struct recursion so a cyclic descriptor (A.parent=B,
    // B.parent=A, or a struct that references itself) returns an error instead of overflowing the
    // stack — schemas are community-contributable, so a cyclic descriptor is reachable input.
    int ComputeSize(const std::string& className, std::string* err = nullptr, int depth = 0) const;

private:
    std::map<std::string, ClassSchema> m_byName;
    std::string                        m_schemaVersion;      // Havok/SCHEMA.yaml `schema_version:` (raw)
    bool                               m_schemaStampPresent = false;  // a SCHEMA.yaml file was seen
};

// Parse ONE descriptor's YAML text into a ClassSchema (LoadDir uses it; exposed for tests).
bool ParseSchema(const std::string& yamlText, ClassSchema& out, std::string& err);

} // namespace havok::schema
