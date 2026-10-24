/// @file
/// @brief hkbGetUpModifier: blends a character getting up from a ragdoll — aligns the world-from-model with the ragdoll's
///        pose on its first modify, then turns the character's up axis back to the ground normal (or the world's up).
#pragma once

#include <Behavior/Context/hkbContext.h>
#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace havok
{
    /// @note 0x20 bytes; member names after the Havok SDK, unverified beyond the size.
    struct hkbGetUpModifierInternalState : hkReferencedObject
    {
        hkReal m_timeSinceBegin;   ///< +0x10
        hkReal m_timeStep;         ///< +0x14
        hkBool m_initNextModify;   ///< +0x18

        HK_FORCE_INLINE hkbGetUpModifierInternalState() {}
        /// @note Vanilla inlines it into the type registry's finish function; out of line here, it also brings the
        ///       class's vtable and scalar deleting destructor into hkbGetUpModifier.cpp.
        hkbGetUpModifierInternalState(hkFinishLoadedObjectFlag flag);
    };

    /// @note Layout from the schema (schema/modifiers/hkbGetUpModifier.yaml, 0x80 bytes); member names are the reflected ones.
    ///       The `slot N` annotations are vanilla's vtable slots. The destructor is implicit.
    class hkbGetUpModifier : public hkbModifier
    {
    public:
        /// @brief The up axis to align with first (world space); zero = the world's up from the start.
        hkVector4 m_groundNormal;              ///< +0x50
        /// @brief Seconds to turn the character's up axis to the world's up.
        hkReal    m_duration;                  ///< +0x60
        /// @brief Seconds to turn it to m_groundNormal before that.
        hkReal    m_alignWithGroundDuration;   ///< +0x64
        /// @brief The three ragdoll bones whose pose fixes the reference frame (hkaPoseMatchingUtility's).
        hkInt16   m_rootBoneIndex;             ///< +0x68
        hkInt16   m_otherBoneIndex;            ///< +0x6a
        hkInt16   m_anotherBoneIndex;          ///< +0x6c
        hkReal    m_timeSinceBegin;            ///< +0x70
        hkReal    m_timeStep;                  ///< +0x74, the last update's (zeroed by modify)
        /// @brief Set by activate: the next modify snaps the world-from-model onto the ragdoll's reference frame.
        hkBool    m_initNextModify;            ///< +0x78

        /// @note Not in the inventory (only .pdata references it); the TU's first function.
        hkbGetUpModifier();
        /// @brief The cloneNode copy: the settings, with the run state reset.
        hkbGetUpModifier(const hkbGetUpModifier& mod);

        virtual void                activate(const hkbContext& context);                              ///< slot 4
        virtual void                update(const hkbContext& context, hkReal timestep);               ///< slot 5
        virtual bool                isValid(hkStringPtr& errorString) const;                         ///< slot 10
        virtual hkbNode*            cloneNode(hkbBehaviorGraph& rootBehavior) const;                  ///< slot 12
        virtual hkReferencedObject* createInternalState();                                            ///< slot 13
        virtual void                getInternalState(hkReferencedObject& internalState) const;       ///< slot 14
        virtual void                setInternalState(const hkReferencedObject& internalState);       ///< slot 16
        virtual void                modify(const hkbContext& context, hkbGeneratorOutput& inOut);     ///< slot 23
    };
} }
