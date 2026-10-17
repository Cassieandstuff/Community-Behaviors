/// @file
/// @brief hkbClientCharacterState: what the behavior visual-debugger client mirrors of one remote character.
#pragma once

#include <Behavior/VisualDebugger/hkbVisualDebuggerData.h>

namespace CB { namespace schema
{
    /// @note 0x110 bytes; layout per Havok's reflection. The skeleton is not reference counted (the destructor leaves it).
    class hkbClientCharacterState : public hkReferencedObject
    {
    public:
        hkArray<hkUlong>                         m_deformableSkinIds;           ///< +0x10
        hkArray<hkUlong>                         m_rigidSkinIds;                ///< +0x20
        hkArray<hkInt16>                         m_externalEventIds;            ///< +0x30
        hkArray< hkRefPtr<hkbAuxiliaryNodeInfo> > m_auxiliaryInfo;              ///< +0x40
        hkArray<hkInt16>                         m_activeEventIds;              ///< +0x50
        hkArray<hkInt16>                         m_activeVariableIds;           ///< +0x60
        hkUlong                                  m_characterId;                 ///< +0x70
        hkStringPtr                              m_instanceName;                ///< +0x78
        hkStringPtr                              m_templateName;                ///< +0x80
        hkStringPtr                              m_fullPathToProject;           ///< +0x88
        hkRefPtr<hkbBehaviorGraphData>           m_behaviorData;                ///< +0x90
        hkRefPtr<hkbBehaviorGraphInternalState>  m_behaviorInternalState;       ///< +0x98
        /// @note Owned (the destructor deletes it); not serialized. Key and value types provisional.
        hkPointerMap<int, int>*                  m_nodeIdToInternalStateMap;    ///< +0xa0
        hkBool                                   m_visible;                     ///< +0xa8
        hkReal                                   m_elapsedSimulationTime;       ///< +0xac
        const hkaSkeleton*                       m_skeleton;                    ///< +0xb0
        hkQsTransform                            m_worldFromModel;              ///< +0xc0
        hkArray<hkQsTransform>                   m_poseModelSpace;              ///< +0xf0
        hkArray<hkQsTransform>                   m_rigidAttachmentTransforms;   ///< +0x100

        hkbClientCharacterState(hkFinishLoadedObjectFlag flag);
        ~hkbClientCharacterState();
    };
} }
