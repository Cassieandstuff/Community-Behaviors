#pragma once

// Compiler-feature framework — see CLAUDE.md "Compiler features — CB::feature (SOP)".
//
// A feature is a self-registered COMPILE-TIME GRAPH MANIPULATION the compile pipeline applies to the merged graph
// (the ER wildcard gate, the BDI symbol injection, the watermark, …).
//
// PURE-Apply rule (directive): a feature body depends ONLY on the core graph API (decompile::GraphEdit over the
// merged SchemaObject graph) + these context interfaces — it NEVER #includes or uses SKSE/CommonLib/runtime. (The
// plugin target force-includes the plugin PCH, so a feature TU *compiles* with CommonLib in scope; that is a build
// detail. The feature's SOURCE keeps its logical dependency to the core + this header, so it stays unit-testable
// without a plugin and offline-reproducible.)

#include <decompile/BehaviorDecompile.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Forward-declared so this header stays light + pure: a feature body that emits animdata includes
// the full <havok/anim/AnimDataDeriver.h>; the interface only needs the incomplete type by const-ref.
namespace CB::core::animdata { struct DeriveClipInput; }

namespace CB::feature {

    // Logging sink. The host (BR runtime) forwards to the plugin logger; an offline harness can
    // supply a no-op or a capturing sink. The feature pre-formats messages (std::format) and passes
    // the finished string, so this stays free of any format-library dependency.
    struct IFeatureLog {
        virtual void Info(std::string_view) = 0;
        virtual void Warn(std::string_view) = 0;
        virtual ~IFeatureLog() = default;
    };

    // The character animationNames roster for the actor the current graph belongs to — the currency
    // both clip legs share (animIndex = roster.indexOf(animationName)). A feature that binds clips to
    // roster indices (Animation Relay's clip-index-binding) queries it; IndexOf returns -1 for a name
    // not in the roster. Read-only, name-keyed — the host owns the merged roster.
    struct IRosterService {
        virtual int         IndexOf(std::string_view animationName) const = 0;
        virtual std::size_t Size() const = 0;
        virtual ~IRosterService() = default;
    };

    // The CONTRIBUTION hook for animationdata: a feature that ADDS animations (a DeriveClipInput that
    // doesn't correspond to a graph clip the compiler already sees) pushes it here, into the same sink
    // the host's FIRST-CLASS adsf-derive stage fills from each graph's own hkbClipGenerators. Deriving
    // the base adsf is compiler output, not a feature — it is NOT done through this interface; this is
    // purely how a feature feeds EXTRA clips into that first-class collector. Pushing is per-graph — the
    // host keys contributions by graphKey and runs the project-level finalize (annotation-trigger merge,
    // animIndex resolution against the roster, high-band alloc, collated emit) once every graph of a
    // project has contributed.
    struct IAnimDataSink {
        virtual void EmitClip(std::string_view graphKey, const CB::core::animdata::DeriveClipInput&) = 0;
        virtual ~IAnimDataSink() = default;
    };

    /// @brief One compile-time symbol declaration (a BDI config entry): an event, or a WORD variable with its raw
    ///        4-byte value (a REAL's float bits).
    struct SymbolDecl {
        std::string  name;
        bool         isEvent = false;
        std::string  varType;          ///< VARIABLE_TYPE_* (variables only)
        std::int32_t value = 0;        ///< raw word (variables only)
    };

    /// @brief The declarations scoped to a graph (BDI's projectPath match) — loaded by the host, read by the feature.
    struct ISymbolDecls {
        virtual std::vector<SymbolDecl> For(std::string_view graphKey) const = 0;
        virtual ~ISymbolDecls() = default;
    };

    // Per-graph inputs a feature may read. Grows as features need it (settings, id-minting, …) — each
    // a narrow interface so the pure-Apply rule keeps holding. The service pointers are NULLABLE: the
    // host supplies only what a given compile provides (offline unit tests and mutation-only features
    // like the wildcard gate leave them null), so a feature that needs one gates on it (in AppliesTo
    // or a null check) rather than assuming presence.
    struct FeatureContext {
        std::string_view      graphKey;              // the graph currently being compiled (serve key)
        IFeatureLog&          log;
        const IRosterService* roster   = nullptr;    // the actor's merged animationNames roster, or null
        IAnimDataSink*        animData = nullptr;    // where a derive feature pushes clips, or null
        const ISymbolDecls*   symbols  = nullptr;    // BDI declarations, or null
    };

    // What a feature did — the host logs a summary from this.
    struct FeatureResult {
        bool        applied   = false;
        std::size_t mutations = 0;

        // Node ids (FormIDs — GraphEdit::IdOf) this feature WROTE.
        // Self-reported: features are declarative about what they touch (the Community Shaders idiom —
        // authors declare, the host surfaces — rather than an auto-tracking membrane). The host
        // aggregates these across the run and warns when two features write the same key: run order
        // (below) still picks a deterministic winner (last writer), but the clobber is now visible
        // instead of silent. A non-mutating feature (side-artifact emitter) leaves this empty.
        std::vector<std::string> touched;
    };

    // One graph transform. Apply is called once per graph.
    class IGraphFeature {
    public:
        virtual ~IGraphFeature() = default;

        // Stable, namespaced id — e.g. "engine-relay.wildcard-gate". Matches the run-list.
        virtual std::string_view Id() const = 0;

        // Coarse gate: should this feature run for this graph (id/settings/targeting)? Apply still
        // guards on data validity.
        virtual bool AppliesTo(const FeatureContext&) const = 0;

        // Edit the merged graph. Pure: touches only `graph`, core types, and ctx.
        virtual FeatureResult Apply(CB::core::decompile::GraphEdit& graph, const FeatureContext&) = 0;

        // Relative run-order constraints (RETURNED BY VALUE-STABLE SPAN over storage the feature
        // owns — return a static array or empty). The host topo-sorts the enabled set from these, so a
        // feature composes into the pipeline without anyone editing a central ordered list:
        //   RunsAfter()  — ids that must run BEFORE this one (e.g. bind clips AFTER TC adds structure).
        //   RunsBefore() — ids that must run AFTER this one (e.g. gate wildcards BEFORE nothing yet).
        // Constraints referencing a feature that isn't in the enabled set are trivially satisfied
        // (ignored). A cycle is a compile-time fault the host reports; features are non-commutative, so
        // declaring the real dependencies is how correctness is kept. Default: no constraints.
        virtual std::span<const std::string_view> RunsAfter()  const { return {}; }
        virtual std::span<const std::string_view> RunsBefore() const { return {}; }
    };

}  // namespace CB::feature
