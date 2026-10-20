/// @file
/// @brief hkpPhantom: a world object that reports overlaps but is not simulated.
#pragma once

#include <Physics/Dynamics/World/hkpWorldObject.h>

namespace CB { namespace havok
{
    class hkpPhantomOverlapListener;
    class hkpPhantomListener;

    struct hkAabb;

    /// @note 0xf0 bytes. Its collidable's broad-phase type is BROAD_PHASE_PHANTOM (2). Virtual slots 6–14 per the Havok
    ///       SDK (nine, as hkpCachingShapePhantom's vtable 0x141974af8 holds before hkpShapePhantom's four); only
    ///       ensureDeterministicOrder's (slot 11) is confirmed by a matched call.
    class hkpPhantom : public hkpWorldObject
    {
    public:
        hkArray<hkpPhantomOverlapListener*> m_overlapListeners;   ///< +0xd0
        hkArray<hkpPhantomListener*>        m_phantomListeners;   ///< +0xe0

        virtual int         getType() const;                                            ///< slot 6 — hkpPhantomType
        virtual void        calcAabb(hkAabb& aabb);                                     ///< slot 7
        virtual void        addOverlappingCollidable(hkpCollidable* collidable);        ///< slot 8
        virtual hkBool      isOverlappingCollidableAdded(const hkpCollidable* collidable);   ///< slot 9
        virtual void        removeOverlappingCollidable(hkpCollidable* collidable);     ///< slot 10
        /// @brief Sorts the overlapping collidables so queries report them in a deterministic order.
        virtual void        ensureDeterministicOrder();                                 ///< slot 11
        virtual hkpPhantom* clone() const;                                              ///< slot 12
        virtual void        updateShapeCollectionFilter();                              ///< slot 13
        virtual void        deallocateInternalArrays();                                 ///< slot 14
    };
} }
