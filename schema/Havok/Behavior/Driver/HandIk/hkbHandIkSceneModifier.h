/// @file
/// @brief hkbHandIkSceneModifier: the first hand IK pass (before the attachments are fixed up).
#pragma once

#include <Behavior/World/hkbSceneModifier.h>

namespace CB { namespace schema
{
    /// @note 0x10 bytes. The destructor is implicit (vanilla's scalar deleting destructor stamps only hkBaseObject's
    ///       vtable).
    class hkbHandIkSceneModifier : public hkbSceneModifier
    {
    public:
        /// @note Vanilla has no out-of-line copy (hkbWorld's constructor inlines it); out of line here, it brings the
        ///       class's vtable and scalar deleting destructor into Behavior/Driver/HandIk/hkbHandIkSceneModifier.cpp.
        hkbHandIkSceneModifier();

        /// @brief Runs each character's hand IK driver (isFixup false).
        virtual void modify(hkbSceneCharacters& sceneCharacters, hkReal timestep);   ///< slot 3
    };
} }
