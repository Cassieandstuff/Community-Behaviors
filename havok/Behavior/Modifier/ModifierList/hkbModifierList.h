/// @file
/// @brief hkbModifierList: runs a list of modifiers in order, as one modifier.
#pragma once

#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace havok
{
    extern const hkClass hkbModifierListClass;

    /// @note The `slot N` annotations are vanilla's vtable slots. The destructor is out of line and stamps the vtable
    ///       (user-declared); vanilla's scalar deleting destructor (slot 0) calls it.
    class hkbModifierList : public hkbModifier
    {
    public:
        /// @brief Referenced by hand (addReference on copy, removeReference on destruction), not through hkRefPtr.
        hkArray<hkbModifier*> m_modifiers;   ///< +0x50

        /// @brief The cloneNode copy: shares (and references) the child templates.
        hkbModifierList(const hkbModifierList& mod);
        virtual ~hkbModifierList();                                                          ///< slot 0

        /// @note Out of line among the type-registration getters (0x140ac4790); hkbModifierGenerator::updateSync calls it.
        static const hkClass& staticClass();

        virtual const void* getClassType() const;                                           ///< slot 1
        virtual int         getMaxNumChildren(GetChildrenFlags flags);                      ///< slot 8
        virtual void        getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo);   ///< slot 9
        virtual bool        isValid(hkStringPtr& errorString) const;                        ///< slot 10
        /// @brief Shares this list itself (as CLONE_STATE_SHARABLE) when nothing is bound to it, else copies it.
        virtual hkbNode*    cloneNode(hkbBehaviorGraph& rootBehavior) const;                ///< slot 12
        virtual void        modify(const hkbContext& context, hkbGeneratorOutput& inOut);   ///< slot 23
        /// @brief Forwards to every enabled child that is a BSTweenerModifier (through its clone).
        virtual void        updateSync(const hkbContext& context, hkbNodeInfo& info);       ///< slot 24
    };
} }
