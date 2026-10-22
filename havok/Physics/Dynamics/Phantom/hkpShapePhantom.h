/// @file
/// @brief hkpShapePhantom: a phantom with a shape and a transform (the base of the simple and caching phantoms).
#pragma once

#include <Physics/Dynamics/Phantom/hkpPhantom.h>

namespace CB { namespace havok
{
    class hkpCdPointCollector;
    class hkpCdBodyPairCollector;

    /// @note 0x1a0 bytes; vtable 0x141974a58. Its collidable's motion points at m_motionState.
    class hkpShapePhantom : public hkpPhantom
    {
    public:
        hkMotionState m_motionState;   ///< +0xf0 — the transform at +0xf0, its translation at +0x120

        /// @note Out of line in vanilla (0x140b49360); calls the hkpWorldObject constructor with BROAD_PHASE_PHANTOM.
        hkpShapePhantom(const hkpShape* shape, const hkTransform& transform);

        /// @note Slots 15–18 per the Havok SDK; only getPenetrations' (slot 18, the last) is confirmed by a matched call.
        virtual void setPositionAndLinearCast(const hkVector4& position, const void* input, hkpCdPointCollector& castCollector,
                                              hkpCdPointCollector* startCollector);   ///< slot 15 — input: hkpLinearCastInput
        virtual void setTransformAndLinearCast(const hkTransform& transform, const void* input, hkpCdPointCollector& castCollector,
                                               hkpCdPointCollector* startCollector);  ///< slot 16
        virtual void getClosestPoints(hkpCdPointCollector& collector, const void* input = HK_NULL);       ///< slot 17 — hkpCollisionInput
        /// @brief Reports every body pair the phantom's shape overlaps to `collector`.
        virtual void getPenetrations(hkpCdBodyPairCollector& collector, const void* input = HK_NULL);     ///< slot 18 — hkpCollisionInput

        /// @brief Moves the phantom and updates the broad phase.
        void setTransform(const hkTransform& transform);
        /// @brief Moves the phantom's translation; `extraTolerance` widens the broad-phase AABB.
        /// @note `extraTolerance` arrives in xmm2.
        void setPosition(const hkVector4& position, hkReal extraTolerance);

        HK_FORCE_INLINE const hkTransform& getTransform() const { return m_motionState.getTransform(); }
    };
} }
