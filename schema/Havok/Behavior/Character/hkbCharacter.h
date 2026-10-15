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
    struct hkbBehaviorGraphInternalState;
    class hkbCharacterControllerDriver;
    class hkbFootIkDriver;
    class hkbHandIkDriver;
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

    /// @note Vanilla's scalar deleting destructor (0x140abddb0) sits in hkbCharacter's TU, emitted there by
    ///       hkbCharacter::accessAnimationBindingSet's `new`; it calls the out-of-line destructor.
    struct hkbAnimationBindingSet : hkReferencedObject
    {
        hkArray<hkbAnimationBindingWithTriggers*> m_bindings;   ///< +0x10, indexed by hkbClipGenerator::m_animationBindingIndex

        /// @note No user-provided default constructor: vanilla's `new hkbAnimationBindingSet()` value-initializes, zeroing
        ///       the block before the members are constructed.
        /// @note Out of line in vanilla (0x140adf660); not reimplemented.
        virtual ~hkbAnimationBindingSet();   ///< slot 0
    };

    struct hkbCharacterStringData : hkReferencedObject
    {
        char        m_pad10[0xa0 - 0x10];
        hkStringPtr m_name;   ///< +0xa0
    };

    struct hkbCharacterData : hkReferencedObject
    {
        char                    m_pad10[0x30 - 0x10];
        hkVector4               m_modelUpMS;        ///< +0x30
        hkVector4               m_modelForwardMS;   ///< +0x40
        hkVector4               m_modelRightMS;     ///< +0x50
        char                    m_pad60[0x98 - 0x60];
        hkbCharacterStringData* m_stringData;       ///< +0x98
        char                    m_padA0[0xa8 - 0xa0];
        hkReal                  m_scale;            ///< +0xa8
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
        hkArray<hkbCharacter*>                 m_nearbyCharacters;            ///< +0x10
        hkInt16                                m_currentLod;                  ///< +0x20
        hkInt16                                m_numTracksInLod;              ///< +0x22, -1 = no limit
        char                                   m_pad24[0x28 - 0x24];
        hkStringPtr                            m_name;                        ///< +0x28
        hkRefPtr<hkbRagdollDriver>             m_ragdollDriver;               ///< +0x30
        hkRefPtr<hkbCharacterControllerDriver> m_characterControllerDriver;   ///< +0x38
        hkRefPtr<hkbFootIkDriver>              m_footIkDriver;                ///< +0x40
        hkRefPtr<hkbHandIkDriver>              m_handIkDriver;                ///< +0x48
        hkRefPtr<hkbCharacterSetup>            m_setup;                       ///< +0x50
        /// @note Referenced (setBehavior swaps it as a hkRefPtr would) but declared raw: code across the tree reads it
        ///       as a plain pointer.
        hkbBehaviorGraph*                      m_behaviorGraph;               ///< +0x58
        hkbProjectData*                        m_projectData;                 ///< +0x60
        hkRefPtr<hkbAnimationBindingSet>       m_animationBindingSet;         ///< +0x68
        hkbRaycastInterface*                   m_raycastInterface;            ///< +0x70
        hkbWorld*                              m_world;                       ///< +0x78
        hkbEventQueue*                         m_eventQueue;                  ///< +0x80
        hkQsTransform*                         m_worldFromModel;              ///< +0x88
        hkQsTransform*                         m_poseLocal;                   ///< +0x90
        int                                    m_numPoseLocal;                ///< +0x98
        /// @brief m_worldFromModel is the character's to free (a heap block of one hkQsTransform).
        hkBool                                 m_deleteWorldFromModel;        ///< +0x9c
        /// @brief m_poseLocal is the character's to free (an hkMemoryRouter::easyAlloc block).
        hkBool                                 m_deletePoseLocal;             ///< +0x9d

        /// @note Leaves m_handIkDriver and m_animationBindingSet null, as vanilla's does (both runtime-only).
        hkbCharacter(hkFinishLoadedObjectFlag flag);
        /// @note Out of line in vanilla (called by the scalar deleting destructor); not reimplemented yet.
        virtual ~hkbCharacter();                                                                    ///< slot 0

        /// @brief The rigid bodies near `position` (within `radius`) that collide with `collisionFilterInfo`.
        /// @note Name and parameter names provisional (the two flags: mode 1 sets the first, mode 2 both — read as
        ///       "exclude this character's bodies" and "only other characters' bodies").
        virtual void getNearbyRigidBodies(const hkVector4& position, hkReal radius, hkpWorld* world, hkArray<hkpRigidBody*>& rigidBodiesOut,
                                          hkUint32 collisionFilterInfo, bool excludeThisCharacter, bool onlyOtherCharacters);   ///< slot 3
        /// @brief The characters within `radius`.
        virtual void getNearbyCharacters(hkReal radius, hkArray<hkbCharacter*>& charactersOut);   ///< slot 4

        /// @brief Activates the behavior graph (when it is not active yet) in a context built from the world.
        void activate();
        /// @brief activate, restoring the graph from a saved internal state.
        /// @note Name provisional (the Havok SDK's).
        void activateWithInternalState(const hkbBehaviorGraphInternalState& internalState);
        /// @brief Deactivates the active graph, then resets the drivers and frees the owned local pose.
        void deactivate();
        /// @brief Hands `e` to the active graph.
        void handleEvent(const hkbEvent& e);
        /// @brief The character's name, else the character data's.
        hkStringPtr getName() const;
        /// @brief The local pose, built from the skeleton's reference pose on first use.
        /// @note Out of line in vanilla.
        const hkQsTransform* getPoseLocal();
        /// @brief The character's own binding set, else its setup's — created (empty) when the setup has none.
        /// @note Name provisional (Havok's `access` prefix: the non-const getter that may build).
        hkbAnimationBindingSet* accessAnimationBindingSet();
        void setAnimationBindingSet(hkbAnimationBindingSet* animationBindingSet);
        /// @brief Replaces m_worldFromModel, freeing the old one first when the character owned it.
        void setWorldFromModel(hkQsTransform* worldFromModel, bool deleteWorldFromModel);
        /// @brief Replaces m_poseLocal, freeing the old one first when the character owned it.
        void setPoseLocal(hkQsTransform* poseLocal, bool deletePoseLocal);
        /// @brief The character's own raycast interface, else its world's.
        /// @note Out of line in vanilla (0x140abdba0).
        hkbRaycastInterface* getRaycastInterface() const;
        /// @brief Swaps the behavior graph, then tells every world listener (handing it the old graph).
        void setBehavior(hkbBehaviorGraph* behavior);
        /// @brief Points m_poseLocal at `numPoseLocal` transforms; ownership (m_deletePoseLocal) is left as it is.
        /// @note Name provisional.
        void setPoseLocalBuffer(hkQsTransform* poseLocal, int numPoseLocal);
        /// @brief Gives the character its own copy of the skeleton's reference pose, unless it has a pose.
        /// @note Name provisional.
        void initPoseLocal();
        /// @brief Frees the local pose if the character owns it.
        /// @note Name provisional.
        void deletePoseLocal();

        /// @brief The character's own binding set, else its setup's.
        HK_FORCE_INLINE hkbAnimationBindingSet* getAnimationBindingSet() const
        {
            return m_animationBindingSet ? m_animationBindingSet.val() : (m_setup ? m_setup->m_animationBindingSet.val() : HK_NULL);
        }
    };
} }
