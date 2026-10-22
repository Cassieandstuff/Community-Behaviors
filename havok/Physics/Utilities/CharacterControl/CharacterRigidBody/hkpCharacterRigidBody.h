/// @file
/// @brief hkpCharacterRigidBody: the rigid-body character controller (the one Skyrim drives actors with,
///        bUseCharacterRB). It drives an hkpRigidBody and, through its listener, filters that body's contacts.
#pragma once

#include <Physics/Dynamics/Entity/hkpEntityListener.h>
#include <Physics/Dynamics/World/Listener/hkpWorldPostSimulationListener.h>
#include <Physics/Collide/Agent/Query/hkpCdPointCollector.h>
#include <Physics/Utilities/CharacterControl/hkpCharacterControl.h>

namespace CB { namespace havok
{
    class hkpRigidBody;
    class hkpSimpleConstraintContactMgr;
    class hkpCharacterRigidBodyListener;
    class hkpCharacterRigidBodyCinfo;
    struct hkStepInfo;

    /// @note 0x90 bytes (the deleting destructor's fallback size; Bethesda's ahkpCharacterRigidBody is bigger). Vtables
    ///       at +0x00 (0x141976e30), +0x10 (hkpEntityListener, 0x141976e68) and +0x18 (hkpWorldPostSimulationListener,
    ///       0x141976ea0). Not reflected.
    class hkpCharacterRigidBody : public hkReferencedObject, public hkpEntityListener, public hkpWorldPostSimulationListener
    {
    public:
        /// @brief A vertical contact point the listener added (for a contact steeper than maxSlope), with the
        ///        contact manager it was added to.
        /// @note 0x30 bytes.
        struct VertPointInfo
        {
            hkContactPoint                 m_vertPoint;   ///< +0x00
            hkpSimpleConstraintContactMgr* m_mgr;         ///< +0x20
        };

        /// @brief A contact supporting the character, with the body it is on.
        /// @note 0x30 bytes.
        struct SupportInfo
        {
            hkContactPoint m_point;       ///< +0x00
            hkpRigidBody*  m_rigidBody;   ///< +0x20
        };

        /// @brief The w tag (as hkVector4::setInt24W stores it) marking a contact point's position as one of the
        ///        listener's vertical points, so unwelding skips it.
        enum { VERTICAL_POINT_TAG = 0x8df4a7 };
        /// @brief The w tag a vertical point's position gets as discardVerticalPoints removes it.
        enum { DISCARDED_VERTICAL_POINT_TAG = 0xfa2bb3 };
        /// @brief The property key the character body's debug display colour (the cinfo's vdbColor) is stored under.
        enum { DEBUG_DISPLAY_COLOR_PROPERTY = 0x1130 };

        hkpRigidBody*                  m_character;                     ///< +0x20
        hkpCharacterRigidBodyListener* m_listener;                      ///< +0x28 — referenced; null until set
        hkVector4                      m_up;                            ///< +0x30
        hkReal                         m_unweldingHeightOffsetFactor;   ///< +0x40
        /// @brief cos(maxSlope): a contact whose normal is closer to horizontal than this is a wall.
        hkReal                         m_maxSlopeCosine;                ///< +0x44
        hkReal                         m_maxSpeedForSimplexSolver;      ///< +0x48
        hkReal                         m_supportDistance;               ///< +0x4c
        hkReal                         m_hardSupportDistance;           ///< +0x50
        hkUint8                        m_pad54[0x60 - 0x54];
        /// @brief The acceleration the listener's mass modification pushes dynamic bodies with.
        hkVector4                      m_acceleration;                  ///< +0x60
        hkReal                         m_maxForce;                      ///< +0x70
        hkUint8                        m_pad74[0x78 - 0x74];
        hkArray<VertPointInfo>         m_verticalContactPoints;         ///< +0x78
        hkUint8                        m_pad88[0x90 - 0x88];

        /// @brief Builds the character's body (a MOTION_CHARACTER rigid body with infinite inertia) from `info`.
        hkpCharacterRigidBody(const hkpCharacterRigidBodyCinfo& info);
        /// @brief Releases the listener and the body, unhooking this from it.
        virtual ~hkpCharacterRigidBody();

        /// @brief Whether the character stands on something, and on what.
        virtual void checkSupport(const hkStepInfo& stepInfo, hkpSurfaceInfo& ground);   ///< slot 3
        /// @brief The supporting contacts, and whether they hold the character up (an hkpSurfaceInfo::SupportedState).
        virtual int  getSupportInfo(const hkStepInfo& stepInfo, hkArray<SupportInfo>& supportInfo) const;   ///< slot 4
        /// @brief Averages `supportInfo` into `ground`; dynamic bodies' velocities count only with `useDynamicBodyVelocities`.
        virtual void getGround(const hkArray<SupportInfo>& supportInfo, hkBool useDynamicBodyVelocities, hkpSurfaceInfo& ground) const;   ///< slot 5

        virtual void entityAddedCallback(hkpEntity* entity);    ///< hkpEntityListener slot 1
        virtual void entityRemovedCallback(hkpEntity* entity);  ///< hkpEntityListener slot 2
        virtual void postSimulationCallback(hkpWorld* world);   ///< hkpWorldPostSimulationListener slot 1

        hkpRigidBody*    getRigidBody() const;
        /// @brief Takes a reference to `listener` and registers this body as a post-simulation listener of its world.
        /// @note Out of line in vanilla (hkp code, not reimplemented).
        void             setListener(hkpCharacterRigidBodyListener* listener);
        /// @brief Releases the listener and unregisters this body from its world's post-simulation listeners.
        /// @note Out of line in vanilla (hkp code, not reimplemented).
        void             removeListener();
        void             setLinearAccelerationToMassModifier(const hkVector4& newAcc);
        /// @brief Sets the body's velocity (activating it) unless it is already that within 0.001 per axis; the change
        ///        over `timestep` becomes the mass modifier's acceleration.
        void             setLinearVelocity(const hkVector4& newVel, hkReal timestep);
        const hkVector4& getLinearVelocity() const;
        /// @brief Sets the body's angular velocity (activating it) unless it is already that within 0.001 per axis.
        void             setAngularVelocity(const hkVector4& newAVel);
    };
} }
