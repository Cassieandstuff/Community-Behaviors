/// @file
/// @brief hkbRigidBodyRagdollControlsModifier: hands the ragdoll driver keyframe-hierarchy (rigid body) controls for some
///        bones.
#pragma once

#include <Behavior/Driver/Ragdoll/hkbRigidBodyRagdollControlData.h>
#include <Behavior/Modifier/hkbModifier.h>
#include <Behavior/Utils/hkbBoneIndexArray.h>

namespace CB { namespace havok
{
    /// @note Sized 0xa0. Member names are the reflected ones; the `slot N` annotations are vanilla's vtable slots. The
    ///       destructor is implicit: the deleting destructor (0x140acac10) inlines it.
    class hkbRigidBodyRagdollControlsModifier : public hkbModifier
    {
    public:
        hkbRigidBodyRagdollControlData m_controlData;   ///< +0x50
        /// @brief The bones controlled; all of them when null.
        hkRefPtr<hkbBoneIndexArray>    m_bones;         ///< +0x90

        hkbRigidBodyRagdollControlsModifier();
        /// @brief The cloneNode copy: a bone list without bindings is shared, a bound one copied.
        hkbRigidBodyRagdollControlsModifier(const hkbRigidBodyRagdollControlsModifier& mod);

        virtual void     collectBindables(hkbBindableCollector& collector);   ///< slot 3
        /// @note Shared, not copied, when neither it nor its bone list is bound.
        virtual hkbNode* cloneNode(hkbBehaviorGraph& rootBehavior) const;     ///< slot 12
        virtual void     modify(const hkbContext& context, hkbGeneratorOutput& inOut);   ///< slot 23
    };
} }
