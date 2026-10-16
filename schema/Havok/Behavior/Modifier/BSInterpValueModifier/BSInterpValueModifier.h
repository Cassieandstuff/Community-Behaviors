/// @file
/// @brief BSInterpValueModifier: eases a result from a source value toward a target value at a timestep-scaled gain.
#pragma once

#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace schema
{
    /// @note Member names are the class's reflected names (m_timeStep is reflected serialize-ignored). The `slot N`
    ///       annotations are vanilla's vtable slots. The destructor is implicit: the scalar deleting destructor (slot 0)
    ///       inlines it.
    class BSInterpValueModifier : public hkbModifier
    {
    public:
        hkReal m_source;     ///< +0x50
        hkReal m_target;     ///< +0x54
        hkReal m_result;     ///< +0x58
        hkReal m_gain;       ///< +0x5c
        hkReal m_timeStep;   ///< +0x60, the last update's

        BSInterpValueModifier();
        BSInterpValueModifier(hkFinishLoadedObjectFlag flag);
        BSInterpValueModifier(const BSInterpValueModifier& mod);

        static const hkClass& staticClass();

        virtual const void* getClassType() const;                                ///< slot 1
        virtual void        activate(const hkbContext& context);                 ///< slot 4
        /// @brief m_result = the source moved toward the target by BSComputeGainForTimeStep(m_gain, timestep).
        virtual void        update(const hkbContext& context, hkReal timestep);  ///< slot 5
        virtual bool        isValid(hkStringPtr& errorString) const;             ///< slot 10
        virtual hkbNode*    cloneNode(hkbBehaviorGraph& rootBehavior) const;     ///< slot 12
    };
    static_assert(sizeof(BSInterpValueModifier) == 0x68, "BSInterpValueModifier size");
} }
