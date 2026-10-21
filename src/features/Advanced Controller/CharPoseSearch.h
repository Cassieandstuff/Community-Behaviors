#pragma once

#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Context/hkbContext.h>
#include <Behavior/Generator/hkbGenerator.h>

#include <cstddef>

/// @file
/// @brief CBPoseSearchGenerator, CB's own Havok generator (motion matching, stage A): it plays the one child
///        `m_selectedIndex` names, as a stock manual selector does, but with 32-bit indices.
/// @note  Why not the stock selector: hkbManualSelectorGenerator's indices are int8, and it clamps the selection to
///        (int8)(children - 1); with the add-on's 252 clips that is -5, so every selection read past its children
///        (the first in-game run crashed there). The layout is schema/generators/CBPoseSearchGenerator.yaml's: the
///        platform registers the class from that descriptor (platform/classes) and checks the sizes agree.

namespace CB::feature::controller::motion {

    class PoseSearchGenerator : public CB::havok::hkbGenerator {
    public:
        /// @note Each child holds a reference (raw pointers: the constructors and the destructor count them by hand).
        CB::havok::hkArray<CB::havok::hkbGenerator*> m_generators;   ///< +0x48
        CB::havok::hkInt32                           m_selectedIndex;  ///< +0x58, bound to the add-on's MM_Clip
        CB::havok::hkInt32                           m_currentIndex;   ///< +0x5c, the child playing, clamped into range

        /// @brief The finish constructor: the vtable on loaded bytes; serialized members stay as loaded.
        explicit PoseSearchGenerator(CB::havok::hkFinishLoadedObjectFlag flag);
        /// @brief The cloneNode copy: shares the children (each gains a reference), the current index restarted.
        PoseSearchGenerator(const PoseSearchGenerator& other);
        ~PoseSearchGenerator() override;

        const void*          getClassType() const override;
        void                 activate(const CB::havok::hkbContext& context) override;
        /// @brief Follows the selected index; a switch to another child asks the root graph to recompute its active nodes.
        void                 update(const CB::havok::hkbContext& context, CB::havok::hkReal timestep) override;
        int                  getMaxNumChildren(GetChildrenFlags flags) override;
        void                 getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo) override;
        bool                 isValid(CB::havok::hkStringPtr& errorString) const override;
        CB::havok::hkbNode*  cloneNode(CB::havok::hkbBehaviorGraph& rootBehavior) const override;
        void                 generate(const CB::havok::hkbContext& context, const CB::havok::hkbGeneratorOutput** activeChildrenOutput,
                                      CB::havok::hkbGeneratorOutput& output, CB::havok::hkReal timeOffset) override;
        bool                 canRecycleOutput() const override;
        void                 updateSync(const CB::havok::hkbContext& context, CB::havok::hkbNodeInfo& info) override;

    private:
        /// @brief m_selectedIndex clamped into [0, last child].
        int computeCurrentIndex() const;
    };
    static_assert(sizeof(PoseSearchGenerator) == 0x60, "PoseSearchGenerator size (schema: 96)");

}  // namespace CB::feature::controller::motion
