/// @file
/// @brief hkbBehaviorGraphInternalStateInfo: a character's graph state with the events and variables in use.
#pragma once

#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>

namespace CB { namespace schema
{
    /// @note 0x50 bytes. Member names after the Havok SDK (unreflected here); the types are what the destructor
    ///       releases. The auxiliary infos are the SDK's hkbAuxiliaryNodeInfo, not declared here.
    class hkbBehaviorGraphInternalStateInfo : public hkReferencedObject
    {
    public:
        hkUlong                                       m_characterId;          ///< +0x10
        hkRefPtr<hkbBehaviorGraphInternalState>       m_internalState;        ///< +0x18
        hkArray< hkRefPtr<hkReferencedObject> >       m_auxiliaryNodeInfo;    ///< +0x20
        hkArray<hkInt16>                              m_activeEventIds;       ///< +0x30
        hkArray<hkInt16>                              m_activeVariableIds;    ///< +0x40

        HK_FORCE_INLINE hkbBehaviorGraphInternalStateInfo() {}
        /// @note Vanilla inlines it into the type registry's finish function; out of line here, it also brings the
        ///       class's vtable and scalar deleting destructor into this unit.
        hkbBehaviorGraphInternalStateInfo(hkFinishLoadedObjectFlag flag);
    };
} }
