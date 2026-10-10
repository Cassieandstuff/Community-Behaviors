// Engine Relay — global wildcard gate (compile-time graph feature).
//
// Injects a BOOL graph variable (CB::feature::ergate::kGateVar) into the graph and gates EVERY global wildcard
// transition on `!kGateVar`, so Engine Relay can pin the actor by flipping the bit (see ERGate.h
// for the full contract). Default bit = false → byte-behaviour identical to vanilla (inert). LOCAL
// wildcards (FLAG_IS_LOCAL_WILDCARD=2048) are deliberately spared — the escape hatch that keeps a
// locked, framework-owned actor steerable while every global wildcard is frozen.
//
// Extracted verbatim from Resolver.cpp's inline GateWildcards + lock-var injection (feature
// framework, cut #1). Enablement (settings [ERGate] bEnable) is the HOST's job: it only puts this
// feature's id in the run-list when enabled, so AppliesTo here is unconditional and Apply guards on
// graphData. Pure-Apply rule: depends only on havok-core + the feature framework (ERGate.h is a
// header-only CommunityBehaviors constant, no runtime deps).

#include <feature-sdk/FeatureRegistry.h>
#include "features/ERGate.h"   // CB::feature::ergate::kGateVar (main hpp/, on the include path)

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace CB::feature {

    namespace {

        namespace io = CB::core::codec::io;

        constexpr std::int64_t kLocalWildcard    = 2048;   ///< FLAG_IS_LOCAL_WILDCARD — spared
        constexpr std::int64_t kDisableCondition = 256;    ///< FLAG_DISABLE_CONDITION

        std::int16_t readI16(const io::FieldValue& v) {
            std::int16_t x = 0;
            std::memcpy(&x, v.raw.data(), std::min<std::size_t>(v.raw.size(), 2));
            return x;
        }
        std::string readStr(io::SchemaObject& so, const char* field) { return so.FieldRef(field).str; }

        /// @brief Gate every GLOBAL wildcard transition of every state machine on `!gateVar`; returns the count.
        /// @note  Per wildcard: clear FLAG_DISABLE_CONDITION so the condition is evaluated, and give it an
        ///        hkbExpressionCondition of "!<gateVar>" — ANDed into a live expression; a live STRING condition
        ///        can't be ANDed into an expression, so the gate replaces it (block wins) with a warning; an ignored
        ///        (disabled) condition is dropped, not resurrected. The condition is always a NEW object: vanilla
        ///        shares condition objects between transitions, and editing one in place would gate (or double-wrap)
        ///        every sharer.
        std::size_t GateWildcards(CB::core::decompile::GraphEdit& graph, const std::string& gateVar,
                                  std::string_view graphKey, IFeatureLog& log, std::vector<std::string>& touched)
        {
            const std::string gate = "!" + gateVar;
            std::size_t gated = 0;
            graph.ForEach("hkbStateMachine", [&](io::SchemaObject& sm) {
                auto* array = dynamic_cast<io::SchemaObject*>(sm.FieldRef("wildcardTransitions").obj.get());
                if (!array) return;
                const std::size_t before = gated;
                for (const auto& e : array->FieldRef("transitions").objs) {
                    auto* t = dynamic_cast<io::SchemaObject*>(e.get());
                    if (!t) continue;
                    io::FieldValue& flagsField = t->FieldRef("flags");
                    const std::int64_t flags = readI16(flagsField);
                    if (flags & kLocalWildcard) continue;
                    const bool wasDisabled = (flags & kDisableCondition) != 0;
                    const std::int16_t cleared = static_cast<std::int16_t>(flags & ~kDisableCondition);
                    flagsField.raw.assign(reinterpret_cast<const std::uint8_t*>(&cleared), reinterpret_cast<const std::uint8_t*>(&cleared) + 2);

                    io::FieldValue& condField = t->FieldRef("condition");
                    auto* cond = dynamic_cast<io::SchemaObject*>(condField.obj.get());
                    std::string expr = gate;
                    if (!wasDisabled && cond && std::string_view(cond->ClassName()) == "hkbExpressionCondition" &&
                        !readStr(*cond, "expression").empty()) {
                        expr = "(" + readStr(*cond, "expression") + ") && (" + gate + ")";
                    } else if (!wasDisabled && cond && std::string_view(cond->ClassName()) == "hkbStringCondition" &&
                               !readStr(*cond, "conditionString").empty()) {
                        log.Warn(std::format("ER gate: wildcard in '{}'/{} has a live string condition — gate replaces it "
                                             "(block-when-locked wins).", graphKey, readStr(sm, "name")));
                    }
                    auto gateCond = graph.Make("hkbExpressionCondition");
                    if (!gateCond) { log.Warn("ER gate: hkbExpressionCondition missing from the schema"); return; }
                    gateCond->FieldRef("expression").str = expr;
                    condField.obj = gateCond;
                    ++gated;
                }
                if (gated != before) touched.push_back(graph.IdOf(&sm));
            });
            return gated;
        }

        class ErWildcardGate final : public IGraphFeature {
        public:
            std::string_view Id() const override { return "engine-relay.wildcard-gate"; }

            bool AppliesTo(const FeatureContext&) const override { return true; }

            FeatureResult Apply(CB::core::decompile::GraphEdit& graph, const FeatureContext& ctx) override
            {
                if (!graph.GraphData()) return {};

                std::string err;
                if (graph.AddVariable(CB::feature::ergate::kGateVar, "VARIABLE_TYPE_BOOL", 0, err) ==
                    CB::core::decompile::GraphEdit::Add::Failed) {
                    ctx.log.Warn(std::format("ER gate: {}: {}", ctx.graphKey, err));
                    return {};
                }

                FeatureResult r;
                r.applied   = true;
                r.mutations = GateWildcards(graph, CB::feature::ergate::kGateVar, ctx.graphKey, ctx.log, r.touched);
                return r;
            }
        };

        CB_REGISTER_FEATURE(ErWildcardGate);

    }  // namespace

}  // namespace CB::feature
