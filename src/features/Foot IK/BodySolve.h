#pragma once

#include "features/Foot IK/Rig.h"

#include <platform/ik/Math.h>

#include <Animation/Ik/TwoJoints/hkaTwoJointsIkSolver.h>

#include <span>

/// @file
/// @brief The body and legs CB solves after vanilla's foot IK driver has run: a spine bends for the difference between
///        the fore and hind ends, then every leg reaches its ankle target with FABRIK; and the world-from-model tilt
///        toward the ground CB computes in vanilla's place.

namespace CB::havok {
    class hkbFootIkDriver;
    struct hkbFootIkGains;
    struct hkbGeneratorOutput;
    struct hkVector4;
}

namespace CB::feature::footik {

    /// @brief One leg's bend as vanilla's doFootPlacement asked for it (its two-joint setup, target included), deferred.
    struct LegRequest {
        const RigLeg*                                rigLeg = nullptr;
        CB::havok::hkaTwoJointsIkSolver::Setup       setup;
    };

    struct BodyInputs {
        /// @brief The character data's up in model space.
        platform::ik::Vec3 modelUpMS;
        /// @brief The graph's errorUpDownBias: 0 = an anchor goes where its most raised leg wants it, 1 = its most lowered.
        float              errorUpDownBias = 0.5f;
        bool               bendSpine       = true;
    };

    /// @brief Bends the spine for the difference between the fore and hind ends, then every leg toward its ankle
    ///        target, in `output`'s local pose.
    /// @pre   `output` is what vanilla's doFootIk returned: its pose dense and in local space, the world-from-model
    ///        scale restored. The requests' targets are in the model space vanilla solved in (the world-from-model's
    ///        scale moved into the bones' translations); this works in that space and writes back out of it.
    /// @note  The whole body's height is vanilla's (its vertical feedback sinks the world-from-model). Here the root
    ///        never moves: each anchor's legs ask for a height along model up (blended by errorUpDownBias as vanilla's
    ///        sink blends its legs' errors), and a spine that can bend (the horse's Spine1 .. Ribcage) brings its anchor
    ///        by the difference from the rigid anchor's ask (the pelvis, hanging straight off the root). A first design
    ///        moved the root to the MEAN of the legs' asks, scaled by the feedback gain; legs fold but cannot stretch,
    ///        and that gain is vanilla's per-frame convergence rate, not a fraction, so feet reached only short steps.
    /// @note  The ankles keep the model rotations vanilla gave them (the foot roll to the ground); every bone below the
    ///        ones moved here keeps its local transform and follows.
    void SolveBodyAndLegs(const Rig& rig, const CB::havok::hkaSkeleton& skeleton, std::span<const LegRequest> requests, const BodyInputs& inputs,
                          CB::havok::hkbGeneratorOutput& output);

    /// @brief CB's tilt of the world-from-model toward the ground: the slope runs between the mean ground point of the
    ///        legs on each of the two anchors (hind and fore), across the model's right axis; the turn toward it is
    ///        `gains.m_alignWorldFromModelGain` of the way, kept in the driver's m_rotation30 (vanilla's channel into the
    ///        character controller's pitch) and pre-multiplied onto the world-from-model.
    /// @return False when the rig does not have exactly two anchors or the legs' ground points are not valid; the
    ///         caller then leaves the tilt to vanilla.
    bool AlignToGround(const Rig& rig, CB::havok::hkbFootIkDriver& driver, const CB::havok::hkVector4& modelUpMS,
                       const CB::havok::hkVector4& modelRightMS, CB::havok::hkbGeneratorOutput& output, const CB::havok::hkbFootIkGains& gains);

}  // namespace CB::feature::footik
