/// @file
/// @brief BSGetTimeStepModifier: exposes the update's timestep (bindable to a variable).
#pragma once

#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace schema
{
    /// @note Member names are the class's reflected names. The `slot N` annotations are vanilla's vtable slots; it keeps
    ///       the base getClassType. The destructor is implicit: the scalar deleting destructor (slot 0) inlines it.
    class BSGetTimeStepModifier : public hkbModifier
    {
    public:
        hkReal m_timeStep;   ///< +0x50

        BSGetTimeStepModifier();
        BSGetTimeStepModifier(hkFinishLoadedObjectFlag flag);
        BSGetTimeStepModifier(const BSGetTimeStepModifier& mod);

        virtual void     update(const hkbContext& context, hkReal timestep);  ///< slot 5
        virtual hkbNode* cloneNode(hkbBehaviorGraph& rootBehavior) const;     ///< slot 12
    };
    static_assert(sizeof(BSGetTimeStepModifier) == 0x58, "BSGetTimeStepModifier size");
} }
