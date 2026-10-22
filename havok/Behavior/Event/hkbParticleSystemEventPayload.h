/// @file
/// @brief hkbParticleSystemEventPayload: the payload of a particle-emission event.
#pragma once

#include <Behavior/Event/hkbEvent.h>

namespace CB { namespace havok
{
    /// @note 0x50 bytes; members after the Havok SDK (unreflected here), unverified beyond the size.
    class hkbParticleSystemEventPayload : public hkbEventPayload
    {
    public:
        enum SystemType { DEBRIS = 0, DUST = 1, EXPLOSION = 2, SMOKE = 3, SPARKS = 4 };

        hkEnum<SystemType, hkUint8> m_type;            ///< +0x10
        hkInt16                     m_emitBoneIndex;   ///< +0x12
        hkVector4                   m_offset;          ///< +0x20
        hkVector4                   m_direction;       ///< +0x30
        hkInt32                     m_numParticles;    ///< +0x40
        hkReal                      m_speed;           ///< +0x44

        HK_FORCE_INLINE hkbParticleSystemEventPayload() {}
        /// @note Vanilla inlines it into the type registry's finish function; out of line here, it also brings the
        ///       class's vtable and scalar deleting destructor into this unit.
        hkbParticleSystemEventPayload(hkFinishLoadedObjectFlag flag);
    };
} }
