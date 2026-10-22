/// @file
/// @brief hkpAllCdBodyPairCollector: gathers every overlapping body pair a penetration query reports.
#pragma once

#include <Physics/Collide/Agent/Query/hkpCdBodyPairCollector.h>
#include <Physics/Collide/Agent/Collidable/hkpCollidable.h>

namespace CB { namespace havok
{
    /// @note 0x20 bytes.
    struct hkpRootCdBodyPair
    {
        const hkpCollidable* m_rootCollidableA;   ///< +0x00
        hkUint32             m_shapeKeyA;         ///< +0x08
        const hkpCollidable* m_rootCollidableB;   ///< +0x10
        hkUint32             m_shapeKeyB;         ///< +0x18
    };

    /// @note 0x220 bytes; vtable 0x1417ee018. 16 pairs in place before it allocates (from the temp allocator).
    class hkpAllCdBodyPairCollector : public hkpCdBodyPairCollector
    {
    public:
        hkTempInplaceArray<hkpRootCdBodyPair, 16> m_hits;   ///< +0x10

        HK_FORCE_INLINE hkpAllCdBodyPairCollector() { reset(); }
        HK_FORCE_INLINE virtual ~hkpAllCdBodyPairCollector() {}
        virtual void addCdBodyPair(const hkpCdBody& bodyA, const hkpCdBody& bodyB);

        HK_FORCE_INLINE void reset()
        {
            m_hits.clear();
            hkpCdBodyPairCollector::reset();
        }
        HK_FORCE_INLINE const hkArray<hkpRootCdBodyPair>& getHits() const { return m_hits; }
    };
} }
