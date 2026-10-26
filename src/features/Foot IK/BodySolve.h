#pragma once

#include "features/Foot IK/Rig.h"

#include <Animation/Ik/TwoJoints/hkaTwoJointsIkSolver.h>

#include <span>

/// @file
/// @brief The body and legs CB solves after vanilla's foot IK driver has run: the anchors give toward where the legs
///        want them (the root carries the rigid anchors, the spines bend to the others), then every leg reaches its
///        ankle target with FABRIK; and the world-from-model tilt toward the ground CB computes in vanilla's place.

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

    /// @brief Moves the body and bends the legs in `output`'s local pose, toward the requests' ankle targets.
    /// @param stiffness How far the anchors give toward where the legs want them, 0..1.
    /// @pre   `output` is what vanilla's doFootIk returned: its pose dense and in local space, the world-from-model
    ///        scale restored. The requests' targets are in the model space vanilla solved in (the world-from-model's
    ///        scale moved into the bones' translations); this works in that space and writes back out of it.
    /// @note  The ankles keep the model rotations vanilla gave them (the foot roll to the ground); every bone below the
    ///        ones moved here keeps its local transform and follows.
    void SolveBodyAndLegs(const Rig& rig, const CB::havok::hkaSkeleton& skeleton, std::span<const LegRequest> requests, float stiffness,
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
