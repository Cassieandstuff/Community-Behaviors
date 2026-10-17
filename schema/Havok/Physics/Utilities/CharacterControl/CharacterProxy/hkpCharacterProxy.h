/// @file
/// @brief hkpCharacterProxy: a phantom-based character controller (moves a shape phantom through the world).
#pragma once

#include <Physics/Utilities/CharacterControl/CharacterProxy/hkpCharacterProxyCinfo.h>
#include <Physics/Utilities/CharacterControl/hkpCharacterControl.h>
#include <Physics/Collide/Agent/Query/hkpCdPointCollector.h>
#include <Physics/Dynamics/Entity/hkpEntityListener.h>
#include <Physics/Dynamics/Phantom/hkpPhantomListener.h>
#include <Common/Base/Types/Physics/hkStepInfo.h>

namespace CB { namespace schema
{
    class hkpRigidBody;
    class hkpPhantom;

    /// @note 0xf0 bytes; vtables 0x14197b2c0 (+0), 0x14197b2f0 (hkpEntityListener, +0x10) and 0x14197b328
    ///       (hkpPhantomListener, +0x18). Member offsets from updateFromCinfo (0x140b804d0); names per the Havok SDK.
    class hkpCharacterProxy : public hkReferencedObject, public hkpEntityListener, public hkpPhantomListener
    {
    public:
        hkArray<hkRootCdPoint> m_manifold;                        ///< +0x20
        hkArray<hkpRigidBody*> m_bodies;                          ///< +0x30
        hkArray<hkpPhantom*>   m_phantoms;                        ///< +0x40
        hkArray<hkpPhantom*>   m_overlappingTriggerVolumes;       ///< +0x50
        hkVector4              m_velocity;                        ///< +0x60
        hkVector4              m_oldDisplacement;                 ///< +0x70
        hkpShapePhantom*       m_shapePhantom;                    ///< +0x80
        hkReal                 m_dynamicFriction;                 ///< +0x88
        hkReal                 m_staticFriction;                  ///< +0x8c
        /// @brief Normalized.
        hkVector4              m_up;                              ///< +0x90
        hkReal                 m_extraUpStaticFriction;           ///< +0xa0
        hkReal                 m_extraDownStaticFriction;         ///< +0xa4
        hkReal                 m_keepDistance;                    ///< +0xa8
        hkReal                 m_keepContactTolerance;            ///< +0xac
        hkReal                 m_contactAngleSensitivity;         ///< +0xb0
        int                    m_userPlanes;                      ///< +0xb4
        hkReal                 m_maxCharacterSpeedForSolver;      ///< +0xb8
        hkReal                 m_characterStrength;               ///< +0xbc
        hkReal                 m_characterMass;                   ///< +0xc0
        hkArray<void*>         m_listeners;                       ///< +0xc8 — hkpCharacterProxyListener*
        /// @brief cos(cinfo.m_maxSlope).
        hkReal                 m_maxSlopeCosine;                  ///< +0xd8
        hkReal                 m_penetrationRecoverySpeed;        ///< +0xdc
        int                    m_maxCastIterations;               ///< +0xe0
        hkBool                 m_refreshManifoldInCheckSupport;   ///< +0xe4

        /// @note Out of line in vanilla (0x140b800c0); calls updateFromCinfo.
        hkpCharacterProxy(const hkpCharacterProxyCinfo& info);

        /// @brief Copies `cinfo` in (normalizing up, taking cos of the max slope) and tags the phantom with this proxy
        ///        (world-object property 0x1300).
        void updateFromCinfo(const hkpCharacterProxyCinfo& cinfo);
        /// @brief Moves the proxy by its velocity over `stepInfo`, sliding along what it touches.
        void integrate(const hkStepInfo& stepInfo, const hkVector4& worldGravity);
        /// @brief integrate with caller-provided collectors.
        /// @note Parameter list unconfirmed past the gravity: vanilla's integrate passes a null 4th argument, then the
        ///       two collectors.
        void integrateWithCollectors(const hkStepInfo& stepInfo, const hkVector4& gravity, void* unknown, hkpAllCdPointCollector& castCollector,
                                     hkpAllCdPointCollector& startPointCollector);
        /// @brief Whether (and how) the proxy is supported in `direction`.
        void checkSupport(const hkVector4& direction, hkpSurfaceInfo& ground);
        /// @brief checkSupport with a caller-provided collector.
        void checkSupportWithCollector(const hkVector4& direction, hkpSurfaceInfo& ground, hkpAllCdPointCollector& startPointCollector);
        /// @brief The phantom's translation.
        /// @note Out of line in vanilla.
        const hkVector4& getPosition() const;
        /// @brief Moves the phantom (its broad-phase AABB widened by keepDistance + keepContactTolerance).
        void setPosition(const hkVector4& position);
        /// @note Out of line in vanilla.
        const hkVector4& getLinearVelocity() const;
        /// @note Out of line in vanilla.
        void setLinearVelocity(const hkVector4& velocity);
    };
} }
