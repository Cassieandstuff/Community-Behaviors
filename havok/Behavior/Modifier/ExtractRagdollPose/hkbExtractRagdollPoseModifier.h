/// @file
/// @brief hkbExtractRagdollPoseModifier: replaces the output pose with the ragdoll's simulated pose.
#pragma once

#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace havok
{
    class hkStatisticsCollector;

    /// @note Sized 0x58. Member names are the ones isValid looks the bindings up by; the `slot N` annotations are
    ///       vanilla's vtable slots. The destructor is implicit: the deleting destructor (0x140ac9260) inlines it.
    class hkbExtractRagdollPoseModifier : public hkbModifier
    {
    public:
        hkInt16 m_poseMatchingBone0;             ///< +0x50
        hkInt16 m_poseMatchingBone1;             ///< +0x52
        hkInt16 m_poseMatchingBone2;             ///< +0x54
        hkBool  m_enableComputeWorldFromModel;   ///< +0x56

        hkbExtractRagdollPoseModifier();
        hkbExtractRagdollPoseModifier(const hkbExtractRagdollPoseModifier& mod);

        virtual void     calcContentStatistics(hkStatisticsCollector* collector, const hkClass* cls) const;   ///< slot 2
        /// @note The pose matching bones may be unset (-1) when a variable binds them; they only matter when the
        ///       world-from-model is computed.
        virtual bool     isValid(hkStringPtr& errorString) const;            ///< slot 10
        /// @note Shared, not copied, when nothing is bound.
        virtual hkbNode* cloneNode(hkbBehaviorGraph& rootBehavior) const;    ///< slot 12
        /// @brief Has the character's ragdoll driver extract the ragdoll's pose into `inOut`, once the ragdoll is in a
        ///        physics world.
        virtual void     modify(const hkbContext& context, hkbGeneratorOutput& inOut);   ///< slot 23
    };
} }
