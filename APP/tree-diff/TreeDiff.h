#pragma once
// TreeDiff — the domain-agnostic semantic diff engine behind `havok-core-cli tree-diff`.
//
// The engine knows NOTHING about behaviors, setdata, animdata, skeletons or characters. It
// diffs two `RecordSet`s — maps of `stable-identity-key -> YAML text` — under a `DiffPolicy`
// that names the order-significant / order-agnostic arrays and the volatile fields to skip.
// Each domain lives in a thin adapter (in the CLI, where the decompiler/decomposers are
// linked) that turns an artifact into RecordSets + supplies the policy; this file is pure.
//
// Why record-keyed and not positional/id-keyed: CB reorders nodes and reassigns numeric ids
// at will, so any id- or position-keyed diff is meaningless. The decompiler already gives each
// node a stable `Class:name` editorID (emitted as the first `id:` line of every file) and
// renders every ref/enum/float canonically, so a name-keyed, field-level diff is well defined.
//
// Design invariant (same-emitter): both sides MUST be produced by the SAME decompiler/decompose
// pass, so a purely-textual (formatting) difference can never occur — every reported delta is
// semantic. The adapters enforce this by always decompiling from binary internally.

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace havok::diff {

// A record's stable identity -> its YAML text. The key is chosen by the adapter (the `id:`
// line for behaviors, `project|clip` for animdata, a bone/character/relative-path elsewhere)
// and is NEVER a filename or a numeric node id.
using RecordSet = std::map<std::string, std::string>;

// Per-domain array-handling knobs. Defaults give a positional compare for every sequence and
// no volatile-field skipping — a domain opts in to the smarter strategies it needs.
struct DiffPolicy {
    // Field-name -> the element sub-fields (in priority order) that form each element's
    // SECONDARY key. Presence marks the array order-significant AND key-aligned: elements are
    // matched by the composite secondary key, and a change in the relative order of the common
    // keys is reported explicitly as `reordered` (never silently accepted). If the secondary
    // key collides within one side, the field falls back to a positional compare (noted).
    std::map<std::string, std::vector<std::string>> orderedKeyedArrays;

    // Like orderedKeyedArrays (elements matched by the composite secondary key, then field-diffed —
    // so a real element change is still caught AND the volatile-index skip still applies), but the
    // relative ORDER of elements carries no meaning, so a reorder is NEVER reported. Use for arrays
    // whose order the engine ignores (e.g. a node's variable `bindings`).
    std::map<std::string, std::vector<std::string>> unorderedKeyedArrays;

    // Field-names whose sequences are pure order-agnostic multisets (name lists): compared as
    // multisets, reporting only added/removed elements.
    std::set<std::string> multisetArrays;

    // Volatile-field skip rules (behavior domain). Each pair is {rawField, companionNameField}:
    // the raw index field is skipped when the companion name field is present on BOTH sides and
    // equal (indices renumber under mod merges; the name is authoritative). e.g.
    // {"eventId","event"}, {"variableIndex","variable"}.
    std::vector<std::pair<std::string, std::string>> volatileIndexFields;

    // Skip an `id:` map entry whose value is a bare integer (no `Class:` prefix) — the
    // encounter-order numeric fallback the decompiler assigns to unnamed inlined objects, which
    // is volatile between two compiles and never a semantic difference.
    bool skipNumericFallbackId = false;
};

// One reported field-level difference within a matched record.
struct FieldDiff {
    std::string path;   // dotted path, e.g. "generators.triggers[Attack|0.5].localTime"
    std::string a;      // A's value (or "" for added)
    std::string b;      // B's value (or "" for removed)
    // "changed" | "only-a" | "only-b" | "added" | "removed" | "reordered" | "type"
    std::string kind;
};

struct RecordDiff {
    std::string             key;
    std::vector<FieldDiff>  fields;
};

struct DiffResult {
    std::vector<std::string> onlyInA;   // record keys present only in A
    std::vector<std::string> onlyInB;   // record keys present only in B
    std::vector<RecordDiff>  changed;   // matched records whose fields differ
    std::vector<std::string> duplicateKeyWarnings;   // a genuine Class:name collision, lossy

    bool AnyDifference() const {
        return !onlyInA.empty() || !onlyInB.empty() || !changed.empty();
    }
};

// Match A vs B by key and field-diff each matched pair under `policy`.
DiffResult DiffRecordSets(const RecordSet& a, const RecordSet& b, const DiffPolicy& policy);

// Per-artifact roll-up for summary.yaml.
struct SummaryEntry {
    std::string artifact;   // relative artifact path / project name
    std::string domain;
    int compared  = 0;      // records matched (present on both sides)
    int onlyA     = 0;
    int onlyB     = 0;
    int differing = 0;      // matched records with >=1 field diff
};

// Write the delta-only folder for ONE artifact: one `<deltaDir>/<domain>/<artifactRel>/
// <sanitizedKey>.diff.yaml` per differing record, plus a `_missing.diff.yaml` listing
// only-in-A / only-in-B when non-empty. Identical records produce no file. Fills `entry`.
void WriteArtifactDelta(const std::filesystem::path& deltaDir,
                        const std::string&           domain,
                        const std::string&           artifactRel,
                        const DiffResult&            result,
                        SummaryEntry&                entry);

// Write `<deltaDir>/summary.yaml` from the accumulated per-artifact entries + the grand verdict.
void WriteSummary(const std::filesystem::path&     deltaDir,
                  const std::vector<SummaryEntry>& entries,
                  bool                             anyDifference);

// Filename-safe form of a record key (for the per-record delta file).
std::string SanitizeKey(const std::string& key);

} // namespace havok::diff
