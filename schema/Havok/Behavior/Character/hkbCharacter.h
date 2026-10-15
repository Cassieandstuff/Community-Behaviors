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
    struct hkbEventQueue;

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
        hkVector4 m_modelUpMS;   ///< +0x30
        char      m_pad40[0xa8 - 0x40];
        hkReal    m_scale;       ///< +0xa8
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
        char                             m_pad24[0x50 - 0x24];
        hkRefPtr<hkbCharacterSetup>      m_setup;                 ///< +0x50
        hkbBehaviorGraph*                m_behaviorGraph;         ///< +0x58
        char                             m_pad60[0x68 - 0x60];
        hkRefPtr<hkbAnimationBindingSet> m_animationBindingSet;   ///< +0x68
        char                             m_pad70[0x80 - 0x70];
        hkbEventQueue*                   m_eventQueue;            ///< +0x80
        hkQsTransform*                   m_worldFromModel;        ///< +0x88

        /// @brief The character's own binding set, else its setup's.
        HK_FORCE_INLINE hkbAnimationBindingSet* getAnimationBindingSet() const
        {
            return m_animationBindingSet ? m_animationBindingSet.val() : (m_setup ? m_setup->m_animationBindingSet.val() : HK_NULL);
        }
    };
} }
