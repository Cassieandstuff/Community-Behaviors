/// @file
/// @brief hkpCdPointCollector / hkpAllCdPointCollector and the hits they gather (only what the behavior runtime uses).
#pragma once

#include <Physics/Collide/Agent/Collidable/hkpCollidable.h>

namespace CB { namespace schema
{
    /// @note 0x20 bytes: position, separating normal (distance in w).
    struct hkContactPoint
    {
        hkVector4 m_position;
        hkVector4 m_separatingNormal;

        /// @brief The separation along the normal (negative = penetrating), kept in the normal's w.
        HK_FORCE_INLINE hkReal getDistance() const { return m_separatingNormal(3); }
        /// @brief The same contact seen from the other body: the position moves onto that body's surface, the normal
        ///        turns around.
        HK_FORCE_INLINE void flip()
        {
            m_position.addMul4(m_separatingNormal.getSimdAt3(), m_separatingNormal);
            m_separatingNormal.setNeg3(m_separatingNormal);
        }
    };

    /// @note 0x40 bytes.
    struct hkRootCdPoint
    {
        hkContactPoint       m_contact;           ///< +0x00
        const hkpCollidable* m_rootCollidableA;   ///< +0x20
        hkUint32             m_shapeKeyA;         ///< +0x28
        const hkpCollidable* m_rootCollidableB;   ///< +0x30
        hkUint32             m_shapeKeyB;         ///< +0x38
    };

    /// @note Only the reset the inline constructors use is defined; the rest of the interface is declared as the vtable
    ///       needs it (slots per the Havok SDK, unverified).
    class hkpCdPointCollector
    {
    public:
        hkReal m_earlyOutDistance;   ///< +0x08

        HK_FORCE_INLINE hkpCdPointCollector() { reset(); }
        virtual ~hkpCdPointCollector() {}
        virtual void addCdPoint(const void* event) = 0;
        HK_FORCE_INLINE void reset() { m_earlyOutDistance = HK_REAL_MAX; }
    };

    /// @brief Gathers every hit (8 in place before it allocates).
    class hkpAllCdPointCollector : public hkpCdPointCollector
    {
    public:
        hkInplaceArray<hkRootCdPoint, 8> m_hits;   ///< +0x10

        HK_FORCE_INLINE hkpAllCdPointCollector() { reset(); }
        /// @note User-declared, as Havok's: the destructor restamps the vtable before the hits array goes.
        HK_FORCE_INLINE virtual ~hkpAllCdPointCollector() {}
        virtual void addCdPoint(const void* event);

        HK_FORCE_INLINE void reset()
        {
            m_hits.clear();
            hkpCdPointCollector::reset();
        }
        HK_FORCE_INLINE const hkArray<hkRootCdPoint>& getHits() const { return m_hits; }
    };
} }
