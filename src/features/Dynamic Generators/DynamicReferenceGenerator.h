#pragma once

#include <hky/layout/CBDynamicBehaviorReferenceGenerator.h>
#include <Common/Base/hkBase.h>
#include <Behavior/Behavior/Generator/hkbGenerator.h>

#include <cstddef>

/// @file
/// @brief CBDynamicBehaviorReferenceGenerator: a behavior reference whose behavior the binding registry names per actor.
///        The layout is schema/generators/CBDynamicBehaviorReferenceGenerator.yaml's.

namespace CB::feature::dynamic {

    class DynamicReferenceGenerator : public ::hkbGenerator {
    public:
        ::hkStringPtr   m_key;        ///< +0x48, byte-exact
        ::hkbGenerator* m_fallback;   ///< +0x50, the loaded file's (never referenced or freed here)
        /// @brief +0x58, runtime: this clone's hold on its binding (a Bindings ticket), 0 when it holds none.
        ::hkUint64      m_ticket;

        explicit DynamicReferenceGenerator(::hkFinishLoadedObjectFlag flag);
        /// @brief The cloneNode copy: each character's clone resolves its own actor's binding.
        DynamicReferenceGenerator(const DynamicReferenceGenerator& other);
        ~DynamicReferenceGenerator() override;

        const ::hkClass* getClassType() const override;
        /// @brief Resolve the binding: the bound behavior, else the fallback, else nothing. Logged either way.
        void             activate(const ::hkbContext& context) override;
        void             deactivate(const ::hkbContext& context) override;
        int              getMaxNumChildren(GetChildrenFlags flags) override;
        void             getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo) override;
        bool             isValid(::hkStringPtr& errorString) const override;
        ::hkbNode*       cloneNode(::hkbBehaviorGraph& rootBehavior) const override;
        void             generate(const ::hkbContext& context, const ::hkbGeneratorOutput** activeChildrenOutput,
                                  ::hkbGeneratorOutput& output, ::hkReal timeOffset) override;
        /// @brief +0x60, runtime: what this clone runs now: the bound behavior, the fallback, or null.
        ::hkbGenerator* m_running;
    };
    HKY_LAYOUT_CHECK(DynamicReferenceGenerator, hky::layout::CBDynamicBehaviorReferenceGenerator);
    static_assert(offsetof(DynamicReferenceGenerator, m_key) == 0x48);

}  // namespace CB::feature::dynamic
