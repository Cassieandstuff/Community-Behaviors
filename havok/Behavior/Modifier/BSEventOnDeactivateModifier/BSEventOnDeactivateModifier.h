/// @file
/// @brief BSEventOnDeactivateModifier: Bethesda's modifier that raises an event when it is deactivated.
#pragma once

#include <Behavior/Context/hkbContext.h>
#include <Behavior/Event/hkbEventQueue.h>
#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace havok
{
    /// @note Member names are the class's reflected names. The `slot N` annotations are vanilla's vtable slots. The
    ///       destructor is implicit: the scalar deleting destructor (slot 0) inlines it.
    class BSEventOnDeactivateModifier : public hkbModifier
    {
    public:
        hkbEventProperty m_event;   ///< +0x50

        /// @note Every constructor sets bits 0 and 1 of m_userData; what they flag is unknown.
        BSEventOnDeactivateModifier();
        BSEventOnDeactivateModifier(hkFinishLoadedObjectFlag flag);
        BSEventOnDeactivateModifier(const BSEventOnDeactivateModifier& mod);

        virtual void     deactivate(const hkbContext& context);                ///< slot 7
        virtual bool     isValid(hkStringPtr& errorString) const;              ///< slot 10
        virtual hkbNode* cloneNode(hkbBehaviorGraph& rootBehavior) const;      ///< slot 12
    };
} }
