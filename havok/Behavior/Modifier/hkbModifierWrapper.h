/// @file
/// @brief hkbModifierWrapper: the base of the modifiers that run one child modifier under a condition
///        (hkbDelayedModifier, hkbEventDrivenModifier).
#pragma once

#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace havok
{
    /// @note The `slot N` annotations are vanilla's vtable slots. No getClassType of its own (slot 1 stays
    ///       hkReferencedObject's, in its subclass hkbDelayedModifier too). The destructor is implicit: vanilla's scalar
    ///       deleting destructor (slot 0) stamps no vtable. Both constructors are out of line: the subclasses call them.
    class hkbModifierWrapper : public hkbModifier
    {
    public:
        hkRefPtr<hkbModifier> m_modifier;   ///< +0x50

        hkbModifierWrapper();
        /// @brief The cloneNode copy: shares the child template (the clones are looked up per graph).
        hkbModifierWrapper(const hkbModifierWrapper& mod);

        virtual void     getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo);   ///< slot 9
        virtual bool     isValid(hkStringPtr& errorString) const;                           ///< slot 10
        /// @brief Null: the wrapper itself is never cloned, only its subclasses.
        virtual hkbNode* cloneNode(hkbBehaviorGraph& rootBehavior) const;                   ///< slot 12

        void setModifier(hkbModifier* modifier);
    };
} }
