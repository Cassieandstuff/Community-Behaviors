/// @file
/// @brief hkbStringCondition: a condition given as text, evaluated by an installable callback.
#pragma once

#include <Behavior/Condition/hkbCondition.h>

namespace CB { namespace havok
{
    /// @note 0x18 bytes.
    class hkbStringCondition : public hkbCondition
    {
    public:
        /// @brief Evaluates a condition string.
        /// @note Name provisional; the signature is what isTrue passes (the string in rcx, the context left in rdx).
        typedef bool (*EvaluateFunction)(const char* conditionString, const hkbContext& context);

        /// @note Name provisional (0x1431b77e0). Nothing in the exe writes it: vanilla always takes the default.
        static EvaluateFunction s_evaluateFunction;
        /// @note Name provisional (0x14201efde): isTrue's result while no evaluator is installed.
        static bool s_defaultResult;

        hkStringPtr m_conditionString;   ///< +0x10

        HK_FORCE_INLINE hkbStringCondition() {}
        /// @note Vanilla inlines it into the type registry's finish function; out of line here, it also brings the
        ///       class's vtable and scalar deleting destructor into this unit.
        hkbStringCondition(hkFinishLoadedObjectFlag flag);

        virtual bool isTrue(const hkbContext& context);   ///< slot 3
    };
} }
