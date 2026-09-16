#pragma once
#include "havok/model/BehaviorData.h"
#include "havok/model/yaml/UnitSource.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

// YamlBehaviorLoader (Tier B, SECONDARY) — parses an authored behavior YAML tree
// (root behavior.yaml + states/ clips/ generators/ transitions/ modifiers/ data/)
// into a BehaviorData. C++ mirror of HKBuild\src\BehaviorReader.cs.
//
// This is the ONLY translation unit in havok-core that depends on ryml
// (rapidyaml). It is deliberately isolated: every other TU compiles with zero
// external dependencies. ryml is wired the same way Engine Relay wires it —
// `find_package(ryml CONFIG REQUIRED)` + `target_link_libraries(... ryml::ryml)`
// in the consuming CMake; the .cpp pulls ryml through <RymlInclude.h> (the shared
// shim in include/external/ that works around a c4core v0.5.0 C++20 attribute bug).
// It is verified in the Visual Studio build (where ryml is on the include path),
// not in the standalone havok-core test build.

namespace havok::schema { class SchemaRegistry; }   // fwd — the merge classifier reads field `merge:` tags

namespace havok::model {

class YamlBehaviorLoader {
public:
    // Load the behavior rooted at `directory` (the <name>.hkx folder containing
    // behavior.yaml). Throws std::runtime_error on missing/invalid root.
    static BehaviorData Load(const std::string& directory);

    // Record-level merge: load `dirs` in priority order (base first) into one
    // BehaviorData — a later dir's same-named node overrides an earlier one, a new
    // node is added. Only the first (base) dir must contain behavior.yaml. This is
    // how BR overlays mod .hky deltas onto the vanilla base at runtime.
    static BehaviorData LoadMerged(const std::vector<std::string>& dirs);

    // Same record-level merge, but over abstract unit sources instead of on-disk
    // directories — the string overloads above wrap each path in a DiskUnitSource
    // and forward here. Lets a caller (e.g. BR's in-memory .hky zip backend) supply
    // units that never touch the filesystem. `sources` are in priority order (base
    // first); only the base must contain behavior.yaml.
    static BehaviorData LoadMerged(const std::vector<std::shared_ptr<const IUnitSource>>& sources);

    // Opt-in sink for NON-FATAL merge notices (today: same-slot collisions on a
    // positional array, where load-order last-writer keeps the highest-priority mod's
    // slot and drops another mod's differing edit). havok-core has no logger by design
    // — the consumer supplies one (BR routes to its plugin log; the CLI to stderr).
    // Unset = quiet; the merged result is identical either way. Set once before any
    // LoadMerged; not synchronized against concurrent LoadMerged calls.
    static void SetDiagnosticSink(std::function<void(const std::string&)> sink);

    // Point the load-order merge at the Havok/ class schema, so an array's merge policy
    // (compose / guarded) is read from that field's `merge:` tag in the schema descriptor
    // — the schema is the single source of truth, shared with the converter's merge, so the
    // two can't drift. Same ambient contract as SetDiagnosticSink: set once before any
    // LoadMerged, the pointed-at registry must outlive those calls, not synchronized against
    // concurrent LoadMerged. nullptr (default) = no schema → the built-in compose/guarded
    // name sets are the fallback (behaviour identical to pre-schema-tag BR-39).
    //
    // `strict` (gate-only): when true AND the schema is wired, the built-in compose/guarded name
    // fallback is DISABLED — only a field's own `merge:` tag can classify it. A real composable
    // array whose tag is missing then unions instead of composing, so its output diverges from the
    // ground truth and the byte-diff gate catches it — proving the merge is genuinely tag-driven,
    // not coasting on the fallback. Production leaves it false (the safe fallback stays live).
    static void SetSchemaRegistry(const havok::schema::SchemaRegistry* reg, bool strict = false);

    // One node identity that appears in >1 merge layer (a cross-layer overlap). Identity is
    // (class, id-else-name) — folder-agnostic, matching the loader's Stage-2 dispatch.
    struct NodeContribution {
        std::string              cls;      // node class (peekClass)
        std::string              key;      // id-else-name (keyOf)
        std::vector<std::size_t> layers;   // indices into `sources`, ascending (load order)
    };

    // Node-granular contributor scan over the SAME layer sources LoadMerged consumes:
    // every node identity (class, id-else-name) that more than one layer carries, with the
    // layer indices that carry it. Built with the EXACT whole-unit scan + (class,key) grouping
    // the merge overlays by (scanSourceSection over the whole unit) — so an overlap reported
    // here is precisely a node the merge would combine, never an approximation that can drift
    // from it. Read-only (no BehaviorData built, no bash-merge run): it just groups the raw node
    // files. Feeds BR's node-granular load-order conflict report (which mods edit the same node)
    // — the resolver maps each layer index back to its bundle to classify override vs clash.
    static std::vector<NodeContribution>
        NodeContributions(const std::vector<std::shared_ptr<const IUnitSource>>& sources);
};

} // namespace havok::model
