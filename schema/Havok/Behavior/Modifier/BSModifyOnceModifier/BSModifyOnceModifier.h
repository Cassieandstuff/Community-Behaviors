/// @file
/// @brief BSModifyOnceModifier: Bethesda's modifier that runs one modifier through a single activate/update/deactivate
///        cycle when it activates, and another when it deactivates.
#pragma once

#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace schema
{
    /// @note Member names are the class's reflected names. The `slot N` annotations are vanilla's vtable slots. The
    ///       destructor is out of line in vanilla (the scalar deleting destructor, slot 0, calls it).
    class BSModifyOnceModifier : public hkbModifier
    {
    public:
        /// @note Raw pointers, not reference-counted: a clone owns the clones it made of the template's modifiers and
        ///       deletes them (cloneState CLONE_STATE_CLONE); a template's are the graph's.
        hkbModifier* m_pOnActivateModifier;     ///< +0x50
        hkUint8      m_pad58[8];                ///< +0x58 — untouched by every function
        hkbModifier* m_pOnDeactivateModifier;   ///< +0x60
        hkUint8      m_pad68[8];                ///< +0x68 — untouched by every function

        /// @note Every constructor sets bits 0 and 1 of m_userData; what they flag is unknown.
        BSModifyOnceModifier();
        BSModifyOnceModifier(hkFinishLoadedObjectFlag flag);
        /// @brief The cloneNode copy: clones both modifiers into the root behavior.
        BSModifyOnceModifier(const BSModifyOnceModifier& mod, hkbBehaviorGraph& rootBehavior);
        ~BSModifyOnceModifier();

        virtual void     activate(const hkbContext& context);                                  ///< slot 4
        virtual void     deactivate(const hkbContext& context);                                ///< slot 7
        virtual int      getMaxNumChildren(GetChildrenFlags flags);                            ///< slot 8
        virtual void     getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo);      ///< slot 9
        virtual bool     isValid(hkStringPtr& errorString) const;                              ///< slot 10
        virtual void     precompute(const hkbContext& context);                                ///< slot 11
        virtual hkbNode* cloneNode(hkbBehaviorGraph& rootBehavior) const;                      ///< slot 12
    };
} }
