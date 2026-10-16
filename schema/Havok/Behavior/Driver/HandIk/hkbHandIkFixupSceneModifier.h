/// @file
/// @brief hkbHandIkFixupSceneModifier: the second hand IK pass, which fixes the hands up after the attachments move.
#pragma once

#include <Behavior/World/hkbSceneModifier.h>

namespace CB { namespace schema
{
    /// @note 0x10 bytes. The destructor is implicit (vanilla's scalar deleting destructor stamps only hkBaseObject's
    ///       vtable).
    class hkbHandIkFixupSceneModifier : public hkbSceneModifier
    {
    public:
        /// @note Vanilla has no out-of-line copy (hkbWorld's constructor inlines it); out of line here, it brings the
        ///       class's vtable and scalar deleting destructor into Behavior/Driver/HandIk/hkbHandIkFixupSceneModifier.cpp.
        hkbHandIkFixupSceneModifier();

        /// @brief Runs each character's hand IK driver (isFixup true).
        virtual void modify(hkbSceneCharacters& sceneCharacters, hkReal timestep);   ///< slot 3
    };
} }
