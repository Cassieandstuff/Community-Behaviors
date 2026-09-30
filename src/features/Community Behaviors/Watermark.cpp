// Community Behaviors — compile-time watermark (graph feature).

#include "FeatureRegistry.h"
#include "core/resolve/Watermark.h"   // kWatermarkVar / kWatermarkValue (header-only constants)

#include <format>
#include <string>
#include <string_view>

namespace CB::feature {

    namespace {

        /// @brief Declares the INT32 `BR_Watermark` variable stamped with the build fingerprint, so a probe can tell
        ///        CB-served bytes from vanilla and a stale on-disk cache from a current one.
        /// @note  A stop-gap cache-invalidation signature until real fingerprinting lands. A graph that already declares
        ///        the name keeps its own declaration.
        class Watermark final : public IGraphFeature {
        public:
            std::string_view Id() const override { return "cb.watermark"; }

            bool AppliesTo(const FeatureContext&) const override { return true; }

            FeatureResult Apply(CB::core::decompile::GraphEdit& graph, const FeatureContext& ctx) override
            {
                if (!graph.GraphData()) return {};
                std::string err;
                const auto added = graph.AddVariable(CB::core::resolve::kWatermarkVar, "VARIABLE_TYPE_INT32",
                                                     CB::core::resolve::kWatermarkValue, err);
                if (added == CB::core::decompile::GraphEdit::Add::Failed) {
                    ctx.log.Warn(std::format("watermark: {}: {}", ctx.graphKey, err));
                    return {};
                }
                FeatureResult r;
                r.applied   = added == CB::core::decompile::GraphEdit::Add::Added;
                r.mutations = r.applied ? 1 : 0;
                return r;
            }
        };

        CB_REGISTER_FEATURE(Watermark);

    }  // namespace

}  // namespace CB::feature
