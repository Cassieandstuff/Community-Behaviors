#pragma once

#include <Common/Base/hkBase.h>
#include <Behavior/Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Behavior/Context/hkbContext.h>
#include <Behavior/Behavior/Modifier/hkbModifier.h>

/// @file
/// @brief CBDamageModifier, CB's own Havok modifier: while it is active and enabled, its actor takes hits, magic,
///        stagger and health damage as its members say (i-frames, super armor, damage scaling). The layout is
///        schema/modifiers/CBDamageModifier.yaml's: the platform registers the class from that descriptor
///        (platform/classes) and checks the sizes agree.
/// @note  Why a modifier and not a graph variable the hooks read: the effect lasts exactly while its part of the graph
///        runs. A variable set by an event stays set when the clip is cut before its clearing event; a modifier's
///        deactivation always ends it. Event-timed windows wrap it in an hkbEventDrivenModifier.

namespace CB::feature::combat {

    class DamageModifier : public ::hkbModifier {
    public:
        ::hkBool m_ignoreHits;            ///< +0x50
        ::hkBool m_ignoreMagic;           ///< +0x51
        ::hkReal m_incomingStaggerMult;   ///< +0x54
        ::hkReal m_incomingDamageMult;    ///< +0x58

        /// @brief The finish constructor: the vtable on loaded bytes; serialized members stay as loaded.
        explicit DamageModifier(::hkFinishLoadedObjectFlag flag);
        DamageModifier(const DamageModifier& other);
        /// @note Retracts the node from the active table: a graph torn down while the node is active destroys its clone
        ///       without deactivating it.
        ~DamageModifier() override;

        const ::hkClass*          getClassType() const override;
        void                 activate(const ::hkbContext& context) override;
        /// @brief Republishes the members, which bindings may have changed since the last frame.
        /// @note Overridden even if it did nothing: hkbNode's own marks the node update-free, and the graph then skips
        ///       copying bound variables into it.
        void                 update(const ::hkbContext& context, ::hkReal timestep) override;
        void                 deactivate(const ::hkbContext& context) override;
        bool                 isValid(::hkStringPtr& errorString) const override;
        ::hkbNode*  cloneNode(::hkbBehaviorGraph& rootBehavior) const override;
    };
    static_assert(sizeof(DamageModifier) == 0x60, "DamageModifier size (schema: 96)");

}  // namespace CB::feature::combat
