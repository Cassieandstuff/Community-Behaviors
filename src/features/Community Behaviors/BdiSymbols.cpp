// Community Behaviors — compile-time Behavior Data Injector symbols (graph feature).

#include <feature-sdk/FeatureRegistry.h>

#include <format>
#include <string>
#include <string_view>

namespace CB::feature {

    namespace {

        /// @brief Declares every BDI event / variable scoped to this graph (the host loads the configs — ctx.symbols).
        /// @note  BDI appends symbols at runtime because nothing else could recompile a symbol table; CB does, so the
        ///        injection becomes part of the compile and the symbols are simply IN the bytes. A name the graph already
        ///        declares keeps its declaration (its index is already wired).
        class BdiSymbols final : public IGraphFeature {
        public:
            std::string_view Id() const override { return "cb.bdi-symbols"; }

            bool AppliesTo(const FeatureContext& ctx) const override { return ctx.symbols != nullptr; }

            FeatureResult Apply(CB::core::decompile::GraphEdit& graph, const FeatureContext& ctx) override
            {
                if (!graph.GraphData()) return {};
                FeatureResult r;
                std::string err;
                for (const SymbolDecl& d : ctx.symbols->For(ctx.graphKey)) {
                    const auto added = d.isEvent ? graph.AddEvent(d.name, err)
                                                 : graph.AddVariable(d.name, d.varType, d.value, err);
                    if (added == CB::core::decompile::GraphEdit::Add::Failed)
                        ctx.log.Warn(std::format("BDI: {}: '{}': {}", ctx.graphKey, d.name, err));
                    else if (added == CB::core::decompile::GraphEdit::Add::Added)
                        ++r.mutations;
                }
                r.applied = r.mutations > 0;
                return r;
            }
        };

        CB_REGISTER_FEATURE(BdiSymbols);

    }  // namespace

}  // namespace CB::feature
