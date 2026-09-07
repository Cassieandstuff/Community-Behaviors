#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// cb-resolve — Community Behaviors' load-order resolution + .hky document, as a
// MINIMAL public contract for any tool (Scene Editor, converter, …) so every
// consumer resolves a load order IDENTICALLY to the CB compiler. Single owner, no
// drift.
//
// This is a curated surface, not a re-export of the whole engine: it exposes only
// reading + writing .hky bundles + resolving/merging a load order + the resolved
// model + the schema needed to interpret it. It is deliberately **havok-core-free**
// — no HKX decode, no typed hkb*/hka* classes, no compile/emit. Packing a .hky is
// just zipping an already-authored YAML tree (vendored miniz), NOT compiling — so
// the write side stays inside the havok-core-free contract. HKX asset decode is a
// separate (legacy) concern the editor is moving onto CB's schema pipeline.
//
// Consumers include ONLY this header and use the cb::resolve:: names; the
// underlying havok::model / havok::schema types are internal and may move.
// ─────────────────────────────────────────────────────────────────────────────

#include "havok/model/yaml/HkyArchive.h"        // read + pack a .hky bundle
#include "havok/model/yaml/UnitSource.h"        // IUnitSource — a unit's file view
#include "havok/model/yaml/YamlBehaviorLoader.h" // LoadMerged + NodeContributions
#include "havok/model/BehaviorData.h"           // the resolved graph model + typed *Def types
#include "havok/model/ProjectData.h"            // ProjectSpec — the project unit
#include "havok/model/defs/CharacterDefs.h"     // CharacterData
#include "havok/model/yaml/CharacterYamlLoader.h" // load/merge a character unit
#include "havok/model/BashMerge.h"              // the shared field/array merge primitive
#include "havok/anim/AnimationData.h"           // animationdata model + emit (havok-core-free)
#include "havok/anim/AnimDataDeriver.h"         // derive an animdata clip list from the graph
#include "havok/sct/AnimDataFromBehavior.h"     // DeriveClipInputs straight from a BehaviorData
#include "havok-schema/HavokSchema.h"           // SchemaRegistry + SchemaVersion / CheckSchemaCompat

namespace cb::resolve {

    // Read/write side: a packed .hky bundle (base master or a mod delta).
    //   • Read:  `Archive::LoadFromFile(path, err)` decompresses; source()/units()
    //            expose its units as IUnitSource views.
    //   • Write: `Archive::PackDirectory(dir, outHkyPath, err)` zips an authored
    //            YAML tree into a single-file .hky — the exact layout LoadFromFile
    //            reads back, so pack→load round-trips. This is how a tool exports a
    //            bundle identically to CB's build-time packer. Static, no instance.
    using Archive    = havok::model::HkyArchive;
    using UnitKind   = havok::model::HkyArchive::UnitKind;
    using UnitSource = havok::model::IUnitSource;

    // Resolve side: the load-order merge. `LoadOrder::LoadMerged({base, delta, …})`
    // (dirs or IUnitSource layers, ascending priority) → a resolved ResolvedGraph.
    // Point it at a SchemaRegistry (SetSchemaRegistry) so per-field `merge:` policy
    // drives the compose — the same classifier the compiler uses.
    using LoadOrder     = havok::model::YamlBehaviorLoader;
    using ResolvedGraph = havok::model::BehaviorData;

    // The Havok class schema (loaded from the Havok/ tree) that the merge consults.
    // Find the shipped tree via the CB_RESOLVE_SCHEMA_DIR CMake var: reg.LoadDir(dir).
    using SchemaRegistry = havok::schema::SchemaRegistry;

    // The base master (vanilla Skyrim.hky) is the layer-0 input LoadMerged merges mod
    // deltas onto — resolution is "deltas over the base." It is NOT shipped with this
    // package: it's a decompiled representation of the user's own vanilla game data
    // (produced only by the full CB build, from $SKYRIM_DATASOURCE), so a tool OPENS
    // it from the user's install at runtime — via MO2's VFS, exactly as the Scene
    // Editor does — and passes it as the first layer. Resolve the game Data/ root
    // yourself (MO2 / registry); this is the CB-mod-relative path under it. See
    // docs/consuming-cb-resolve.md.
    inline constexpr const char* kBaseMasterDataPath =
        "community_behaviors/plugins/Skyrim.hky";

    // ── Curated model + import surface (stable names) ────────────────────────────
    // A resolved project isn't only the behavior graph — a tool imports/edits the
    // project and character units too. These are havok-core-free and load the same
    // way the compiler does.
    using ProjectSpec      = havok::model::ProjectSpec;          // project-level spec
    using CharacterData    = havok::model::CharacterData;        // a resolved character unit
    using CharacterLoader  = havok::model::CharacterYamlLoader;  // Load / LoadMerged, mirrors LoadOrder

    // Provenance: which load-order layers touch the same node — the editor's
    // conflict/inspector feed. `LoadOrder::NodeContributions(sources)` → these,
    // built with the EXACT grouping the merge overlays by (never an approximation).
    using NodeContribution = havok::model::YamlBehaviorLoader::NodeContribution;

    // Schema-version gating: the editor↔compiler contract stamp. Parse the tree's
    // stamp (SchemaRegistry::SchemaVersionString) and gate an authored .hky against
    // the current tree exactly as the compiler does.
    using SchemaVersion = havok::schema::SchemaVersion;
    using SchemaCompat  = havok::schema::SchemaCompat;
    using havok::schema::CheckSchemaCompat;

    // Animation data: derive an animationdatasinglefile clip list from a resolved
    // graph (havok-core-free — distinct from the retired havok::anim path).
    using havok::sct::DeriveClipInputsFromBehavior;

    // ── Broader engine namespaces (fuller model, less frozen) ────────────────────
    // The top-level names above are the recommended, stable contract. These aliases
    // hoist the fuller model wholesale — the ~30 typed node `*Def` types
    // (ResolvedGraph's members), the animdata model, and the merge primitives — so a
    // tool can spell any of them. They track the engine and may move; prefer the
    // curated names where one exists.
    namespace model    = havok::model;     // BehaviorData's typed *Def node model + loaders
    namespace animdata = havok::animdata;  // ClipGenerator / Project / SingleFile / DeriveClipList
    namespace merge    = havok::merge;     // BashMerge: ParamMerge, PatchLayer, MergeReport, …
    namespace schema   = havok::schema;    // ClassSchema, Field, FieldKind, ScalarWidth, …

}  // namespace cb::resolve
