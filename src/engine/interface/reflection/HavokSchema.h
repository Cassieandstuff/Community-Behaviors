#pragma once
// The loader/model for the schema/ class schema (the source of truth for what a Havok
// class IS in the SSE/AE packfile). PASSIVE: it parses descriptors into an in-memory ClassSchema and
// computes the serialized size by walking fields. It knows NOTHING of read/write/transpile/convert/
// compile — those are CONSUMERS of this schema (the HavokIo serde, the merge engine). See
// schema/README.md for the descriptor format.

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace CB::engine::schema {

enum class FieldKind {
    Vtable,       // ReadUSize   — the runtime vtable slot (8 B, written 0)
    Scalar,       // a scalar (see Scalar)
    String,       // hkStringPtr (8 B)
    CString,      // char* CString (8 B) — ReadCString
    Quaternion,   // inline hkQuaternion (16 B)
    Ptr,          // class pointer (8 B); `ref` = target class
    StructArray,  // hkArray<T>  (16 B); `ref` = element class
    PtrArray,     // hkArray<T*> (16 B); `ref` = element class
    Vec4Array,    // hkArray<Vector4> (16 B)
    QsTransformArray, // hkArray<hkQsTransform> (16 B)
    StringArray,  // hkArray<hkStringPtr> (16 B) — ReadStringPointerArray
    ScalarArray,  // hkArray<scalar> (16 B); `scalar` = element type (float/int16/byte/…)
    Struct,       // inline value member; `ref` = its class (size added recursively)
    Vector4,      // inline hkVector4 (16 B)
    QsTransform,  // inline hkQsTransform (48 B)
    BoolArray,    // C-style bool[N] (N B); `count` = N
    EmptyArray,   // void-typed SERIALIZE_IGNORED hkArray slot (16 B, no value)
    EmptyPtr,     // void-typed SERIALIZE_IGNORED pointer slot (8 B, no value)
    Pad,          // align the cursor to `count`-byte boundary
    Skip,         // advance the cursor `count` bytes
    Text          ///< @brief A metadata (Bethesda text file) scalar: one line, carried VERBATIM in FieldValue::str —
                  ///<        "5.96046e-008" must round-trip as written, so it is never parsed to binary.
};

enum class Scalar { Int8, Byte, UInt16, Int16, UInt32, Int32, UInt64, Int64, Float, Half, Bool };

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
    bool        boneRef = false;         ///< @brief `boneref: true` — a scalar bone index into the graph's rig; the .hky
                                         ///<        names the bone (-1 = none, omitted).
    bool        boneList = false;        ///< @brief `bonelist: true` — a scalar array of bone indices; the .hky lists names.
    bool        boneSet = false;         ///< @brief `boneset: true` — that bone list is a SET (order carries nothing): a patch
                                         ///<        adds / removes bones by name instead of replacing the list.
    std::string boneOf;                  ///< @brief `boneof: <field>` — a scalar bone index (or index array) into the skeleton
                                         ///<        the enclosing object's `<field>` pointer names (a mapper's skeletonA /
                                         ///<        skeletonB), not the graph's rig; the .hky names the bone.
    bool        boneWeights = false;     ///< @brief `boneweights: <default>` — a float array indexed by rig bone; the .hky
    std::string boneWeightDefault;       ///<        is a bone-name -> weight map, unlisted bones taking this default.
    std::string roster;                  ///< @brief `roster: <key>` — the array is a keyed ROSTER in the .hky: `ref` (keyed by
                                         ///<        the child node's FormID), `field:<f>` (by an element's string field),
                                         ///<        `bone:<f>` (by the bone an element's index field names), `id` (minted
                                         ///<        sub-record FormIDs, owner-qualified, scoped to the parent), `slot` (as
                                         ///<        `id`, but entries are referenced by position elsewhere: never deleted).
    std::string charPropIf;              ///< @brief `charpropif: <field>=<value>` — the `varref` index names a CHARACTER
                                         ///<        PROPERTY (not a variable) when that sibling field holds that value.
    std::string unitRef;                 ///< @brief `unitref: behavior|character|skeleton|animation` — the string (or each
                                         ///<        string of a stringarray) names another file of that kind by its path
                                         ///<        relative to the PROJECT folder (`Behaviors\1HM_Behavior.hkx`); empty = not
                                         ///<        a reference. A member attribute: no serde, size or signature effect.
                                         ///< @note The string stays byte-exact; only matching it to a unit folds case
                                         ///<       (path identity, as the engine's own lookup does).
    bool        hkyEmit = false;         // force-emit this field in the .hky even though it is `ignored`
                                         // (SERIALIZE_IGNORED but semantically authored, e.g.
                                         // hkbBlendingTransitionEffect.initializeCharacterPose).
    std::string eventRef;                // scalar is an event index → .hky also emits `<eventRef>: '<name>'`
    std::string varRef;                  // scalar is a variable index → .hky also emits `<varRef>: '<name>'`
    std::string header;                  ///< @brief metadata `recordarray`: `header: <field>` hoists that field of every
                                         ///<        record into a leading list (count, N values), the bodies follow.
    std::string when;                    ///< @brief metadata: present only when the named bool field reads "1".
    std::string frame;                   ///< @brief metadata: fields sharing a frame are prefixed by ONE line count
                                         ///<        covering all their lines (the adsf section A / B framing).
    bool        frameCounted = false;    ///< @brief metadata `recordarray` with `count: frame`: no count prefix; records
                                         ///<        run to the end of the field's frame.
    std::string metaType;                ///< @brief metadata scalar: the declared `type:` (string/int32/uint32/float/…) — what
                                         ///<        an `encoding: binary` record reads off the bytes into its text value.
    std::string hkType;                  ///< @brief `hktype: ulong|enum|flags` (`stringptr` on an `empty: ptr` slot: an
                                         ///<        ignored hkStringPtr, e.g. hkbProjectStringData.rootPath) — the member's reflected type where it differs
                                         ///<        from what `type:` implies (hkbNode.userData is a ULONG; an `enum`/`flags`
                                         ///<        here carries no enum reference). Signature only; no serde effect.
    bool        enumHkyOnly = false;     ///< @brief `hkyenum:` rather than `enum:` — the .hky renders the value by name, but
                                         ///<        the exe reflects a plain scalar (no enum in the signature).
    std::string roleName;                ///< @brief `role: { role: <Role item> }` — what the member is for, as Havok's
                                         ///<        hkbRoleAttribute::m_role tags it (e.g. ROLE_TIME); empty = ROLE_DEFAULT.
    std::vector<std::string> roleFlagNames;   ///< @brief `role: { flags: [<RoleFlags item>, …] }` — e.g. FLAG_OUTPUT: the
                                              ///<        node writes this member (another node or an output binding reads it).
    int         role      = 0;           ///< @brief roleName resolved against the Role enum by LoadDir.
    int         roleFlags = 0;           ///< @brief roleFlagNames resolved against the RoleFlags enum by LoadDir, OR'd.
                                         ///< @note Item names are matched exactly (Havok's spelling); an unknown one fails
                                         ///<       the load. A member attribute: no serde, size or signature effect.
    bool        cbAdded = false;         ///< @brief `cb: true` — a member CB adds to a vanilla class (in its padding: the
                                         ///<        exe's code never touches it). Left out of the class's vanilla signature.
    bool         user      = false;      ///< @brief `user: <value>` — the game fills this member at runtime; `<value>` is
    std::int64_t userValue = 0;          ///<        the binary sentinel a file stores in its place. The .hky writes `user`
                                         ///<        exactly when the stored value equals it, and reads `user` back to it.
                                         ///< @note Only a single integer scalar (LoadDir refuses another kind). A "none"
                                         ///<       index (an event id, a sync variable) is data, not `user`: it stays -1.
    std::string charProp;                ///< @brief `charprop: <class>|bone` — the member's bone data comes from a CHARACTER
                                         ///<        PROPERTY: a pointer to `<class>` (its `ref`, a bone-weight or bone-index
                                         ///<        set) bound to a POINTER property, or (`bone`, on a `boneref` scalar) an
                                         ///<        INT32 property holding the bone's index in each character's rig. Bound and
                                         ///<        holding its neutral value (CharPropNeutral), the .hky writes the member as
                                         ///<        `{ charprop: '<property>' }` and its binding folds into it; a behavior
                                         ///<        whose member holds bone data inline instead is flagged (CharPropViolations).
};

struct ClassSchema {
    std::string        name;
    std::string        parent;               // empty for a root class
    std::uint32_t      signature = 0;        // the class CRC
    /// @brief For a vanilla class with `cb: true` members: the exe's signature, as `signature:` stores it. LoadDir then
    ///        sets `signature` to the one with CB's members, which CB registers and writes.
    std::uint32_t      vanillaSignature = 0;
    int                interfaces = 0;       ///< @brief `interfaces: N` — hkClass::m_numImplementedInterfaces (signature only).
    std::vector<std::string> enums;          ///< @brief `enums: [A, B]` — the class's declared enums in order, each a
                                             ///<        `kind: enum` descriptor (signature only).
    int                size      = 0;        // OPTIONAL declared serialized size (bytes, SSE 64-bit). The
                                             // authority is ComputeSize (derived from the field layout,
                                             // used by the serializer); this is a cross-check — LoadDir
                                             // asserts ComputeSize == size when present (0 = omit/derive).
    std::vector<Field> fields;               // this class's OWN fields, in serialized order (parent's come first, from its schema)
    std::string        vocab;                    ///< @brief `vocab: graph|character` — the .hky renders this object's name
                                                 ///<        rosters as name-keyed records (see renderVocab), not parallel arrays.
    bool               metadata  = false;        ///< @brief `kind: metadata` — a Bethesda text-file record (adsf/asdsf), not
                                                 ///<        a Havok class: no signature/size; read/written by MetaText.
    bool               blankTerminated = false;  ///< @brief metadata `terminator: blank` — a blank line ends each record.
    bool               binary = false;           ///< @brief metadata `encoding: binary` — little-endian scalars, a
                                                 ///<        recordarray = u32 count + records (the speeddata bodies).
    int                frameLimit = 0;           ///< @brief metadata `framelimit: N` — a frame's line count may not exceed N
                                                 ///<        (the engine reads it through a 16-bit truncation: 65535),
                                                 ///<        unless the writer is told the reader takes the full count.
    std::string        unwrap;                   ///< @brief `unwrap: <field>` — a .hky convenience: a pointer/struct to this
                                                 ///<        class renders as that one field's value directly, when every other
                                                 ///<        field would emit nothing (else the full object form, so lossless).
    std::string        factory;                  ///< @brief `factory: <name>` — the engine factory name this class stands for,
                                                 ///<        byte-exact (a typed movement agent's "Player Controls"), so a node's
                                                 ///<        name field can be checked against its class. No serde, size or
                                                 ///<        signature effect.
                                                 ///< @invariant At most one class claims a given name (LoadDir).
    /// @brief `view: { kind: <view>, <role>: <field>, ... }` — how an editor shows this class: `kind` names the view,
    ///        every other key maps one of that view's roles to one of the class's OWN fields. No serde, size or
    ///        signature effect.
    /// @note  Known views (LoadDir rejects any other, or a missing role): `curve` (keys, label) — a named list of keys;
    ///        `curvekey` (time, value, interp) — one key of a curve. CB-API's SampleCurve reads a curve by them.
    struct View {
        std::string                        kind;
        std::map<std::string, std::string> roles;   ///< role -> field name
    } view;
};

/// @brief A `kind: embedded` descriptor: a hand-parsed file kind whose `root:` top-level map holds the fields of the
///        schema class `cls`, beside keys the schema does not describe (an animation unit's `curves:`).
/// @note  A file is of the kind when its first top-level key is `root`. An editor parses and saves only the class's
///        fields; every other byte of the file is left as it is.
struct EmbeddedKind {
    std::string name;
    std::string root;
    std::string cls;
};

/// @brief A `kind: rule` descriptor (schema/metadata/rules/): a transform over a whole unit set, run by the C++
///        operation its `op:` names (CB::engine::rules), together with its inverse.
/// @note  The schema only checks the descriptor's shape and that every rule it orders against exists; the op is
///        resolved when the rules run.
struct RuleDef {
    std::string              name;
    std::string              op;
    std::vector<std::string> after;    ///< `order: { after: [...] }` — rules that run before this one
    std::vector<std::string> before;   ///< `order: { before: [...] }` — rules that run after this one
    std::vector<std::string> runs;     ///< `runs: [regen, converter]` — where it applies
    std::vector<std::string> select;   ///< `select: [...]` — the field kinds the op touches (RuleSelects)
    std::string              text;     ///< the whole descriptor, for the op's own `params:`
    std::string              file;     ///< the descriptor's path, for messages
};

/// @brief Whether field `f` is of selector kind `token`: `boneweights`, `bonelist`, `boneref` (the schema row keys), `user`
///        or `charprop`. An unknown token is never true; LoadDir refuses one in a descriptor.
bool RuleSelects(const Field& f, const std::string& token);

// ── Enum definitions (`kind: enum` descriptors) ─────────────────────────────────────────────────
// A reflected Havok enum: its type name + ordered (name,value) items. The item NAMES and VALUES are
// CASE-SENSITIVE Havok data — they render a .hky field's value by name (MODE_SINGLE_PLAY, not 47) AND
// feed the class-signature CRC. A field references an enum by name via Field::enumName; the enum lives
// here, in the data tree, next to the classes that use it (single source — formerly HavokEnums.h).
struct EnumItem {
    std::string name;
    long        value = 0;   // may be negative (e.g. VARIABLE_TYPE_INVALID = -1)
};
struct EnumDef {
    std::string           name;
    bool                  isFlags = false;   // OR-able bitfield → .hky renders via FormatFlags
    std::vector<EnumItem> items;             // in declared order (order matters for the signature)
};

// Serialized byte width of a scalar (SSE 64-bit).
int ScalarWidth(Scalar s);

/// @brief The ONE identity of the schema/ class-layout + merge-policy contract: SemVer over WIRE + MERGE
///        compatibility, with an optional `-rc.N` prerelease.
/// @note Single source is CB_SCHEMA_VERSION (root CMakeLists.txt, which also holds the bump rules); it is
///       generated into each shipped tree's SCHEMA.yaml stamp and into kSchemaVersion (SchemaVersion.gen.h).
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

/// @brief A class's flattened serialized field list (parent fields first, schema order) + a name → index map.
/// @invariant `index` maps each name to its FIRST occurrence in `fields` (a parent's field shadows a same-named
///            child field — the order the per-name lookups have always resolved).
struct FieldLayout {
    struct Hash {
        using is_transparent = void;
        std::size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
    };
    std::vector<const Field*>                                                    fields;
    std::unordered_map<std::string, std::size_t, Hash, std::equal_to<>>         index;
};

class SchemaRegistry {
public:
    SchemaRegistry() = default;
    /// @note Non-copyable: the cached layouts point into this registry's own ClassSchemas. A move keeps the
    ///       std::map nodes (and so every pointer) valid.
    SchemaRegistry(const SchemaRegistry&)            = delete;
    SchemaRegistry& operator=(const SchemaRegistry&) = delete;
    SchemaRegistry(SchemaRegistry&&)                 = default;
    SchemaRegistry& operator=(SchemaRegistry&&)      = default;

    /// @brief Load every *.yaml/*.yml under `root`, recursively, dispatched on its top-level `kind:` (none = a class;
    ///        `metadata`, `enum`, `semantics`, `probe`, `compatshim`, `embedded`, `rule`). Non-yaml files are ignored.
    /// @note Folders carry no meaning. A name defined twice within a kind (classes and metadata records share one
    ///       namespace) or an unknown kind fails the load, with `err` naming the files.
    bool LoadDir(const std::string& root, std::string& err);

    const ClassSchema* Find(const std::string& name) const;
    /// @brief Whether class `cls` is `base` or derives from it through its `parent:` chain. False when `cls` is unknown.
    bool Derives(const std::string& cls, const std::string& base) const;

    /// @brief The class's flattened field list, parent-first.
    /// @warning Throws std::runtime_error on an unresolved or cyclic `parent:` chain.
    std::vector<const Field*> Flatten(const ClassSchema* cs) const;

    /// @brief The cached layout of a class this registry loaded, or nullptr (not ours, or its parent chain
    ///        doesn't resolve — Flatten then throws the reason).
    /// @note Built once at the end of a successful LoadDir; the registry is immutable afterwards, so lookups
    ///       are lock-free and safe from any thread.
    const FieldLayout* Layout(const ClassSchema* cs) const;
    const std::map<std::string, ClassSchema>& All() const { return m_byName; }

    // Enum definitions (`kind: enum` descriptors). Find one by its type name (e.g. "PlaybackMode").
    // Empty until LoadDir has run over a tree that carries enum descriptors.
    const EnumDef* FindEnum(const std::string& name) const;

    /// @brief The text of the `kind: compatshim` descriptor named `name` (e.g. "boneorder"), or nullptr.
    /// @note A shim is compatibility data the schema carries for a reader of its own, not a class: LoadDir checks only
    ///       that it parses and names itself once.
    const std::string* CompatShim(const std::string& name) const;

    /// @brief Every `kind: embedded` descriptor LoadDir read, by name.
    const std::map<std::string, EmbeddedKind>& EmbeddedKinds() const { return m_embedded; }

    /// @brief Every `kind: rule` descriptor LoadDir read, by name.
    const std::map<std::string, RuleDef>& Rules() const { return m_rules; }

    /// @brief The raw `schema_version:` LoadDir read from the tree's SCHEMA.yaml stamp; empty when the tree
    ///        carried none (the unstamped source schema/). SharedRegistry() gates it against kSchemaVersion.
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

    // Apply one `kind: semantics` document onto the already-loaded classes: each top-level key
    // is a merge strategy (the string Field::merge carries — "compose"/"guarded"/"replace"), and its
    // list holds "<Class>" (whole-class default for every field) or "<Class>.<field>" (one field,
    // overriding the class default). This is the single auditable source for merge policy; LoadDir
    // calls it after the class walk. A wrong class/field name is a hard error (fails loud, by design —
    // the whole point is that a mis-assignment can't hide the way a scattered per-field tag could).
    bool ApplyMergeSemantics(const std::string& yamlText, std::string& err);

    // Field-walk serialized size for `className`: parent fields (recursively) then this class's,
    // honoring scalar widths + pad(align)/skip and inline Struct refs. Returns the computed byte
    // size, or -1 if a parent (or a Struct field's `ref`) is unresolved (`err` explains).
    // `depth` bounds the parent + inline-Struct recursion so a cyclic descriptor (A.parent=B,
    // B.parent=A, or a struct that references itself) returns an error instead of overflowing the
    // stack — schemas are community-contributable, so a cyclic descriptor is reachable input.
    int ComputeSize(const std::string& className, std::string* err = nullptr, int depth = 0) const;

    /// @brief The byte offset of each of `className`'s OWN fields (parallel to ClassSchema::fields), by the same walk
    ///        ComputeSize takes: what a runtime reflection of the class (hkClassMember offsets) must agree with.
    /// @return empty with `err` set when the class or a parent is unresolved.
    std::vector<int> OwnFieldOffsets(const std::string& className, std::string* err = nullptr) const;

    /// @brief The class signature the engine computes from `className`'s reflection (hkClass::getSignature, flags 0).
    /// @note  CRC-32 (zlib's) over each class's own block, the class first and then up its parent chain; a block is
    ///        u32 interfaces, the declared enums, u32 enum count, the members, u32 member count. A member emits the
    ///        class chain of an inline struct (never through a pointer), its enum, its name raw, then u16 type, u16
    ///        subtype, u16 cArraySize, u16 flags. ENUM/FLAGS: the flags word is xored with size(subtype) << 3 and the
    ///        subtype word is 0. SERIALIZE_IGNORED: the bit is dropped, the type word becomes TYPE_ZERO, the subtype
    ///        word the real type (plus one word for a nonzero real subtype). Offsets and class names are not hashed.
    ///        RE'd from AE 1.6.1170 (getSignature 0x140a97cf0, writeSignature 0x140a97d80); reproduces the stored
    ///        signatures of the hkbGenerator chain and hkbManualSelectorGenerator.
    /// @warning The schema is a serialization projection: a class whose descriptor drops reflected members (or their
    ///          flags) computes a different value. Every class touched that stores a `signature:` is checked against
    ///          it, so a wrong answer fails here rather than in the game's loader.
    /// @return false with `err` set on an unresolved class or enum, or a touched class whose stored signature differs.
    /// @param vanilla leave out every `cb: true` member (the signature the exe computes for its own description).
    bool ComputeSignature(const std::string& className, std::uint32_t& out, std::string* err = nullptr, bool vanilla = false) const;

private:
    /// @brief Advance `cursor` past `f` (a Pad aligns it); false with `err` set on an unresolved inline Struct.
    bool AdvanceField(const Field& f, int& cursor, std::string* err, int depth) const;

    /// @brief Append the signature bytes of `cs` and its parent chain; every class visited lands in `touched`.
    bool EmitSignatureChain(const ClassSchema* cs, std::string& out, std::vector<const ClassSchema*>& touched,
                            std::string* err, int depth, bool vanilla) const;
    /// @brief Append one class's own signature block (see ComputeSignature).
    bool EmitSignatureOwn(const ClassSchema& cs, std::string& out, std::vector<const ClassSchema*>& touched,
                          std::string* err, int depth, bool vanilla) const;

    void BuildLayouts();

    std::map<std::string, ClassSchema> m_byName;
    std::unordered_map<const ClassSchema*, FieldLayout> m_layouts;   ///< see Layout()
    std::map<std::string, EnumDef>     m_enums;               // `kind: enum` descriptors, keyed by enum type name
    std::map<std::string, std::string> m_compatShims;         ///< `kind: compatshim` texts, keyed by `name:`
    std::map<std::string, EmbeddedKind> m_embedded;           ///< `kind: embedded` descriptors, keyed by `name:`
    std::map<std::string, RuleDef>     m_rules;               ///< `kind: rule` descriptors, keyed by `name:`
    std::string                        m_schemaVersion;      // SCHEMA.yaml `schema_version:` (raw)
    bool                               m_schemaStampPresent = false;  // a SCHEMA.yaml file was seen
};

// Parse ONE descriptor's YAML text into a ClassSchema (LoadDir uses it; exposed for tests).
bool ParseSchema(const std::string& yamlText, ClassSchema& out, std::string& err);

// Parse ONE `kind: enum` descriptor's YAML text into an EnumDef (LoadDir uses it; exposed for tests).
bool ParseEnumDef(const std::string& yamlText, EnumDef& out, std::string& err);

/// @brief Parse ONE `kind: rule` descriptor (LoadDir uses it; exposed for tests).
/// @return false with `err` set on a missing name / op / runs, an unknown `runs:` stage or `select:` token.
bool ParseRuleDef(const std::string& yamlText, RuleDef& out, std::string& err);

/// @brief A schema descriptor's top-level `kind:` (empty for a class), the one key LoadDir dispatches on.
/// @return false with `err` set when the text is not a YAML map.
bool DescriptorKind(const std::string& yamlText, std::string& out, std::string& err);

// ── Shared process-wide registry ───────────────────────────────────────────────
// The ONE lazily-loaded schema registry every schema-driven compile / decompile shares, so the tree loads
// ONCE and the editor<->compiler version-stamp contract is enforced in a single place.

// Configure the directory the shared registry loads from (the deployed schema/ tree). Overrides the
// $SCT_HAVOK_SCHEMA_DIR fallback. Call before the first SharedRegistry() — it does not force a reload.
void SetSharedSchemaDir(const std::string& dir);

/// @brief The directory the shared registry loads (or loaded) from: SetSharedSchemaDir's, else $SCT_HAVOK_SCHEMA_DIR;
///        empty when neither is set.
std::string SharedSchemaDir();

// The shared registry, lazily loaded ONCE on first call (from SetSharedSchemaDir's dir, else
// $SCT_HAVOK_SCHEMA_DIR). Returns nullptr when unconfigured or when the load/version-stamp gate
// failed — the reason is in SharedRegistryError(). Load is attempted at most once.
SchemaRegistry* SharedRegistry();

// Why SharedRegistry() returned nullptr (no dir configured, load failure, or a version-stamp
// integrity refusal). Empty while healthy. Populated as a side effect of the first SharedRegistry().
const std::string& SharedRegistryError();

} // namespace CB::engine::schema
