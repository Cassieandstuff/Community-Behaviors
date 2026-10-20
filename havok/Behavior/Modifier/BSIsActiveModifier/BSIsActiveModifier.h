/// @file
/// @brief BSIsActiveModifier: five flags that say whether the modifier is active (each optionally inverted).
#pragma once

#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace havok
{
    /// @note Member names are the class's reflected names. The `slot N` annotations are vanilla's vtable slots; it keeps
    ///       the base getClassType. The destructor is implicit: the scalar deleting destructor (slot 0) inlines it.
    class BSIsActiveModifier : public hkbModifier
    {
    public:
        hkBool m_bIsActive0;       ///< +0x50
        hkBool m_bInvertActive0;   ///< +0x51
        hkBool m_bIsActive1;       ///< +0x52
        hkBool m_bInvertActive1;   ///< +0x53
        hkBool m_bIsActive2;       ///< +0x54
        hkBool m_bInvertActive2;   ///< +0x55
        hkBool m_bIsActive3;       ///< +0x56
        hkBool m_bInvertActive3;   ///< +0x57
        hkBool m_bIsActive4;       ///< +0x58
        hkBool m_bInvertActive4;   ///< +0x59

        BSIsActiveModifier();
        BSIsActiveModifier(hkFinishLoadedObjectFlag flag);
        BSIsActiveModifier(const BSIsActiveModifier& mod);

        /// @brief Each m_bIsActiveN becomes true (false when m_bInvertActiveN).
        virtual void     activate(const hkbContext& context);                 ///< slot 4
        /// @note Empty, but overridden (vanilla's own `ret`).
        virtual void     update(const hkbContext& context, hkReal timestep);  ///< slot 5
        /// @brief Each m_bIsActiveN becomes false (true when m_bInvertActiveN).
        virtual void     deactivate(const hkbContext& context);               ///< slot 7
        virtual bool     isValid(hkStringPtr& errorString) const;             ///< slot 10
        virtual hkbNode* cloneNode(hkbBehaviorGraph& rootBehavior) const;     ///< slot 12
    };
    static_assert(sizeof(BSIsActiveModifier) == 0x60, "BSIsActiveModifier size");
} }
