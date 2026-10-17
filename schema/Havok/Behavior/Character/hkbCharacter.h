/// @file
/// @brief hkbCharacter, its setup and data, and the animation binding set the clips draw from.
#pragma once

#include <Animation/Animation/hkaAnimationBinding.h>
#include <Animation/Ik/FootPlacement/hkaRaycastInterface.h>
#include <Animation/Mapper/hkaSkeletonMapper.h>
#include <Animation/Rig/hkaSkeleton.h>
#include <Behavior/Driver/FootIk/hkbFootIkDriverInfo.h>
#include <Behavior/Driver/HandIk/hkbHandIkDriverInfo.h>
#include <Behavior/Event/hkbEvent.h>
#include <Behavior/Project/hkbProjectData.h>
#include <Behavior/Variables/hkbVariableInfo.h>
#include <Behavior/Variables/hkbVariableValueSet.h>

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

    class hkbAnimationBindingWithTriggers : public hkReferencedObject
    {
    public:
        /// @note 0x18 bytes: the event payload is reference-holding (the destructor releases it).
        struct Trigger
        {
            hkReal           m_time;    ///< +0x0
            hkbEventProperty m_event;   ///< +0x8
        };

        /// @note Not reference-counted: vanilla's destructor only nulls it (no test, no release).
        hkaAnimationBinding*          m_binding;    ///< +0x10
        hkArray<Trigger>              m_triggers;   ///< +0x18

        /// @brief Drops the binding without releasing it, then the triggers.
        /// @note Out of line in vanilla (0x140adf5a0, Behavior/Character/hkbAnimationBindingSet.cpp).
        ~hkbAnimationBindingWithTriggers();
    };

    /// @note Vanilla's scalar deleting destructor (0x140abddb0) sits in hkbCharacter's TU, emitted there by
    ///       hkbCharacter::accessAnimationBindingSet's `new`; it calls the out-of-line destructor. No user-provided default
    ///       constructor: that `new hkbAnimationBindingSet()` value-initializes, zeroing the block first.
    struct hkbAnimationBindingSet : hkReferencedObject
    {
        /// @note Reference-holding raw pointers: clear releases each.
        hkArray<hkbAnimationBindingWithTriggers*> m_bindings;   ///< +0x10, indexed by hkbClipGenerator::m_animationBindingIndex

        /// @note Out of line in vanilla (0x140adf660, Behavior/Character/hkbAnimationBindingSet.cpp).
        virtual ~hkbAnimationBindingSet();   ///< slot 0

        /// @brief Releases every binding and empties the list (keeping its storage).
        /// @note Name provisional; out of line in vanilla (0x140adf6d0).
        void clear();
    };

    /// @note 0xc0 bytes.
    struct hkbCharacterStringData : hkReferencedObject
    {
        hkArray<hkStringPtr> m_deformableSkinNames;                  ///< +0x10
        hkArray<hkStringPtr> m_rigidSkinNames;                       ///< +0x20
        hkArray<hkStringPtr> m_animationNames;                       ///< +0x30
        hkArray<hkStringPtr> m_animationFilenames;                   ///< +0x40
        hkArray<hkStringPtr> m_characterPropertyNames;               ///< +0x50
        hkArray<hkStringPtr> m_retargetingSkeletonMapperFilenames;   ///< +0x60
        hkArray<hkStringPtr> m_lodNames;                             ///< +0x70
        hkArray<hkStringPtr> m_mirroredSyncPointSubstringsA;         ///< +0x80
        hkArray<hkStringPtr> m_mirroredSyncPointSubstringsB;         ///< +0x90
        hkStringPtr          m_name;                                 ///< +0xa0
        hkStringPtr          m_rigName;                              ///< +0xa8
        hkStringPtr          m_ragdollName;                          ///< +0xb0
        hkStringPtr          m_behaviorFilename;                     ///< +0xb8

        HK_FORCE_INLINE hkbCharacterStringData() {}
        /// @note Vanilla inlines it into the type registry's finish function; out of line here
        ///       (Behavior/Character/hkbCharacterStringData.cpp), it also brings the class's vtable and scalar deleting
        ///       destructor into that unit.
        hkbCharacterStringData(hkFinishLoadedObjectFlag flag);
    };

    class hkbMirroredSkeletonInfo : public hkReferencedObject
    {
    public:
        hkVector4        m_mirrorAxis;    ///< +0x10
        hkArray<hkInt16> m_bonePairMap;   ///< +0x20
    };

    /// @note 0xb0 bytes.
    struct hkbCharacterData : hkReferencedObject
    {
        /// @note 0x18 bytes.
        struct CharacterControllerInfo
        {
            hkReal                       m_capsuleHeight;              ///< +0x0
            hkReal                       m_capsuleRadius;              ///< +0x4
            hkUint32                     m_collisionFilterInfo;        ///< +0x8
            /// @note Havok's hkpCharacterControllerCinfo (physics; not declared here).
            hkRefPtr<hkReferencedObject> m_characterControllerCinfo;   ///< +0x10
        };

        CharacterControllerInfo           m_characterControllerInfo;   ///< +0x10
        hkVector4                         m_modelUpMS;                 ///< +0x30
        hkVector4                         m_modelForwardMS;            ///< +0x40
        hkVector4                         m_modelRightMS;              ///< +0x50
        hkArray<hkbVariableInfo>          m_characterPropertyInfos;    ///< +0x60
        hkArray<hkInt32>                  m_numBonesPerLod;            ///< +0x70
        hkRefPtr<hkbVariableValueSet>     m_characterPropertyValues;   ///< +0x80
        hkRefPtr<hkbFootIkDriverInfo>     m_footIkDriverInfo;          ///< +0x88
        hkRefPtr<hkbHandIkDriverInfo>     m_handIkDriverInfo;          ///< +0x90
        hkRefPtr<hkbCharacterStringData>  m_stringData;                ///< +0x98
        hkRefPtr<hkbMirroredSkeletonInfo> m_mirroredSkeletonInfo;      ///< +0xa0
        hkReal                            m_scale;                     ///< +0xa8
        hkInt16                           m_numHands;                  ///< +0xac
        hkInt16                           m_numFloatSlots;             ///< +0xae

        HK_FORCE_INLINE hkbCharacterData() {}
        /// @note Vanilla inlines it into the type registry's finish function; out of line here
        ///       (Behavior/Character/hkbCharacterData.cpp), it also brings the class's vtable and scalar deleting
        ///       destructor into that unit.
        hkbCharacterData(hkFinishLoadedObjectFlag flag);
    };

    /// @note 0x58 bytes.
    struct hkbCharacterSetup : hkReferencedObject
    {
        hkArray< hkRefPtr<hkaSkeletonMapper> > m_retargetingSkeletonMappers;          ///< +0x10
        hkRefPtr<hkaSkeleton>                  m_animationSkeleton;                   ///< +0x20
        hkRefPtr<hkaSkeletonMapper>            m_ragdollToAnimationSkeletonMapper;    ///< +0x28
        hkRefPtr<hkaSkeletonMapper>            m_animationToRagdollSkeletonMapper;    ///< +0x30
        hkRefPtr<hkbAnimationBindingSet>       m_animationBindingSet;                 ///< +0x38
        hkRefPtr<hkbCharacterData>             m_data;                                ///< +0x40
        /// @brief Built lazily by getMirroredSkeleton from the character data's mirroring info.
        hkRefPtr<hkaMirroredSkeleton>          m_mirroredSkeleton;                    ///< +0x48
        /// @note Name after the Havok SDK, unverified; released by the destructor.
        hkRefPtr<hkReferencedObject>           m_characterPropertyIdMap;              ///< +0x50

        /// @note Out of line in vanilla (0x140ae5970, Behavior/Character/hkbCharacterSetup.cpp); the body is empty.
        ~hkbCharacterSetup();

        /// @note Name provisional.
        const hkaSkeletonMapper* getRetargetingSkeletonMapper(const char* rigName);
        /// @brief Built lazily into +0x48 from the character data's mirroring info.
        hkaMirroredSkeleton* getMirroredSkeleton();
    };

    class hkpCdPointCollector;

    /// @brief Physics queries the behavior runtime makes.
    struct hkbPhysicsUtils
    {
        /// @brief Every body within `radius` of `position` (with `tolerance`) that collides with `collisionFilterInfo`, into
        ///        `collector`.
        /// @note Name, owner and parameter names provisional; out of line in vanilla (0x140ae1ae0). hkbCharacter passes
        ///       tolerance 0.001 and radius minus it.
        static void getClosestPoints(hkpWorld* world, hkUint32 collisionFilterInfo, const hkVector4& position, hkReal tolerance, hkReal radius,
                                     hkpCdPointCollector* collector);
    };

    struct hkbCharacter : hkReferencedObject
    {
        /// @brief The rigid-body property key holding the owning hkbCharacter.
        /// @note Name provisional (Skyrim's 0x1400).
        enum { PROPERTY_CHARACTER = 0x1400 };

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
        /// @note Callers read it as a plain pointer (hkRefPtr converts); hkbBehaviorGraph may be incomplete where they do.
        hkRefPtr<hkbBehaviorGraph>             m_behaviorGraph;               ///< +0x58
        hkRefPtr<hkbProjectData>               m_projectData;                 ///< +0x60
        hkRefPtr<hkbAnimationBindingSet>       m_animationBindingSet;         ///< +0x68
        hkaRaycastInterface*                   m_raycastInterface;            ///< +0x70
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
        hkaRaycastInterface* getRaycastInterface() const;
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
