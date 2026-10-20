/// @file
/// @brief The behavior visual debugger's messages: the infos the server reports about characters and the commands a
///        client sends back.
/// @note Vanilla compiles them into one unit (their destructors and deleting destructors sit together at
///       0x140f0b410..0x140f0c4f0); the file name is provisional (the exe names only VisualDebugger\Client\hkbBehaviorClient.cpp).
///       Layouts per Havok's reflection.
#pragma once

#include <Animation/Rig/hkaSkeleton.h>
#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>

namespace CB { namespace havok
{
    class hkbAuxiliaryNodeInfo : public hkReferencedObject
    {
    public:
        enum NodeType
        {
            NODE_TYPE_UNKNOWN,
            NODE_TYPE_NODE,
            NODE_TYPE_TRANSITION,
            NODE_TYPE_WILDCARD_TRANSITION,
            NODE_TYPE_STATE,
            NODE_TYPE_STATE_MACHINE,
            NODE_TYPE_MODIFIER_GENERATOR,
            NODE_TYPE_MODIFIER,
            NODE_TYPE_CLIP,
            NODE_TYPE_BLEND,
            NODE_TYPE_TRANSITION_EFFECT,
        };

        hkEnum<NodeType, hkUint8> m_type;                    ///< +0x10
        hkUint8                   m_depth;                   ///< +0x11
        hkStringPtr               m_referenceBehaviorName;   ///< +0x18
        hkArray<hkStringPtr>      m_selfTransitionNames;     ///< +0x20

        hkbAuxiliaryNodeInfo(hkFinishLoadedObjectFlag flag);
    };

    class hkbBehaviorEventsInfo : public hkReferencedObject
    {
    public:
        hkUlong          m_characterId;        ///< +0x10
        hkArray<hkInt16> m_externalEventIds;   ///< +0x18
        int              m_padding;            ///< +0x28

        hkbBehaviorEventsInfo(hkFinishLoadedObjectFlag flag);
    };

    struct hkbBehaviorInfoIdToNamePair
    {
        enum ToolNodeType
        {
            NODE_TYPE_UNKNOWN,
            NODE_TYPE_STATE_MACHINE,
            NODE_TYPE_CLIP,
            NODE_TYPE_BLEND,
            NODE_TYPE_MODIFIER,
            NODE_TYPE_GENERATOR,
            NODE_TYPE_MODIFIER_GENERATOR,
            NODE_TYPE_TRANSITION_EFFECT,
            NODE_TYPE_BEHAVIOR_FILE_REFERENCE,
        };

        hkStringPtr                   m_behaviorName;   ///< +0x00
        hkStringPtr                   m_nodeName;       ///< +0x08
        hkEnum<ToolNodeType, hkUint8> m_toolType;       ///< +0x10
        hkInt16                       m_id;             ///< +0x12
    };

    class hkbBehaviorInfo : public hkReferencedObject
    {
    public:
        hkUlong                                m_characterId;     ///< +0x10
        hkRefPtr<hkbBehaviorGraphData>         m_data;            ///< +0x18
        hkArray<hkbBehaviorInfoIdToNamePair>   m_idToNamePairs;   ///< +0x20

        hkbBehaviorInfo(hkFinishLoadedObjectFlag flag);
    };

    /// @note The skeleton is not reference counted (the destructor leaves it).
    class hkbCharacterAddedInfo : public hkReferencedObject
    {
    public:
        hkUlong                m_characterId;         ///< +0x10
        hkStringPtr            m_instanceName;        ///< +0x18
        hkStringPtr            m_templateName;        ///< +0x20
        hkStringPtr            m_fullPathToProject;   ///< +0x28
        const hkaSkeleton*     m_skeleton;            ///< +0x30
        hkQsTransform          m_worldFromModel;      ///< +0x40
        hkArray<hkQsTransform> m_poseModelSpace;      ///< +0x70

        hkbCharacterAddedInfo(hkFinishLoadedObjectFlag flag);
    };

    class hkbCharacterControlCommand : public hkReferencedObject
    {
    public:
        enum CharacterControlCommand
        {
            COMMAND_HIDE,
            COMMAND_SHOW,
        };

        hkUlong                                  m_characterId;   ///< +0x10
        hkEnum<CharacterControlCommand, hkUint8> m_command;       ///< +0x18
        int                                      m_padding;       ///< +0x1c

        hkbCharacterControlCommand(hkFinishLoadedObjectFlag flag);
    };

    class hkbCharacterInfo : public hkReferencedObject
    {
    public:
        enum Event
        {
            REMOVED_FROM_WORLD,
            SHOWN,
            HIDDEN,
        };

        hkUlong                m_characterId;   ///< +0x10
        hkEnum<Event, hkUint8> m_event;         ///< +0x18
        int                    m_padding;       ///< +0x1c

        hkbCharacterInfo(hkFinishLoadedObjectFlag flag);
    };

    class hkbCharacterSkinInfo : public hkReferencedObject
    {
    public:
        hkUlong          m_characterId;       ///< +0x10
        hkArray<hkUlong> m_deformableSkins;   ///< +0x18
        hkArray<hkUlong> m_rigidSkins;        ///< +0x28

        hkbCharacterSkinInfo(hkFinishLoadedObjectFlag flag);
    };

    class hkbCharacterSteppedInfo : public hkReferencedObject
    {
    public:
        hkUlong                m_characterId;                 ///< +0x10
        hkReal                 m_deltaTime;                   ///< +0x18
        hkQsTransform          m_worldFromModel;              ///< +0x20
        hkArray<hkQsTransform> m_poseModelSpace;              ///< +0x50
        hkArray<hkQsTransform> m_rigidAttachmentTransforms;   ///< +0x60

        hkbCharacterSteppedInfo(hkFinishLoadedObjectFlag flag);
    };

    class hkbEventRaisedInfo : public hkReferencedObject
    {
    public:
        hkUlong     m_characterId;   ///< +0x10
        hkStringPtr m_eventName;     ///< +0x18
        hkBool      m_raisedBySdk;   ///< +0x20
        int         m_senderId;      ///< +0x24
        int         m_padding;       ///< +0x28

        hkbEventRaisedInfo(hkFinishLoadedObjectFlag flag);
    };

    class hkbLinkedSymbolInfo : public hkReferencedObject
    {
    public:
        hkArray<hkStringPtr> m_eventNames;      ///< +0x10
        hkArray<hkStringPtr> m_variableNames;   ///< +0x20

        hkbLinkedSymbolInfo(hkFinishLoadedObjectFlag flag);
    };

    class hkbRaiseEventCommand : public hkReferencedObject
    {
    public:
        hkUlong m_characterId;   ///< +0x10
        hkBool  m_global;        ///< +0x18
        int     m_externalId;    ///< +0x1c

        hkbRaiseEventCommand(hkFinishLoadedObjectFlag flag);
    };

    class hkbSetBehaviorCommand : public hkReferencedObject
    {
    public:
        hkUlong                              m_characterId;           ///< +0x10
        hkRefPtr<hkbBehaviorGraph>           m_behavior;              ///< +0x18
        hkRefPtr<hkbGenerator>               m_rootGenerator;         ///< +0x20
        hkArray< hkRefPtr<hkbBehaviorGraph> > m_referencedBehaviors;  ///< +0x28
        int                                  m_startStateIndex;       ///< +0x38
        hkBool                               m_randomizeSimulation;   ///< +0x3c
        int                                  m_padding;               ///< +0x40

        hkbSetBehaviorCommand(hkFinishLoadedObjectFlag flag);
    };

    class hkbSetLocalTimeOfClipGeneratorCommand : public hkReferencedObject
    {
    public:
        hkUlong m_characterId;   ///< +0x10
        hkReal  m_localTime;     ///< +0x18
        hkInt16 m_nodeId;        ///< +0x1c

        hkbSetLocalTimeOfClipGeneratorCommand(hkFinishLoadedObjectFlag flag);
    };

    class hkbSetNodePropertyCommand : public hkReferencedObject
    {
    public:
        hkUlong          m_characterId;     ///< +0x10
        hkStringPtr      m_nodeName;        ///< +0x18
        hkStringPtr      m_propertyName;    ///< +0x20
        hkbVariableValue m_propertyValue;   ///< +0x28
        int              m_padding;         ///< +0x2c

        hkbSetNodePropertyCommand(hkFinishLoadedObjectFlag flag);
    };

    class hkbSetWordVariableCommand : public hkReferencedObject
    {
    public:
        hkVector4                                    m_quadValue;     ///< +0x10
        hkUlong                                      m_characterId;   ///< +0x20
        int                                          m_variableId;    ///< +0x28
        hkbVariableValue                             m_value;         ///< +0x2c
        hkEnum<hkbVariableInfo::VariableType, hkUint8> m_type;        ///< +0x30
        hkBool                                       m_global;        ///< +0x31

        hkbSetWordVariableCommand(hkFinishLoadedObjectFlag flag);
    };

    class hkbSimulationControlCommand : public hkReferencedObject
    {
    public:
        enum SimulationControlCommand
        {
            COMMAND_PLAY,
            COMMAND_PAUSE,
            COMMAND_STEP,
            COMMAND_STOP,
            COMMAND_ACCUMULATE_MOTION,
            COMMAND_DO_NOT_ACCUMULATE_MOTION,
        };

        hkEnum<SimulationControlCommand, hkUint8> m_command;   ///< +0x10

        hkbSimulationControlCommand(hkFinishLoadedObjectFlag flag);
    };

    class hkbSimulationStateInfo : public hkReferencedObject
    {
    public:
        /// @note Reflected as an 8-bit enum whose values the exe does not carry.
        hkUint8 m_simulationState;   ///< +0x10

        hkbSimulationStateInfo(hkFinishLoadedObjectFlag flag);
    };
} }
