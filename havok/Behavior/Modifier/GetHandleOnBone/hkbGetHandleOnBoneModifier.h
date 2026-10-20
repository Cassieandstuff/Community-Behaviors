/// @file
/// @brief hkbGetHandleOnBoneModifier: on activation, points m_handleOut at a ragdoll bone's rigid body or an animation
///        bone (or a named local frame on either).
#pragma once

#include <Behavior/Context/hkbContext.h>
#include <Behavior/Modifier/hkbModifier.h>
#include <Behavior/Utils/hkbHandle.h>

namespace CB { namespace havok
{
    /// @note Sized 0x68. Member names are the reflected ones; the `slot N` annotations are vanilla's vtable slots. The
    ///       destructor is implicit: the deleting destructor (0x140ac95f0) inlines it.
    class hkbGetHandleOnBoneModifier : public hkbModifier
    {
    public:
        hkRefPtr<hkbHandle> m_handleOut;            ///< +0x50
        hkStringPtr         m_localFrameName;       ///< +0x58
        hkInt16             m_ragdollBoneIndex;     ///< +0x60
        hkInt16             m_animationBoneIndex;   ///< +0x62

        hkbGetHandleOnBoneModifier();
        /// @brief The cloneNode copy: the settings, without the output handle.
        hkbGetHandleOnBoneModifier(const hkbGetHandleOnBoneModifier& mod);

        virtual void     activate(const hkbContext& context);                ///< slot 4
        virtual void     deactivate(const hkbContext& context);              ///< slot 7
        virtual bool     isValid(hkStringPtr& errorString) const;            ///< slot 10
        virtual hkbNode* cloneNode(hkbBehaviorGraph& rootBehavior) const;    ///< slot 12
    };
} }
