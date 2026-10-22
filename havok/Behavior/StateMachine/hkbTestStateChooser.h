/// @file
/// @brief hkbTestStateChooser: Havok's test state chooser (always state 0).
#pragma once

#include <Behavior/StateMachine/hkbStateChooser.h>

namespace CB { namespace havok
{
    /// @note 0x20 bytes; members after the Havok SDK (unreflected here), unverified beyond the string at +0x18.
    class hkbTestStateChooser : public hkbStateChooser
    {
    public:
        hkInt32     m_int;      ///< +0x10
        hkReal      m_real;     ///< +0x14
        hkStringPtr m_string;   ///< +0x18

        HK_FORCE_INLINE hkbTestStateChooser() {}
        /// @note Vanilla inlines it into the type registry's finish function; out of line here, it also brings the
        ///       class's vtable and scalar deleting destructor into this unit.
        hkbTestStateChooser(hkFinishLoadedObjectFlag flag);

        virtual int chooseStateId(const hkbContext& context, hkbStateMachine& sm);   ///< slot 3
    };
} }
