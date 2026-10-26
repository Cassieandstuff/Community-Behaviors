#pragma once

#include <Common/Base/hkBase.h>

#include <string>
#include <vector>

/// @file
/// @brief The foot IK rig CB derives from a character's foot IK data: each leg's full chain and the body bone it hangs
///        from, plus the spine between those body bones.
/// @note  Derived, not authored: vanilla's hkbFootIkDriverInfo names only hip, knee and ankle per leg, and every
///        creature that has foot IK data carries it. A skeleton the rule gets wrong is fixed by patching that data in
///        a bundle (the character .hkx is CB-compiled), not by a chain file.

namespace CB::havok {
    struct hkaSkeleton;
    class hkbFootIkDriverInfo;
}

namespace CB::feature::footik {

    /// @brief One leg: the bones FABRIK moves, from just below its anchor down to the ankle.
    struct RigLeg {
        std::vector<CB::havok::hkInt16> chain;    ///< parent first; back() is the data's ankle
        CB::havok::hkInt16              anchor;   ///< the body bone the leg hangs from; -1 = none
    };

    struct Rig {
        std::vector<RigLeg> legs;
        /// @brief The lowest bone above every anchor; the anchor itself when the legs share one (a biped's pelvis).
        CB::havok::hkInt16  bodyRoot = -1;
        /// @brief Per distinct anchor, the bones from just below bodyRoot down to it (empty for bodyRoot itself).
        /// @note  The horse: HorsePelvis's is [HorsePelvis], HorseRibcage's [HorseSpine1 .. HorseSpine4, HorseRibcage].
        std::vector<std::vector<CB::havok::hkInt16>> spines;
        /// @brief Why the data could not be used; empty when it could.
        std::string         problem;
    };

    /// @brief Each leg walks up from its ankle to the first bone another leg's ankle also hangs from: that bone is its
    ///        anchor, and the bones passed on the way are its chain.
    /// @note  A leg whose walk reaches the root without meeting another leg (a one-legged rig) anchors at its hip's
    ///        parent.
    /// @pre   `info`'s bone indices are indices into `skeleton` (vanilla validates the hip above the knee above the
    ///        ankle, 0x140adf340); an index out of range sets `problem`.
    Rig DeriveRig(const CB::havok::hkaSkeleton& skeleton, const CB::havok::hkbFootIkDriverInfo& info);

    /// @brief The rig as one log line per leg and spine, bones by name.
    std::string Describe(const Rig& rig, const CB::havok::hkaSkeleton& skeleton);

}  // namespace CB::feature::footik
