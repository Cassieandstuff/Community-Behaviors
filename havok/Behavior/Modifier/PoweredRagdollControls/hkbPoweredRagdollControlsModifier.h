/// @file
/// @brief hkbPoweredRagdollControlsModifier: hands the ragdoll driver powered (motor) controls for some bones.
#pragma once

#include <Behavior/Driver/Ragdoll/hkbPoweredRagdollControlData.h>
#include <Behavior/Driver/Ragdoll/hkbRagdollDriver.h>
#include <Behavior/Utils/hkbBoneWeightArray.h>
#include <Behavior/Modifier/hkbModifier.h>
#include <Behavior/Utils/hkbBoneIndexArray.h>

namespace CB { namespace havok
{
    /// @note Sized 0x90. Member names are the reflected ones; the `slot N` annotations are vanilla's vtable slots. The
    ///       destructor is implicit: the deleting destructor (0x140aca790) inlines it.
    class hkbPoweredRagdollControlsModifier : public hkbModifier
    {
    public:
        hkbPoweredRagdollControlData  m_controlData;              ///< +0x50
        hkRefPtr<hkbBoneIndexArray>   m_bones;                    ///< +0x70
        hkbWorldFromModelModeData     m_worldFromModelModeData;   ///< +0x78
        /// @note Deprecated in favor of m_bones (isValid's message); the two exclude each other.
        hkRefPtr<hkbBoneWeightArray>  m_boneWeights;              ///< +0x80

        hkbPoweredRagdollControlsModifier();
        /// @brief The cloneNode copy: bone lists without bindings are shared, bound ones copied.
        hkbPoweredRagdollControlsModifier(const hkbPoweredRagdollControlsModifier& mod);

        virtual void     collectBindables(hkbBindableCollector& collector);   ///< slot 3
        virtual bool     isValid(hkStringPtr& errorString) const;             ///< slot 10
        /// @note Shared, not copied, when neither it nor its bone lists are bound.
        virtual hkbNode* cloneNode(hkbBehaviorGraph& rootBehavior) const;     ///< slot 12
        virtual void     modify(const hkbContext& context, hkbGeneratorOutput& inOut);   ///< slot 23
    };
} }
