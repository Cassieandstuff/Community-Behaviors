#pragma once

#include "IGraphFeature.h"

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace CB::features {

    // Process-wide registry of graph features. Features self-register at static-init via
    // AutoRegister; the HOST decides which run (and in what order) by passing an ordered id list to
    // Run() — run order is DATA, never static-init order (features compose on one graph and are NOT
    // commutative: e.g. TC adds structure → Animation Relay binds the clips it now sees → ER gates
    // the settled wildcards).
    class FeatureRegistry {
    public:
        static FeatureRegistry& Instance();

        void Register(std::unique_ptr<IGraphFeature> feature);

        // Topo-sort `enabledIds` by the features' declared RunsAfter/RunsBefore constraints and return
        // a concrete run order. The HOST decides WHICH features are enabled (settings/manifest); this
        // decides in WHAT ORDER, from data on the features themselves — so a feature slots into the
        // pipeline by declaring its dependencies, not by anyone editing a central list. Ids not
        // registered are dropped; constraints referencing an id outside `enabledIds` are ignored
        // (trivially satisfied). Ties break by the input order of `enabledIds` (stable, deterministic).
        // A dependency CYCLE is reported via `log` (Warn) and the cycle's members are appended in input
        // order so the compile still proceeds (last-writer-wins stays deterministic; the fault is loud).
        std::vector<std::string>
        ResolveRunOrder(const std::vector<std::string>& enabledIds, IFeatureLog& log) const;

        // For each id in `orderedIds` (already ordered — see ResolveRunOrder): if a feature with that
        // id is registered AND AppliesTo(ctx), Apply it to `data`. After the run, any node key written
        // by two or more features is reported via `ctx.log` (last writer wins; the clobber is made
        // visible). Returns {id, result} per feature that ran, in run order.
        std::vector<std::pair<std::string, FeatureResult>>
        Run(const std::vector<std::string>& orderedIds,
            havok::model::BehaviorData& data,
            const FeatureContext& ctx) const;

        FeatureRegistry(const FeatureRegistry&)            = delete;
        FeatureRegistry& operator=(const FeatureRegistry&) = delete;

    private:
        FeatureRegistry() = default;
        std::map<std::string, std::unique_ptr<IGraphFeature>, std::less<>> m_byId;
    };

    // Static-init self-registration helper. Prefer the CB_REGISTER_FEATURE macro below; this is the
    // raw form (`static AutoRegister<MyFeature> s_reg;`) for anyone who wants it.
    template <class F>
    struct AutoRegister {
        AutoRegister() { FeatureRegistry::Instance().Register(std::make_unique<F>()); }
    };

}  // namespace CB::features

// CB_REGISTER_FEATURE(Type) — the documented one-liner to register a feature. Use it at namespace
// scope where the feature type is visible, right after the class:
//
//     class MyFeature final : public CB::features::IGraphFeature { … };
//     CB_REGISTER_FEATURE(MyFeature);
//
// Expands to a file-local `static AutoRegister<Type>` whose ctor registers the feature at static
// init. `Type` may be qualified; the instance name is uniquified by line so multiple registrations
// in one TU don't collide.
#define BR_DETAIL_FEATURE_CONCAT2(a, b) a##b
#define BR_DETAIL_FEATURE_CONCAT(a, b)  BR_DETAIL_FEATURE_CONCAT2(a, b)
#define CB_REGISTER_FEATURE(Type)                              \
    static ::CB::features::AutoRegister<Type>       \
        BR_DETAIL_FEATURE_CONCAT(_br_feature_reg_, __LINE__)
