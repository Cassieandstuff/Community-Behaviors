/// @file
/// @brief hkbCameraShakeEventPayload: the payload of a camera-shake event.
#pragma once

#include <Behavior/Event/hkbEvent.h>

namespace CB { namespace havok
{
    /// @note 0x18 bytes; members after the Havok SDK (unreflected here), unverified.
    class hkbCameraShakeEventPayload : public hkbEventPayload
    {
    public:
        hkReal m_amplitude;   ///< +0x10
        hkReal m_halfLife;    ///< +0x14

        HK_FORCE_INLINE hkbCameraShakeEventPayload() {}
        /// @note Vanilla inlines it into the type registry's finish function; out of line here, it also brings the
        ///       class's vtable and scalar deleting destructor into this unit.
        hkbCameraShakeEventPayload(hkFinishLoadedObjectFlag flag);
    };
} }
