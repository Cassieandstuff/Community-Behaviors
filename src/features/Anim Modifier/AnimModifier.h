#pragma once

#include <Common/Base/hkBase.h>
#include <Behavior/Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Behavior/Context/hkbContext.h>
#include <Behavior/Behavior/Generator/Clip/hkbClipGenerator.h>
#include <Behavior/Behavior/Modifier/hkbModifier.h>

#include <platform/anim/AnimationFiles.h>

/// @file
/// @brief CBAnimModifier, CB's own Havok modifier: conditional animations, CB's replacement for OAR. The layout is
///        schema/modifiers/CBAnimModifier.yaml's (and CBAnimModifierRule.yaml's); the platform registers the class from
///        that descriptor (platform/classes) and checks the sizes agree.
/// @note  Why a modifier with no clip hook: a modifier generator lists [generator, modifier], and the graph activates a
///        node, then its children in that order, so the clip's subtree has activated (with its own animation) just
///        before this node's activate, with nothing in between. This node then plays its rule's animation on the clip
///        in place, as BSSynchronizedClipGenerator re-points its clip. The clip keeps its own binding index and its own
///        load for its whole life, so its deactivate undoes the swap; this node owns only its own file.
/// @note  The clip deactivates first (the active-node array is leaf-first) and, in an update pass, its clone is
///        released right after: deactivate touches the clip only while it still plays this node's binding.

namespace CB::feature::replacer {

    /// @brief CBAnimModifierRule (32 bytes).
    struct AnimModifierRule {
        HK_DECLARE_POD_TYPE();

        ::hkStringPtr m_name;        ///< +0x00 the rule's key
        ::hkStringPtr m_when;        ///< +0x08 condition expression; empty always holds
        ::hkStringPtr m_animation;   ///< +0x10 the file from Data, byte-exact
        ::hkInt32     m_priority;    ///< +0x18
        ::hkInt32     m_unused;      ///< +0x1c
    };
    static_assert(sizeof(AnimModifierRule) == 32, "AnimModifierRule size (schema: 32)");

    class AnimModifier : public ::hkbModifier {
    public:
        /// @note Serialized members have no default initializer: the finish constructor must leave the loaded bytes.
        ::hkbClipGenerator*            m_clip;              ///< +0x50 the clip's TEMPLATE (clones copy the pointer)
        ::hkArray<AnimModifierRule>    m_rules;             ///< +0x58 the template's; a clone's stays empty
        platform::anim::LoadedAnimation m_playing;          ///< +0x68 runtime: the file the clip plays, or none
        ::hkInt32                      m_rule  = -1;        ///< +0x78 runtime: the rule playing or waiting to load
        ::hkUint32                     m_state = 0;         ///< +0x7c runtime: State bits

        enum State : ::hkUint32 {
            kPending = 1,   ///< the rule's file was streaming in at activate; update swaps it in once loaded
        };

        /// @brief The finish constructor: the vtable on loaded bytes; serialized members stay as loaded.
        explicit AnimModifier(::hkFinishLoadedObjectFlag flag);
        /// @brief The cloneNode copy: the clip pointer, no rules (a clone reads its template's), fresh runtime state.
        AnimModifier(const AnimModifier& other);
        ~AnimModifier() override;

        const ::hkClass* getClassType() const override;
        /// @brief Picks the rule (highest priority whose `when` holds) and plays its animation on the clip, which has
        ///        just activated with its own; none = the clip plays its own.
        void             activate(const ::hkbContext& context) override;
        /// @brief Swaps a pending file in once it has loaded, while the clip is still near its start.
        /// @note  Overridden even apart from that: hkbNode's own marks the node update-free, and the graph then skips
        ///        copying bound variables into it.
        void             update(const ::hkbContext& context, ::hkReal timestep) override;
        /// @brief Releases the file; puts the clip back on its own binding if it is still active and playing this one.
        void             deactivate(const ::hkbContext& context) override;
        bool             isValid(::hkStringPtr& errorString) const override;
        ::hkbNode*       cloneNode(::hkbBehaviorGraph& rootBehavior) const override;

    private:
        /// @return the active clone of m_clip, null when the clip is not active in this character's graph.
        ::hkbClipGenerator* ActiveClip(const ::hkbContext& context) const;
        /// @return the node whose rules apply: the template for a clone, else this node.
        const AnimModifier* Template(const ::hkbContext& context) const;
        /// @brief Plays the pending rule's file on `clip` if it has loaded; clears kPending once it has, or gives up.
        void                TrySwap(const ::hkbContext& context, ::hkbClipGenerator& clip, bool atActivate);
    };
    static_assert(sizeof(AnimModifier) == 0x80, "AnimModifier size (schema: 128)");

}  // namespace CB::feature::replacer
