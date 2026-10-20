/// @file
/// @brief The behavior visual debugger's messages, reimplemented against vanilla (AE 1.6.1170).
/// @note The destructors stay implicit (vanilla's out-of-line copies have no vtable store of their own).
#include <Behavior/VisualDebugger/hkbVisualDebuggerData.h>

namespace CB { namespace havok
{
    hkbAuxiliaryNodeInfo::hkbAuxiliaryNodeInfo(hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag), m_referenceBehaviorName(flag), m_selfTransitionNames(flag)
    {
    }

    hkbBehaviorEventsInfo::hkbBehaviorEventsInfo(hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag), m_externalEventIds(flag)
    {
    }

    hkbBehaviorInfo::hkbBehaviorInfo(hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag), m_data(flag), m_idToNamePairs(flag)
    {
    }

    hkbCharacterAddedInfo::hkbCharacterAddedInfo(hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag), m_instanceName(flag), m_templateName(flag), m_fullPathToProject(flag), m_poseModelSpace(flag)
    {
    }

    hkbCharacterControlCommand::hkbCharacterControlCommand(hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag)
    {
    }

    hkbCharacterInfo::hkbCharacterInfo(hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag)
    {
    }

    hkbCharacterSkinInfo::hkbCharacterSkinInfo(hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag), m_deformableSkins(flag), m_rigidSkins(flag)
    {
    }

    hkbCharacterSteppedInfo::hkbCharacterSteppedInfo(hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag), m_poseModelSpace(flag), m_rigidAttachmentTransforms(flag)
    {
    }

    hkbEventRaisedInfo::hkbEventRaisedInfo(hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag), m_eventName(flag)
    {
    }

    hkbLinkedSymbolInfo::hkbLinkedSymbolInfo(hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag), m_eventNames(flag), m_variableNames(flag)
    {
    }

    hkbRaiseEventCommand::hkbRaiseEventCommand(hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag)
    {
    }

    hkbSetBehaviorCommand::hkbSetBehaviorCommand(hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag), m_behavior(flag), m_rootGenerator(flag), m_referencedBehaviors(flag)
    {
    }

    hkbSetLocalTimeOfClipGeneratorCommand::hkbSetLocalTimeOfClipGeneratorCommand(hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag)
    {
    }

    hkbSetNodePropertyCommand::hkbSetNodePropertyCommand(hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag), m_nodeName(flag), m_propertyName(flag)
    {
    }

    hkbSetWordVariableCommand::hkbSetWordVariableCommand(hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag)
    {
    }

    hkbSimulationControlCommand::hkbSimulationControlCommand(hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag)
    {
    }

    hkbSimulationStateInfo::hkbSimulationStateInfo(hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag)
    {
    }
} }
