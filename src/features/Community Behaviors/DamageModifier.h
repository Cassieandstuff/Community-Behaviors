#pragma once

#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Context/hkbContext.h>
#include <Behavior/Modifier/hkbModifier.h>

/// @file
/// @brief CBDamageModifier, CB's own Havok modifier: while it is active and enabled, its actor takes hits, magic,
///        stagger and health damage as its members say (i-frames, super armor, damage scaling). The layout is
///        schema/modifiers/CBDamageModifier.yaml's: the platform registers the class from that descriptor
///        (platform/classes) and checks the sizes agree.
/// @note  Why a modifier and not a graph variable the hooks read: the effect lasts exactly while its part of the graph
///        runs. A variable set by an event stays set when the clip is cut before its clearing event; a modifier's
///        deactivation always ends it. Event-timed windows wrap it in an hkbEventDrivenModifier.

namespace CB::feature::combat {

    class DamageModifier : public CB::havok::hkbModifier {
    public:
        CB::havok::hkBool m_ignoreHits;            ///< +0x50
        CB::havok::hkBool m_ignoreMagic;           ///< +0x51
        CB::havok::hkReal m_incomingStaggerMult;   ///< +0x54
        CB::havok::hkReal m_incomingDamageMult;    ///< +0x58

        /// @brief The finish constructor: the vtable on loaded bytes; serialized members stay as loaded.
        explicit DamageModifier(CB::havok::hkFinishLoadedObjectFlag flag);
        DamageModifier(const DamageModifier& other);
        /// @note Retracts the node from the active table: a graph torn down while the node is active destroys its clone
        ///       without deactivating it.
        ~DamageModifier() override;

        const void*          getClassType() const override;
        void                 activate(const CB::havok::hkbContext& context) override;
        /// @brief Republishes the members, which bindings may have changed since the last frame.
        /// @note Overridden even if it did nothing: hkbNode's own marks the node update-free, and the graph then skips
        ///       copying bound variables into it.
        void                 update(const CB::havok::hkbContext& context, CB::havok::hkReal timestep) override;
        void                 deactivate(const CB::havok::hkbContext& context) override;
        bool                 isValid(CB::havok::hkStringPtr& errorString) const override;
        CB::havok::hkbNode*  cloneNode(CB::havok::hkbBehaviorGraph& rootBehavior) const override;
    };
    static_assert(sizeof(DamageModifier) == 0x60, "DamageModifier size (schema: 96)");

}  // namespace CB::feature::combat
