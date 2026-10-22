/// @file
/// @brief hkbFootIkSceneModifier: the scene pass that runs every character's foot IK driver.
#pragma once

#include <Behavior/World/hkbSceneModifier.h>

namespace CB { namespace havok
{
    /// @note 0x10 bytes. The destructor is implicit (vanilla's scalar deleting destructor stamps only hkBaseObject's
    ///       vtable).
    class hkbFootIkSceneModifier : public hkbSceneModifier
    {
    public:
        /// @note Vanilla has no out-of-line copy (hkbWorld's constructor inlines it); out of line here, it brings the
        ///       class's vtable and scalar deleting destructor into Behavior/Driver/FootIk/hkbFootIkSceneModifier.cpp.
        hkbFootIkSceneModifier();

        /// @brief Runs foot IK on each character's output (whatever the timestep).
        virtual void modify(hkbSceneCharacters& sceneCharacters, hkReal timestep);   ///< slot 3
    };
} }
