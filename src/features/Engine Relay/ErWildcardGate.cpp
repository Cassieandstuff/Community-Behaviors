// Engine Relay — global wildcard gate (compile-time graph feature).
//
// Injects a BOOL graph variable (CB::ergate::kGateVar) into the graph and gates EVERY global wildcard
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

#include "FeatureRegistry.h"
#include "features/ERGate.h"   // CB::ergate::kGateVar (main hpp/, on the include path)

#include <havok/model/BehaviorData.h>
#include <havok/model/HavokEnums.h>   // ResolveEnum / TransitionFlags

#include <algorithm>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace CB::features {

    namespace {

        // Gate every GLOBAL wildcard transition in every state machine of `data` on `!gateVar`.
        // Returns the count gated. (Moved verbatim from Resolver.cpp GateWildcards.)
        //
        // Mechanism per wildcard: clear FLAG_DISABLE_CONDITION (256) so the condition is evaluated,
        // then set/AND an hkbExpressionCondition of "!<gateVar>". A live expression condition is
        // ANDed; a live STRING condition (rare in wildcards) can't be ANDed into an expression, so
        // the gate replaces it (block wins) with a warning; an ignored (disabled) condition is
        // dropped rather than resurrected.
        std::size_t GateWildcards(havok::model::BehaviorData& data, const std::string& gateVar,
                                  std::string_view graphKey, IFeatureLog& log,
                                  std::vector<std::string>& touched)
        {
            namespace en = havok::model::enums;
            const std::string gate = "!" + gateVar;   // fire only when unlocked
            std::size_t gated = 0;
            for (auto& [smName, sm] : data.stateMachines) {
                if (!sm.parsedWildcardTransitions) continue;
                const std::size_t gatedInSm = gated;
                for (auto& t : *sm.parsedWildcardTransitions) {
                    const long flags = en::ResolveEnum(t.flags, en::TransitionFlags());
                    if ((flags & 2048L) != 0) continue;             // FLAG_IS_LOCAL_WILDCARD — spare it
                    const bool wasDisabled = (flags & 256L) != 0;   // FLAG_DISABLE_CONDITION
                    t.flags = std::to_string(flags & ~256L);        // clear it (numeric form)

                    if (!wasDisabled && t.condition && !t.condition->empty()) {
                        t.condition = "(" + *t.condition + ") && (" + gate + ")";   // AND into live expr
                    } else if (!wasDisabled && t.conditionString && !t.conditionString->empty()) {
                        log.Warn(std::format(
                            "ER gate: wildcard in '{}'/{} has a live string condition — gate "
                            "replaces it (block-when-locked wins).", graphKey, smName));
                        t.conditionString.reset();
                        t.condition = gate;
                    } else {
                        t.conditionString.reset();   // drop any ignored/absent condition
                        t.condition = gate;          // gate only
                    }
                    ++gated;
                }
                if (gated != gatedInSm) touched.push_back(smName);   // wrote wildcards in this SM
            }
            return gated;
        }

        class ErWildcardGate final : public IGraphFeature {
        public:
            std::string_view Id() const override { return "engine-relay.wildcard-gate"; }

            bool AppliesTo(const FeatureContext&) const override { return true; }

            FeatureResult Apply(havok::model::BehaviorData& data, const FeatureContext& ctx) override
            {
                if (!data.graphData) return {};   // no variables table → nothing to inject/gate

                // 1. Inject the lock BOOL (default false = unlocked), append-only so it never shifts
                //    an existing variable index.
                auto& vars = data.graphData->variables;
                const bool present = std::any_of(vars.begin(), vars.end(),
                    [](const havok::model::VariableInfoDef& v) { return v.name == CB::ergate::kGateVar; });
                if (!present) {
                    havok::model::VariableInfoDef g;
                    g.name  = CB::ergate::kGateVar;
                    g.type  = "VARIABLE_TYPE_BOOL";
                    g.value = 0;   // unlocked
                    vars.push_back(std::move(g));
                }

                // 2. Gate every global wildcard on !it.
                FeatureResult r;
                r.applied   = true;
                r.mutations = GateWildcards(data, CB::ergate::kGateVar, ctx.graphKey, ctx.log, r.touched);
                return r;
            }
        };

        CB_REGISTER_FEATURE(ErWildcardGate);

    }  // namespace

}  // namespace CB::features
