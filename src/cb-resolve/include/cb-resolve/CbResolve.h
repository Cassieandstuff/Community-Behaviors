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
#include "havok/model/yaml/YamlBehaviorLoader.h" // LoadMerged — the load-order merge
#include "havok/model/BehaviorData.h"           // the resolved graph model
#include "havok-schema/HavokSchema.h"           // SchemaRegistry — merge classifier

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
    using SchemaRegistry = havok::schema::SchemaRegistry;

}  // namespace cb::resolve
