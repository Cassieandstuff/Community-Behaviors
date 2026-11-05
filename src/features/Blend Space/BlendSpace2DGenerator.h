#pragma once

#include <Common/Base/hkBase.h>
#include <Behavior/Behavior/Generator/Blender/hkbBlenderGenerator.h>

/// @file
/// @brief CBBlendSpace2DGenerator: a stock blender that takes its child weights from a point on a plane. The layout is
///        schema/generators/CBBlendSpace2DGenerator.yaml's; the platform registers the class from that descriptor.
/// @note  Only the weights are CB's. The graph generates the active children and hands the outputs to the blender's own
///        generate, and its updateActiveChildren, updateSync and computeChildWeight read what this writes.

namespace CB::feature::blendspace {

    class BlendSpace2DGenerator : public ::hkbBlenderGenerator {
    public:
        ::hkReal m_blendX;   ///< +0xa0, usually bound to a variable
        ::hkReal m_blendY;   ///< +0xa4, usually bound to a variable

        /// @brief The finish constructor: the vtable on loaded bytes; serialized members stay as loaded.
        explicit BlendSpace2DGenerator(::hkFinishLoadedObjectFlag flag);
        /// @brief The cloneNode copy, which is what every character runs: the engine clones each active node per character.
        /// @note  The base's copy builds plain children from the named members alone, so `x` and `y` are copied here.
        BlendSpace2DGenerator(const BlendSpace2DGenerator& other);

        const ::hkClass* getClassType() const override;
        /// @note The graph has synced this node's variables by now, so the first weights are already right.
        void             activate(const ::hkbContext& context) override;
        void             update(const ::hkbContext& context, ::hkReal timestep) override;
        bool             isValid(::hkStringPtr& errorString) const override;
        ::hkbNode*       cloneNode(::hkbBehaviorGraph& rootBehavior) const override;

    private:
        /// @brief Write each child's m_weight from where (m_blendX, m_blendY) falls among the children.
        /// @note  The children are this clone's own, so another character's blend never sees these writes.
        void computeWeights();
    };
    static_assert(sizeof(BlendSpace2DGenerator) == 168, "BlendSpace2DGenerator size (schema: 168)");

}  // namespace CB::feature::blendspace
