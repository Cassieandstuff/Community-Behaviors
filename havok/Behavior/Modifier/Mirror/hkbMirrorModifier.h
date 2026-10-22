/// @file
/// @brief hkbMirrorModifier: mirrors the pose, the world-from-model motion and the sync points of its subtree.
#pragma once

#include <Behavior/Context/hkbContext.h>
#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace havok
{
    /// @note The `slot N` annotations are vanilla's vtable slots. The destructor is implicit: vanilla's scalar deleting
    ///       destructor (slot 0) inlines it.
    class hkbMirrorModifier : public hkbModifier
    {
    public:
        hkBool m_isAdditive;   ///< +0x50 — the pose is additive (mirrored without the reference pose)

        hkbMirrorModifier();
        /// @brief The cloneNode copy.
        hkbMirrorModifier(const hkbMirrorModifier& mod);

        /// @note An unbound modifier is shared, not copied: it marks itself CLONE_STATE_SHARABLE and returns itself with
        ///       a reference added.
        virtual hkbNode* cloneNode(hkbBehaviorGraph& rootBehavior) const;                                ///< slot 12
        virtual void     modify(const hkbContext& context, hkbGeneratorOutput& inOut);                   ///< slot 23
        virtual void     updateSync(const hkbContext& context, hkbNodeInfo& info);                         ///< slot 24
    };
} }
