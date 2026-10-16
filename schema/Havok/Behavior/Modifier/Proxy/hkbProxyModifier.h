/// @file
/// @brief hkbProxyModifier: moves the character through the physics world with an hkpCharacterProxy (a phantom-based
///        controller), and pulls the world-from-model toward it.
#pragma once

#include <Behavior/Context/hkbContext.h>
#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace schema
{
    class hkpCharacterProxy;
    class hkpConvexShape;
    class hkpShapePhantom;

    /// @note 0x120 bytes. Member and enum names are the class's reflected names (the exe's strings); the layout and
    ///       defaults are vanilla's. The destructor is implicit: vanilla's scalar deleting destructor (slot 0) inlines it.
    class hkbProxyModifier : public hkbModifier
    {
    public:
        /// @brief What the character proxy is built from (hkpCharacterProxyCinfo's settings).
        /// @note 0x50 bytes.
        struct ProxyInfo
        {
            hkReal    m_dynamicFriction;                 ///< +0x00
            hkReal    m_staticFriction;                  ///< +0x04
            hkReal    m_keepContactTolerance;            ///< +0x08
            hkVector4 m_up;                              ///< +0x10
            hkReal    m_keepDistance;                    ///< +0x20
            hkReal    m_contactAngleSensitivity;         ///< +0x24
            hkUint32  m_userPlanes;                      ///< +0x28
            hkReal    m_maxCharacterSpeedForSolver;      ///< +0x2c
            hkReal    m_characterStrength;               ///< +0x30
            hkReal    m_characterMass;                   ///< +0x34
            /// @brief Degrees (the proxy takes radians).
            hkReal    m_maxSlope;                        ///< +0x38
            hkReal    m_penetrationRecoverySpeed;        ///< +0x3c
            int       m_maxCastIterations;               ///< +0x40
            hkBool    m_refreshManifoldInCheckSupport;   ///< +0x44

            HK_FORCE_INLINE ProxyInfo()
            :   m_dynamicFriction(1.0f),
                m_staticFriction(0.0f),
                m_keepContactTolerance(0.1f),
                m_keepDistance(0.05f),
                m_contactAngleSensitivity(10.0f),
                m_userPlanes(4),
                m_maxCharacterSpeedForSolver(10.0f),
                m_characterStrength(7.9e28f),
                m_characterMass(0.0f),
                m_maxSlope(90.0f),
                m_penetrationRecoverySpeed(1.0f),
                m_maxCastIterations(10),
                m_refreshManifoldInCheckSupport(false)
            {
                m_up.set(0.0f, 1.0f, 0.0f);
            }
        };

        enum PhantomType
        {
            PHANTOM_TYPE_SIMPLE  = 0,
            PHANTOM_TYPE_CACHING = 1,
        };
        enum LinearVelocityMode
        {
            LINEAR_VELOCITY_MODE_WORLD = 0,
            LINEAR_VELOCITY_MODE_MODEL = 1,
        };

        ProxyInfo                m_proxyInfo;                       ///< +0x50
        /// @brief Added to the incoming motion and the proxy's velocity.
        hkVector4                m_linearVelocity;                  ///< +0xa0
        hkReal                   m_horizontalGain;                  ///< +0xb0
        hkReal                   m_verticalGain;                    ///< +0xb4
        hkReal                   m_maxHorizontalSeparation;         ///< +0xb8
        hkReal                   m_maxVerticalSeparation;           ///< +0xbc
        hkReal                   m_verticalDisplacementError;       ///< +0xc0
        hkReal                   m_verticalDisplacementErrorGain;   ///< +0xc4
        hkReal                   m_maxVerticalDisplacement;         ///< +0xc8
        hkReal                   m_minVerticalDisplacement;         ///< +0xcc
        hkReal                   m_capsuleHeight;                   ///< +0xd0
        hkReal                   m_capsuleRadius;                   ///< +0xd4
        /// @brief Degrees.
        hkReal                   m_maxSlopeForRotation;             ///< +0xd8
        hkUint32                 m_collisionFilterInfo;             ///< +0xdc
        hkInt8                   m_phantomType;                     ///< +0xe0 — PhantomType
        hkInt8                   m_linearVelocityMode;              ///< +0xe1 — LinearVelocityMode
        hkBool                   m_ignoreIncomingRotation;          ///< +0xe2
        hkBool                   m_ignoreCollisionDuringRotation;   ///< +0xe3
        hkBool                   m_ignoreIncomingTranslation;       ///< +0xe4
        hkBool                   m_includeDownwardMomentum;         ///< +0xe5
        hkBool                   m_followWorldFromModel;            ///< +0xe6
        hkBool                   m_isTouchingGround;                ///< +0xe7
        /// @note Built by activate, released by deactivate (raw references).
        hkpCharacterProxy*       m_characterProxy;                  ///< +0xe8
        hkpShapePhantom*         m_phantom;                         ///< +0xf0
        /// @brief The phantom's shape; null = a capsule from m_capsuleHeight/m_capsuleRadius, built at activate.
        hkRefPtr<hkpConvexShape> m_phantomShape;                    ///< +0xf8
        /// @brief Where the proxy was asked to be (horizontally) and was not, carried into the next step.
        hkVector4                m_horizontalDisplacement;          ///< +0x100
        hkReal                   m_verticalDisplacement;            ///< +0x110
        /// @brief The time accumulated by update and not yet consumed by modify.
        hkReal                   m_timestep;                        ///< +0x114
        hkBool                   m_previousFrameFollowWorldFromModel;   ///< +0x118

        hkbProxyModifier();
        /// @brief The cloneNode copy: the settings (one 0x97-byte memCpy, m_proxyInfo through m_followWorldFromModel) and
        ///        the shared phantom shape, with the runtime state reset.
        hkbProxyModifier(const hkbProxyModifier& mod);

        /// @brief Builds the phantom (and its capsule, without m_phantomShape) and the character proxy at the
        ///        character's position, in the context's world.
        virtual void     activate(const hkbContext& context);                            ///< slot 4
        virtual void     update(const hkbContext& context, hkReal timestep);             ///< slot 5
        /// @brief Takes the phantom out of the world and releases it and the proxy.
        virtual void     deactivate(const hkbContext& context);                          ///< slot 7
        virtual hkbNode* cloneNode(hkbBehaviorGraph& rootBehavior) const;                ///< slot 12
        virtual void     modify(const hkbContext& context, hkbGeneratorOutput& inOut);   ///< slot 23

        /// @brief The velocity to drive the proxy with: the incoming motion over the timestep (else the proxy's own
        ///        velocity), plus m_linearVelocity, the proxy's downward momentum (if asked), and gravity over the timestep.
        /// @note Name provisional; out of line in vanilla (0x140afadc0).
        void computeDesiredVelocity(const hkbContext& context, const hkQsTransform& worldFromModel, hkVector4& desiredVelocityOut);
        /// @brief Steps the proxy at `desiredVelocity` (plus the horizontal catch-up), then records how far, horizontally,
        ///        it fell short of where it should be (clamped to m_maxHorizontalSeparation).
        /// @note Name provisional; out of line in vanilla (0x140afb0c0).
        void integrateProxy(const hkbContext& context, const hkVector4& desiredVelocity);
        /// @brief Moves the world-from-model translation to the proxy (horizontally fully, vertically by m_verticalGain
        ///        within m_maxVerticalSeparation), offset by the displacements.
        /// @note Name provisional; out of line in vanilla (0x140afb260).
        void updateTranslation(const hkbContext& context, hkQsTransform& worldFromModel);
        /// @brief Turns the phantom to the world-from-model's rotation, undoing both (phantom and rotation) when that
        ///        puts it against a surface steeper than m_maxSlopeForRotation.
        /// @note Name provisional; out of line in vanilla (0x140afb3e0).
        void updateRotation(const hkbContext& context, hkQsTransform& worldFromModel);
    };
} }
