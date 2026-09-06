// Animation Relay — animationdata derive (compile-time contributor feature).
//
// The adsf (animationdatasinglefile.txt) clip cache is derivable straight from the graph: each
// hkbClipGenerator carries name + animationName + speed + crop + own-triggers, which is exactly a
// DeriveClipInput. This feature reads them out of the graph currently being compiled and pushes one
// per clip into the host's animdata sink — it does NOT mutate the graph (mutations = 0; it's a
// side-artifact emitter, the first of that flavor). The host keys contributions by graphKey and runs
// the project-level finalize (annotation-trigger merge, animIndex resolution against the roster,
// high-band alloc, collated emit) once every graph of a project has contributed.
//
// This is the runtime clip-derivation (DeriveClipInputsFromBehavior) recast as a feature so it rides
// the graph-compile pass instead of a separate post-hoc sweep — the same transform, one caller. The
// roster resolution stays OUT of here on purpose: baking a resolved animIndex into the clip's
// animationBindingIndex would byte-diverge the graph from vanilla (the binding is -1 in vanilla and
// the engine resolves it at runtime), so the animIndex is a finalizer concern, resolved via the
// roster service without touching the graph.
//
// Inert unless the host both lists this feature's id in the run-list AND supplies ctx.animData — with
// no sink there is nothing to contribute to, so AppliesTo gates on it. Pure-Apply: depends only on
// havok-core (BehaviorData + the clip-derive) and the framework's sink interface.

#include "FeatureRegistry.h"

#include <havok/model/BehaviorData.h>
#include <havok/sct/AnimDataFromBehavior.h>   // DeriveClipInputsFromBehavior
#include <havok/anim/AnimDataDeriver.h>        // havok::animdata::DeriveClipInput (the sink payload)

#include <format>
#include <string_view>

namespace CB::features {

    // Reads every hkbClipGenerator in the graph as a DeriveClipInput and pushes it to the sink.
    class AdsfDeriveFeature final : public IGraphFeature {
    public:
        std::string_view Id() const override { return "animation-relay.adsf-derive"; }

        // Only meaningful when the host is collecting animdata this compile.
        bool AppliesTo(const FeatureContext& ctx) const override { return ctx.animData != nullptr; }

        FeatureResult Apply(havok::model::BehaviorData& data, const FeatureContext& ctx) override {
            FeatureResult r;
            if (!ctx.animData) return r;   // AppliesTo already guards this; belt-and-suspenders

            const auto inputs = havok::sct::DeriveClipInputsFromBehavior(data);
            for (const auto& in : inputs)
                ctx.animData->EmitClip(ctx.graphKey, in);

            r.applied   = true;
            r.mutations = 0;   // side-artifact emitter — the graph is untouched
            if (!inputs.empty())
                ctx.log.Info(std::format("adsf-derive: contributed {} clip input(s) from '{}'.",
                                         inputs.size(), ctx.graphKey));
            return r;
        }
    };

    CB_REGISTER_FEATURE(AdsfDeriveFeature);

}  // namespace CB::features
