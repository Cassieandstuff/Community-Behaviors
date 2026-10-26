#pragma once

#include <platform/ik/Math.h>

#include <cstddef>
#include <span>
#include <vector>

/// @file
/// @brief FABRIK (Aristidou & Lasenby 2011): bone chains reaching targets by alternating a pass from the tip (pinned at
///        the target) back to the base and a pass from the base (pinned) out to the tip, over joint POSITIONS; the
///        bones' rotations are recovered afterwards. Single chains, hinge joints, and a body whose limbs pull the
///        anchors they hang from (multiple end effectors with sub-bases).
/// @note  Written from the paper. Yannick van Diermeen's MIT-licensed Python solver (2020) was read as a reference for
///        the reaching passes; none of its code is copied. The hinge rule, the rotation recovery and the body solve
///        are CB's.
/// @note  Model space throughout, in the caller's units. Plain math only (platform/ik/Math.h): no game types.

namespace CB::platform::ik {

    /// @brief A joint that bends about one axis.
    /// @note  The bend is the signed angle from straight (the child continuing its parent's direction) about `axis`.
    ///        A hinge taken from a bent pose has that bend positive, so [0, maxBend] lets the joint bend further or
    ///        straighten but never flip through straight.
    struct Hinge {
        /// @brief Unit axis in the source pose, carried by the chain's frame; zero = a BALL joint (any direction,
        ///        folding at most maxBend).
        Vec3  axis;
        float minBend = 0.0f;   ///< radians
        float maxBend = 0.0f;   ///< radians
    };

    /// @brief The most a joint folds from straight when its data gives no limit.
    inline constexpr float kDefaultMaxBend = 175.0f * 3.14159265f / 180.0f;

    struct Chain {
        /// @brief Parent first; joints[0] is the base, back() the end effector. Solving moves them.
        std::vector<Vec3>  joints;
        /// @brief The joints as the chain was made from; hinge axes and recovered rotations are relative to them.
        std::vector<Vec3>  source;
        /// @brief lengths[i] is the bone from joints[i] to joints[i + 1], kept by every pass.
        std::vector<float> lengths;
        /// @brief hinges[i] constrains joints[i + 1] (one per interior joint); empty = no constraints.
        /// @note  The base joint is free (a ball): it is where the chain meets the body.
        std::vector<Hinge> hinges;
        /// @brief The pass back keeps the last bone's source direction instead of aiming it at the joint above.
        /// @note  For limbs: free, FABRIK reaches a nearer target by folding the short last bone back on itself (a
        ///        5-bone hind leg reaching 8 units up turned its pastern -147 degrees), which no-flip hinges forbid,
        ///        and the solve stalls short; held, the joints above bend instead.
        bool               holdEndBone = false;
        /// @brief The swing from the source base-to-tip direction onto base-to-target, set by each ReachBack and used
        ///        by the ReachOut after it: every hinge axis (and the held end bone) turns with it, so the chain's
        ///        bend plane swings toward a target off to the side as a hip turns a leg.
        /// @note  One frame for both passes and every joint. Carried per joint instead (on the parent going out, the
        ///        child coming back), the two passes disagreed about the axes and a leg reaching 3 units sideways
        ///        fanned out to 6.
        Quat               frame;
    };

    /// @brief A chain over `joints` with a hinge at every interior joint, its axis the joint's bend in this pose,
    ///        limited to [0, kDefaultMaxBend].
    /// @note  A joint too straight to show its bend axis takes the bend plane of the chain's first bent joint and may
    ///        bend either way, [-kDefaultMaxBend, kDefaultMaxBend]; in a chain with no bent joint at all (a straight
    ///        spine) it is a ball joint. A caller that knows the side (the foot IK data's knee axis) replaces that
    ///        hinge.
    Chain MakeChain(std::span<const Vec3> joints);

    struct Params {
        int   maxIterations = 10;
        /// @brief Close enough: the end effector this near the target stops the iterations.
        float tolerance     = 0.1f;
    };

    struct Result {
        int   iterations = 0;
        /// @brief The end effector's distance from the target afterwards.
        float error      = 0.0f;
    };

    /// @brief One pass from the end effector, pinned at `target`, back to the base, every hinge applied.
    /// @return Where the pass leaves the base: where it would have to be for the chain to reach.
    Vec3 ReachBack(Chain& chain, Vec3 target);

    /// @brief One pass from the base, pinned at `base`, out to the end effector, every hinge applied.
    /// @pre   A ReachBack set the chain's frame.
    void ReachOut(Chain& chain, Vec3 base);

    /// @brief Iterates both passes with the base held where it is.
    /// @note  A target out of reach leaves the chain stretched toward it (as straight as the hinges allow).
    Result Solve(Chain& chain, Vec3 target, const Params& params = {});

    /// @brief The model-space rotation of every joint's bone after solving: each bone swung by the shortest arc from its
    ///        source direction onto its new one.
    /// @param sourceRotations Each joint's model-space rotation in the source pose (joints.size() of them).
    /// @return One per joint; the end effector's is unchanged (its bone has no child in the chain to aim it).
    std::vector<Quat> AimRotations(const Chain& chain, std::span<const Quat> sourceRotations);

    /// @brief A limb: a chain whose base hangs from one of the body's anchors, reaching for its own target.
    struct Limb {
        Chain       chain;
        std::size_t anchor = 0;   ///< index into Body::spines
        Vec3        target;
    };

    /// @brief A body: a root, a spine down to each anchor, and the limbs hanging from the anchors.
    /// @note  A spine of one joint is the anchor alone, rigid on the root (the horse's pelvis, a biped's pelvis); a
    ///        longer one bends to bring its anchor where its limbs want it (the horse's Spine1 .. Ribcage). Every
    ///        spine's base hangs rigidly from the root.
    struct Body {
        Vec3               root;
        std::vector<Chain> spines;
        std::vector<Limb>  limbs;
    };

    struct BodyParams {
        /// @brief How far an anchor gives toward where its limbs want it: 0 = the body never moves, 1 = all the way.
        float  stiffness     = 1.0f;
        int    maxIterations = 10;
        Params limb;
        Params spine;
    };

    /// @brief Multiple end effectors with sub-bases: each iteration the limbs reach back from their targets, every
    ///        anchor moves (by `stiffness`) toward the mean of what its limbs ask, the root carries the rigid anchors
    ///        and the spines bend to the others, and the limbs reach out again from where their anchors went.
    /// @note  Anchors only translate here: a spine's bend also turns its anchor, which swings the limbs' bases around
    ///        it; a caller writing the spine's rotations reads the true bases back and solves the limbs again from
    ///        them.
    /// @return The largest limb error.
    float SolveBody(Body& body, const BodyParams& params = {});

}  // namespace CB::platform::ik
