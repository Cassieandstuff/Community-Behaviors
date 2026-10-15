/// @file
/// @brief hkbCharacter, its setup and data, and the animation binding set the clips draw from.
#pragma once

#include <Animation/Animation/hkaAnimationBinding.h>
#include <Animation/Mapper/hkaSkeletonMapper.h>
#include <Animation/Rig/hkaSkeleton.h>
#include <Behavior/Event/hkbEvent.h>

namespace CB { namespace schema
{
    class hkbBehaviorGraph;
    struct hkbProjectData;
    class hkbRagdollDriver;
    class hkpRigidBody;
    class hkpWorld;
    struct hkbEventQueue;
    struct hkbWorld;
    class hkaRagdollInstance;

    /// @brief Casts the rays behavior nodes ask for (the game implements it).
    /// @note Only the slot vanilla's callers use is declared; slot 0 is the destructor.
    class hkbRaycastInterface
    {
    public:
        virtual ~hkbRaycastInterface();
        /// @brief Whether the ray from `from` to `to` hits; the hit's fraction along it and its normal.
        virtual hkBool castRay(const hkVector4& from, const hkVector4& to, hkUint32 collisionFilterInfo, hkReal& hitFractionOut,
                               hkVector4& normalWSOut) = 0;   ///< slot 1
    };

    struct hkbAnimationBindingWithTriggers : hkReferencedObject
    {
        /// @note 0x18 bytes.
        struct Trigger
        {
            hkReal       m_time;
            hkbEventBase m_event;
        };

        hkRefPtr<hkaAnimationBinding> m_binding;    ///< +0x10
        hkArray<Trigger>              m_triggers;   ///< +0x18
    };

    struct hkbAnimationBindingSet : hkReferencedObject
    {
        hkArray<hkbAnimationBindingWithTriggers*> m_bindings;   ///< +0x10, indexed by hkbClipGenerator::m_animationBindingIndex
    };

    struct hkbCharacterData : hkReferencedObject
    {
        char      m_pad10[0x30 - 0x10];
        hkVector4 m_modelUpMS;        ///< +0x30
        hkVector4 m_modelForwardMS;   ///< +0x40
        hkVector4 m_modelRightMS;     ///< +0x50
        char      m_pad60[0xa8 - 0x60];
        hkReal    m_scale;            ///< +0xa8
    };

    struct hkbCharacterSetup : hkReferencedObject
    {
        char                             m_pad10[0x20 - 0x10];
        hkRefPtr<hkaSkeleton>            m_animationSkeleton;     ///< +0x20
        char                             m_pad28[0x38 - 0x28];
        hkRefPtr<hkbAnimationBindingSet> m_animationBindingSet;   ///< +0x38
        hkbCharacterData*                m_data;                  ///< +0x40

        /// @note Name provisional.
        const hkaSkeletonMapper* getRetargetingSkeletonMapper(const char* rigName);
        /// @brief Built lazily into +0x48 from the character data's mirroring info.
        hkaMirroredSkeleton* getMirroredSkeleton();
    };

    struct hkbCharacter : hkReferencedObject
    {
        char                             m_pad10[0x20 - 0x10];
        hkInt16                          m_currentLod;            ///< +0x20
        hkInt16                          m_numTracksInLod;        ///< +0x22, -1 = no limit
        char                             m_pad24[0x30 - 0x24];
        hkRefPtr<hkbRagdollDriver>       m_ragdollDriver;         ///< +0x30
        char                             m_pad38[0x50 - 0x38];
        hkRefPtr<hkbCharacterSetup>      m_setup;                 ///< +0x50
        hkbBehaviorGraph*                m_behaviorGraph;         ///< +0x58
        hkbProjectData*                  m_projectData;           ///< +0x60
        hkRefPtr<hkbAnimationBindingSet> m_animationBindingSet;   ///< +0x68
        hkbRaycastInterface*             m_raycastInterface;      ///< +0x70
        hkbWorld*                        m_world;                 ///< +0x78
        hkbEventQueue*                   m_eventQueue;            ///< +0x80
        hkQsTransform*                   m_worldFromModel;        ///< +0x88

        /// @brief The rigid bodies near `position` (within `radius`) that collide with `collisionFilterInfo`.
        /// @note Name and parameter names provisional (the two flags: mode 1 sets the first, mode 2 both — read as
        ///       "exclude this character's bodies" and "only other characters' bodies").
        virtual void getNearbyRigidBodies(const hkVector4& position, hkReal radius, hkpWorld* world, hkArray<hkpRigidBody*>& rigidBodiesOut,
                                          hkUint32 collisionFilterInfo, bool excludeThisCharacter, bool onlyOtherCharacters);   ///< slot 3
        /// @brief The characters within `radius`.
        virtual void getNearbyCharacters(hkReal radius, hkArray<hkbCharacter*>& charactersOut);   ///< slot 4

        /// @brief The local pose, built from the skeleton's reference pose on first use.
        /// @note Out of line in vanilla.
        const hkQsTransform* getPoseLocal();
        /// @brief The character's own raycast interface, else its world's.
        /// @note Out of line in vanilla (0x140abdba0).
        hkbRaycastInterface* getRaycastInterface() const;

        /// @brief The character's own binding set, else its setup's.
        HK_FORCE_INLINE hkbAnimationBindingSet* getAnimationBindingSet() const
        {
            return m_animationBindingSet ? m_animationBindingSet.val() : (m_setup ? m_setup->m_animationBindingSet.val() : HK_NULL);
        }
    };
} }
