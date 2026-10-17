/// @file
/// @brief VisualDebugger\Client\hkbBehaviorClient.cpp, reimplemented against vanilla (AE 1.6.1170): the destructors so far.
#include <Behavior/VisualDebugger/Client/hkbBehaviorClient.h>

namespace CB { namespace schema
{
    hkbClientCharacterState::hkbClientCharacterState(hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag),
        m_deformableSkinIds(flag),
        m_rigidSkinIds(flag),
        m_externalEventIds(flag),
        m_auxiliaryInfo(flag),
        m_activeEventIds(flag),
        m_activeVariableIds(flag),
        m_instanceName(flag),
        m_templateName(flag),
        m_fullPathToProject(flag),
        m_behaviorData(flag),
        m_behaviorInternalState(flag),
        m_poseModelSpace(flag),
        m_rigidAttachmentTransforms(flag)
    {
    }

    hkbClientCharacterState::~hkbClientCharacterState()
    {
        delete m_nodeIdToInternalStateMap;
    }

    hkbBehaviorClient::~hkbBehaviorClient()
    {
    }
} }
