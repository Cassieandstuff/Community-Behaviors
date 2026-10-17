/// @file
/// @brief BSLookAtModifier: Bethesda's head/eye tracking — turns a bone chain and the eye bones toward a target.
#pragma once

#include <Behavior/Context/hkbContext.h>
#include <Behavior/Event/hkbEvent.h>
#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace schema
{
    /// @brief One tracking bone (or eye bone) of a BSLookAtModifier.
    /// @note Member names are the class's reflected names. Sized 0x40.
    struct BSLookAtModifierBoneData
    {
        hkInt16   m_index;               ///< +0x00, -1 = unset
        hkVector4 m_fwdAxisLS;           ///< +0x10
        hkReal    m_limitAngleDegrees;   ///< +0x20
        hkReal    m_onGain;              ///< +0x24
        hkReal    m_offGain;             ///< +0x28
        hkBool    m_enabled;             ///< +0x2c
        /// @brief The forward axis the bone currently points along (bone space), eased toward the target each frame.
        hkVector4 m_currentFwdAxisLS;    ///< +0x30

        BSLookAtModifierBoneData();
        BSLookAtModifierBoneData(hkFinishLoadedObjectFlag flag);

        /// @brief Points the current forward axis back along the rest axis.
        /// @note Name provisional; out of line in vanilla (0x140b9d1e0), called by activate for every bone.
        void resetCurrentFwdAxis();
    };

    /// @note Member names are the class's reflected names. The `slot N` annotations are vanilla's vtable slots. The
    ///       destructor is implicit: vanilla's (0x140b9e210) stamps no vtable; the deleting destructor calls it out of line.
    class BSLookAtModifier : public hkbModifier
    {
    public:
        hkBool                            m_lookAtTarget;                 ///< +0x50
        hkArray<BSLookAtModifierBoneData> m_bones;                        ///< +0x58, the chain, root first; the last bone aims
        hkArray<BSLookAtModifierBoneData> m_eyeBones;                     ///< +0x68
        hkReal                            m_limitAngleDegrees;            ///< +0x78
        hkReal                            m_limitAngleThresholdDegrees;   ///< +0x7c
        hkBool                            m_continueLookOutsideOfLimit;   ///< +0x80
        hkReal                            m_onGain;                       ///< +0x84
        hkReal                            m_offGain;                      ///< +0x88
        hkBool                            m_useBoneGains;                 ///< +0x8c
        hkVector4                         m_targetLocation;               ///< +0x90, world space
        hkBool                            m_targetOutsideLimits;          ///< +0xa0
        hkbEventProperty                  m_targetOutOfLimitEvent;        ///< +0xa8
        hkBool                            m_lookAtCamera;                 ///< +0xb8
        hkReal                            m_lookAtCameraX;                ///< +0xbc
        hkReal                            m_lookAtCameraY;                ///< +0xc0
        hkReal                            m_lookAtCameraZ;                ///< +0xc4
        hkReal                            m_timeStep;                     ///< +0xc8, the last update's
        /// @brief Whether every bone and eye bone index is inside the skeleton (activate checks; modify needs it).
        hkBool                            m_ballBonesValid;               ///< +0xcc
        /// @note Nulled by the constructors and deactivate; nothing in the class or its Utils reads it.
        void*                             m_pSkeletonMemory;              ///< +0xd0

        BSLookAtModifier();
        /// @note Called by the class's packfile finish function (in the type-registration block).
        BSLookAtModifier(hkFinishLoadedObjectFlag flag);
        /// @brief The cloneNode copy: the settings and both bone lists, m_ballBonesValid left unset.
        BSLookAtModifier(const BSLookAtModifier& mod);

        static const hkClass& staticClass();

        virtual const void* getClassType() const;                                         ///< slot 1
        /// @brief Validates every bone index against the skeleton and resets the bones' current axes.
        virtual void        activate(const hkbContext& context);                          ///< slot 4
        virtual void        update(const hkbContext& context, hkReal timestep);           ///< slot 5
        virtual void        deactivate(const hkbContext& context);                        ///< slot 7
        virtual bool        isValid(hkStringPtr& errorString) const;                      ///< slot 10
        virtual hkbNode*    cloneNode(hkbBehaviorGraph& rootBehavior) const;              ///< slot 12
        /// @brief Loads the output's pose into an hkaPose on the thread's stack allocator and hands it to
        ///        BSLookAtModifierUtils::modifyInternal.
        virtual void        modify(const hkbContext& context, hkbGeneratorOutput& inOut); ///< slot 23
    };
    static_assert(sizeof(BSLookAtModifierBoneData) == 0x40, "BSLookAtModifierBoneData size");
    static_assert(sizeof(BSLookAtModifier) == 0xe0, "BSLookAtModifier size");
} }
