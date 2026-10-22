/// @file
/// @brief hkbKeyframeBonesModifier: keyframes ragdoll bones to targets (or to the animation) through the ragdoll driver.
#pragma once

#include <Behavior/Modifier/hkbModifier.h>
#include <Behavior/Utils/hkbBoneIndexArray.h>

namespace CB { namespace havok
{
    class hkStatisticsCollector;
    extern const hkClass hkbKeyframeBonesModifierClass;

    /// @note Sized 0x68. Member names are the reflected ones; the `slot N` annotations are vanilla's vtable slots. The
    ///       destructor is implicit and, as in vanilla (0x140ac6c10, among the type registry's functions), the deleting
    ///       destructor (0x140ac9ca0) calls it out of line.
    class hkbKeyframeBonesModifier : public hkbModifier
    {
    public:
        /// @brief One keyframed bone and its target (reflected as hkbKeyframeBonesModifierKeyframeInfo, 0x30 bytes).
        struct KeyframeInfo
        {
            hkVector4    m_keyframedPosition;   ///< +0x00
            hkQuaternion m_keyframedRotation;   ///< +0x10
            hkInt16      m_boneIndex;           ///< +0x20
            /// @brief Whether the target is set; activate clears every one.
            hkBool       m_isValid;             ///< +0x22
        };

        hkArray<KeyframeInfo>       m_keyframeInfo;         ///< +0x50
        /// @brief Bones keyframed to the animation (no target of their own).
        hkRefPtr<hkbBoneIndexArray> m_keyframedBonesList;   ///< +0x60

        /// @brief The cloneNode copy: a bone list without bindings is shared, a bound one copied.
        hkbKeyframeBonesModifier(const hkbKeyframeBonesModifier& mod);

        virtual void     calcContentStatistics(hkStatisticsCollector* collector, const hkClass* cls) const;   ///< slot 2
        virtual void     collectBindables(hkbBindableCollector& collector);   ///< slot 3
        /// @brief Invalidates every keyframe target.
        virtual void     activate(const hkbContext& context);                 ///< slot 4
        /// @brief Every bone (from both lists, unless bound) must be set (not -1) and appear once.
        virtual bool     isValid(hkStringPtr& errorString) const;             ///< slot 10
        /// @note Shared, not copied, when neither it nor its bone list is bound.
        virtual hkbNode* cloneNode(hkbBehaviorGraph& rootBehavior) const;     ///< slot 12
        virtual void     modify(const hkbContext& context, hkbGeneratorOutput& inOut);   ///< slot 23

        /// @brief Adds `boneIndex` to the keyframed bones list, creating the list on first use.
        /// @note Name provisional (0x140afc390; no direct caller in the exe).
        void addKeyframedBone(int boneIndex);
    };
} }
