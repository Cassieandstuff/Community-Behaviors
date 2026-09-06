// Per-class Read(PackFileDeserializer&, BinaryReaderEx&) — the exact mirror of
// ClassWrite.cpp (every WriteX -> ReadX, every Skip -> Skip). Plus the registry
// of class-name -> factory used by ConstructVirtualClass.

#include "havok/classes/Classes.h"
#include "havok/core/HavokRegistry.h"
#include "havok/core/PackFileDeserializer.h"

namespace havok {

// ── Base.h ──────────────────────────────────────────────────────────────────
void hkBaseObject::Read(PackFileDeserializer&, BinaryReaderEx& br) {
    br.ReadUSize();
}
void hkReferencedObject::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkBaseObject::Read(des, br);
    m_memSizeAndFlags = br.ReadUInt16();
    m_referenceCount = br.ReadInt16();
    if (des._header.PointerSize == 8) br.Pad(8);
}
void hkbBindable::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
    m_variableBindingSet = des.ReadClassPointer<hkbVariableBindingSet>(br);
    des.ReadEmptyArray(br);
    m_areBindablesCached = br.ReadBoolean();
    br.Skip(7);
}
void hkbNode::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbBindable::Read(des, br);
    m_userData = br.ReadUInt64();
    m_name = des.ReadStringPointer(br);
    m_id = br.ReadInt16();
    m_cloneState = br.ReadSByte();
    m_padNode = des.ReadBooleanCStyleArray<1>(br);
    br.Skip(4);
}
void hkbGenerator::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbNode::Read(des, br);
}

// ── Events.h ────────────────────────────────────────────────────────────────
void hkbEventBase::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    m_id = br.ReadInt32();
    br.Skip(4);
    m_payload = des.ReadClassPointer<hkbEventPayload>(br);
}
void hkbEvent::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbEventBase::Read(des, br);
    des.ReadEmptyPointer(br);
}
void hkbEventProperty::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbEventBase::Read(des, br);
}
void hkbEventPayload::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
}
void hkbStringEventPayload::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbEventPayload::Read(des, br);
    m_data = des.ReadStringPointer(br);
}
void hkbCondition::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
}

// ── Variables.h ─────────────────────────────────────────────────────────────
void hkbRoleAttribute::Read(PackFileDeserializer&, BinaryReaderEx& br) {
    m_role = br.ReadInt16();
    m_flags = br.ReadInt16();
}
void hkbVariableInfo::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    m_role.Read(des, br);
    m_type = br.ReadSByte();
    br.Skip(1);
}
void hkbVariableValue::Read(PackFileDeserializer&, BinaryReaderEx& br) {
    m_value = br.ReadInt32();
}
void hkbEventInfo::Read(PackFileDeserializer&, BinaryReaderEx& br) {
    m_flags = br.ReadUInt32();
}
void hkbVariableBindingSetBinding::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    m_memberPath = des.ReadStringPointer(br);
    des.ReadEmptyPointer(br);
    m_offsetInObjectPlusOne = br.ReadInt32();
    m_offsetInArrayPlusOne = br.ReadInt32();
    m_rootVariableIndex = br.ReadInt32();
    m_variableIndex = br.ReadInt32();
    m_bitIndex = br.ReadSByte();
    m_bindingType = br.ReadSByte();
    m_memberType = br.ReadByte();
    m_variableType = br.ReadSByte();
    m_flags = br.ReadSByte();
    br.Skip(3);
}
void hkbVariableBindingSet::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
    m_bindings = des.ReadClassArray<hkbVariableBindingSetBinding>(br);
    m_indexOfBindingToEnable = br.ReadInt32();
    m_hasOutputBinding = br.ReadBoolean();
    br.Skip(3);
}
void hkbVariableValueSet::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
    m_wordVariableValues = des.ReadClassArray<hkbVariableValue>(br);
    m_quadVariableValues = des.ReadVector4Array(br);
    m_variantVariableValues = des.ReadClassPointerArray<hkReferencedObject>(br);
}

// ── Arrays.h ────────────────────────────────────────────────────────────────
void hkbStateMachineTimeInterval::Read(PackFileDeserializer&, BinaryReaderEx& br) {
    m_enterEventId = br.ReadInt32();
    m_exitEventId = br.ReadInt32();
    m_enterTime = br.ReadSingle();
    m_exitTime = br.ReadSingle();
}
void hkbStateMachineTransitionInfo::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    m_triggerInterval.Read(des, br);
    m_initiateInterval.Read(des, br);
    m_transition = des.ReadClassPointer<hkbTransitionEffect>(br);
    m_condition = des.ReadClassPointer<hkbCondition>(br);
    m_eventId = br.ReadInt32();
    m_toStateId = br.ReadInt32();
    m_fromNestedStateId = br.ReadInt32();
    m_toNestedStateId = br.ReadInt32();
    m_priority = br.ReadInt16();
    m_flags = br.ReadInt16();
    br.Skip(4);
}
void hkbStateMachineTransitionInfoArray::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
    m_transitions = des.ReadClassArray<hkbStateMachineTransitionInfo>(br);
}
void hkbStateMachineEventPropertyArray::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
    m_events = des.ReadClassArray<hkbEventProperty>(br);
}
void hkbClipTrigger::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    m_localTime = br.ReadSingle();
    br.Skip(4);
    m_event.Read(des, br);
    m_relativeToEndOfClip = br.ReadBoolean();
    m_acyclic = br.ReadBoolean();
    m_isAnnotation = br.ReadBoolean();
    br.Skip(5);
}
void hkbClipTriggerArray::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
    m_triggers = des.ReadClassArray<hkbClipTrigger>(br);
}
void hkbBoneWeightArray::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbBindable::Read(des, br);
    m_boneWeights = des.ReadSingleArray(br);
}

// ── StateMachine.h ──────────────────────────────────────────────────────────
void hkbStateChooser::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
}
void hkbStateListener::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
}
void hkbStateMachineStateInfo::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbBindable::Read(des, br);
    m_listeners = des.ReadClassPointerArray<hkbStateListener>(br);
    m_enterNotifyEvents = des.ReadClassPointer<hkbStateMachineEventPropertyArray>(br);
    m_exitNotifyEvents = des.ReadClassPointer<hkbStateMachineEventPropertyArray>(br);
    m_transitions = des.ReadClassPointer<hkbStateMachineTransitionInfoArray>(br);
    m_generator = des.ReadClassPointer<hkbGenerator>(br);
    m_name = des.ReadStringPointer(br);
    m_stateId = br.ReadInt32();
    m_probability = br.ReadSingle();
    m_enable = br.ReadBoolean();
    br.Skip(7);
}
void hkbStateMachine::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbGenerator::Read(des, br);
    m_eventToSendWhenStateOrTransitionChanges.Read(des, br);
    m_startStateChooser = des.ReadClassPointer<hkbStateChooser>(br);
    m_startStateId = br.ReadInt32();
    m_returnToPreviousStateEventId = br.ReadInt32();
    m_randomTransitionEventId = br.ReadInt32();
    m_transitionToNextHigherStateEventId = br.ReadInt32();
    m_transitionToNextLowerStateEventId = br.ReadInt32();
    m_syncVariableIndex = br.ReadInt32();
    m_currentStateId = br.ReadInt32();
    m_wrapAroundStateId = br.ReadBoolean();
    m_maxSimultaneousTransitions = br.ReadSByte();
    m_startStateMode = br.ReadSByte();
    m_selfTransitionMode = br.ReadSByte();
    m_isActive = br.ReadBoolean();
    br.Skip(7);
    m_states = des.ReadClassPointerArray<hkbStateMachineStateInfo>(br);
    m_wildcardTransitions = des.ReadClassPointer<hkbStateMachineTransitionInfoArray>(br);
    des.ReadEmptyPointer(br);
    des.ReadEmptyArray(br);
    des.ReadEmptyArray(br);
    des.ReadEmptyArray(br);
    des.ReadEmptyArray(br);
    m_timeInState = br.ReadSingle();
    m_lastLocalTime = br.ReadSingle();
    m_previousStateId = br.ReadInt32();
    m_nextStartStateIndexOverride = br.ReadInt32();
    m_stateOrTransitionChanged = br.ReadBoolean();
    m_echoNextUpdate = br.ReadBoolean();
    m_sCurrentStateIndexAndEntered = br.ReadUInt16();
    br.Skip(4);
}

// ── Generators.h ────────────────────────────────────────────────────────────
void hkbClipGenerator::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbGenerator::Read(des, br);
    m_animationName = des.ReadStringPointer(br);
    m_triggers = des.ReadClassPointer<hkbClipTriggerArray>(br);
    m_cropStartAmountLocalTime = br.ReadSingle();
    m_cropEndAmountLocalTime = br.ReadSingle();
    m_startTime = br.ReadSingle();
    m_playbackSpeed = br.ReadSingle();
    m_enforcedDuration = br.ReadSingle();
    m_userControlledTimeFraction = br.ReadSingle();
    m_animationBindingIndex = br.ReadInt16();
    m_mode = br.ReadSByte();
    m_flags = br.ReadSByte();
    br.Skip(4);
    des.ReadEmptyArray(br);
    des.ReadEmptyPointer(br);
    des.ReadEmptyPointer(br);
    des.ReadEmptyPointer(br);
    des.ReadEmptyPointer(br);
    des.ReadEmptyPointer(br);
    m_extractedMotion = des.ReadQSTransform(br);
    des.ReadEmptyArray(br);
    m_localTime = br.ReadSingle();
    m_time = br.ReadSingle();
    m_previousUserControlledTimeFraction = br.ReadSingle();
    m_bufferSize = br.ReadInt32();
    m_echoBufferSize = br.ReadInt32();
    m_atEnd = br.ReadBoolean();
    m_ignoreStartTime = br.ReadBoolean();
    m_pingPongBackward = br.ReadBoolean();
    br.Skip(9);
}
void hkbBlenderGeneratorChild::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbBindable::Read(des, br);
    m_generator = des.ReadClassPointer<hkbGenerator>(br);
    m_boneWeights = des.ReadClassPointer<hkbBoneWeightArray>(br);
    m_weight = br.ReadSingle();
    m_worldFromModelWeight = br.ReadSingle();
    br.Skip(8);
}
void hkbBlenderGenerator::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbGenerator::Read(des, br);
    m_referencePoseWeightThreshold = br.ReadSingle();
    m_blendParameter = br.ReadSingle();
    m_minCyclicBlendParameter = br.ReadSingle();
    m_maxCyclicBlendParameter = br.ReadSingle();
    m_indexOfSyncMasterChild = br.ReadInt16();
    m_flags = br.ReadInt16();
    m_subtractLastChild = br.ReadBoolean();
    br.Skip(3);
    m_children = des.ReadClassPointerArray<hkbBlenderGeneratorChild>(br);
    des.ReadEmptyArray(br);
    des.ReadEmptyArray(br);
    m_endIntervalWeight = br.ReadSingle();
    m_numActiveChildren = br.ReadInt32();
    m_beginIntervalIndex = br.ReadInt16();
    m_endIntervalIndex = br.ReadInt16();
    m_initSync = br.ReadBoolean();
    m_doSubtractiveBlend = br.ReadBoolean();
    br.Skip(2);
}
void hkbManualSelectorGenerator::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbGenerator::Read(des, br);
    m_generators = des.ReadClassPointerArray<hkbGenerator>(br);
    m_selectedGeneratorIndex = br.ReadSByte();
    m_currentGeneratorIndex = br.ReadSByte();
    br.Skip(6);
}
void BSCyclicBlendTransitionGenerator::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbGenerator::Read(des, br);
    br.Skip(8);
    m_pBlenderGenerator = des.ReadClassPointer<hkbGenerator>(br);
    m_EventToFreezeBlendValue.Read(des, br);
    m_EventToCrossBlend.Read(des, br);
    m_fBlendParameter = br.ReadSingle();
    m_fTransitionDuration = br.ReadSingle();
    m_eBlendCurve = br.ReadSByte();
    br.Skip(15);
    des.ReadEmptyPointer(br);
    br.Skip(8);
    des.ReadEmptyPointer(br);
    m_currentMode = br.ReadSByte();
    br.Skip(7);
}
void BSBoneSwitchGeneratorBoneData::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbBindable::Read(des, br);
    m_pGenerator = des.ReadClassPointer<hkbGenerator>(br);
    m_spBoneWeight = des.ReadClassPointer<hkbBoneWeightArray>(br);
}
void BSBoneSwitchGenerator::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbGenerator::Read(des, br);
    br.Skip(8);
    m_pDefaultGenerator = des.ReadClassPointer<hkbGenerator>(br);
    m_ChildrenA = des.ReadClassPointerArray<BSBoneSwitchGeneratorBoneData>(br);
    br.Skip(8);
}
void BSiStateTaggingGenerator::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbGenerator::Read(des, br);
    br.Skip(8);
    m_pDefaultGenerator = des.ReadClassPointer<hkbGenerator>(br);
    m_iStateToSetAs = br.ReadInt32();
    m_iPriority = br.ReadInt32();
}
void hkbBehaviorReferenceGenerator::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbGenerator::Read(des, br);
    m_behaviorName = des.ReadStringPointer(br);
    des.ReadEmptyPointer(br);
}
void BGSGamebryoSequenceGenerator::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbGenerator::Read(des, br);              // 0..72
    m_pSequence = des.ReadStringPointer(br);  // 72..80
    m_eBlendModeFunction = br.ReadSByte();    // 80
    br.Skip(3);                               // 81..84 pad
    m_fPercent = br.ReadSingle();             // 84..88
    des.ReadEmptyArray(br);                   // 88..104 m_events (SERIALIZE_IGNORED)
    m_fTime = br.ReadSingle();                // 104..108 (SERIALIZE_IGNORED)
    m_bDelayedActivate = br.ReadBoolean();    // 108 (SERIALIZE_IGNORED)
    m_bLooping = br.ReadBoolean();            // 109 (SERIALIZE_IGNORED)
    br.Skip(2);                               // 110..112 pad
}

// ── Effects.h ───────────────────────────────────────────────────────────────
void hkbTransitionEffect::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbGenerator::Read(des, br);
    m_selfTransitionMode = br.ReadSByte();
    m_eventMode = br.ReadSByte();
    m_defaultEventMode = br.ReadSByte();
    br.Skip(5);
}
void hkbBlendingTransitionEffect::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbTransitionEffect::Read(des, br);
    m_duration = br.ReadSingle();
    m_toGeneratorStartTimeFraction = br.ReadSingle();
    m_flags = br.ReadUInt16();
    m_endMode = br.ReadSByte();
    m_blendCurve = br.ReadSByte();
    br.Skip(4);
    des.ReadEmptyPointer(br);
    des.ReadEmptyPointer(br);
    des.ReadEmptyArray(br);
    m_timeRemaining = br.ReadSingle();
    m_timeInTransition = br.ReadSingle();
    m_applySelfTransition = br.ReadBoolean();
    m_initializeCharacterPose = br.ReadBoolean();
    br.Skip(6);
}

// ── Modifiers.h ─────────────────────────────────────────────────────────────
void hkbModifier::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbNode::Read(des, br);
    m_enable = br.ReadBoolean();
    m_padModifier = des.ReadBooleanCStyleArray<3>(br);
    br.Skip(4);
}
void hkbTwistModifier::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbModifier::Read(des, br);
    m_axisOfRotation = des.ReadVector4(br);
    m_twistAngle = br.ReadSingle();
    m_startBoneIndex = br.ReadInt16();
    m_endBoneIndex = br.ReadInt16();
    m_setAngleMethod = br.ReadSByte();
    m_rotationAxisCoordinates = br.ReadSByte();
    m_isAdditive = br.ReadBoolean();
    br.Skip(5);
    m_boneChainIndices  = des.ReadInt16Array(br);
    m_parentBoneIndices = des.ReadInt16Array(br);
}

// ── Graph.h ─────────────────────────────────────────────────────────────────
void hkbBehaviorGraphStringData::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
    m_eventNames = des.ReadStringPointerArray(br);
    m_attributeNames = des.ReadStringPointerArray(br);
    m_variableNames = des.ReadStringPointerArray(br);
    m_characterPropertyNames = des.ReadStringPointerArray(br);
}
void hkbBehaviorGraphData::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
    m_attributeDefaults = des.ReadSingleArray(br);
    m_variableInfos = des.ReadClassArray<hkbVariableInfo>(br);
    m_characterPropertyInfos = des.ReadClassArray<hkbVariableInfo>(br);
    m_eventInfos = des.ReadClassArray<hkbEventInfo>(br);
    m_wordMinVariableValues = des.ReadClassArray<hkbVariableValue>(br);
    m_wordMaxVariableValues = des.ReadClassArray<hkbVariableValue>(br);
    m_variableInitialValues = des.ReadClassPointer<hkbVariableValueSet>(br);
    m_stringData = des.ReadClassPointer<hkbBehaviorGraphStringData>(br);
}
void hkbBehaviorGraph::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbGenerator::Read(des, br);
    m_variableMode = br.ReadSByte();
    br.Skip(7);
    des.ReadEmptyArray(br);
    des.ReadEmptyPointer(br);
    des.ReadEmptyArray(br);
    des.ReadEmptyPointer(br);
    m_rootGenerator = des.ReadClassPointer<hkbGenerator>(br);
    m_data = des.ReadClassPointer<hkbBehaviorGraphData>(br);
    for (int i = 0; i < 14; ++i) des.ReadEmptyPointer(br);
    m_numIntermediateOutputs = br.ReadInt32();
    br.Skip(4);
    des.ReadEmptyArray(br);
    des.ReadEmptyArray(br);
    m_numStaticNodes = br.ReadInt16();
    m_nextUniqueId = br.ReadInt16();
    m_isActive = br.ReadBoolean();
    m_isLinked = br.ReadBoolean();
    m_updateActiveNodes = br.ReadBoolean();
    m_stateOrTransitionChanged = br.ReadBoolean();
}
void hkRootLevelContainerNamedVariant::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    m_name = des.ReadStringPointer(br);
    m_className = des.ReadStringPointer(br);
    m_variant = des.ReadClassPointer<hkReferencedObject>(br);
}
void hkRootLevelContainer::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    m_namedVariants = des.ReadClassArray<hkRootLevelContainerNamedVariant>(br);
}

// ── Resource.h ────────────────────────────────────────────────────────────────
void hkMemoryResourceHandleExternalLink::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    m_memberName = des.ReadStringPointer(br);
    m_externalId = des.ReadStringPointer(br);
}
void hkMemoryResourceHandle::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);   // hkResourceHandle base adds no serialized members
    m_variant    = des.ReadClassPointer<hkReferencedObject>(br);
    m_name       = des.ReadStringPointer(br);
    m_references = des.ReadClassArray<hkMemoryResourceHandleExternalLink>(br);
}
void hkMemoryResourceContainer::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);   // hkResourceContainer base adds no serialized members
    m_name = des.ReadStringPointer(br);
    des.ReadEmptyPointer(br);            // m_parent — SERIALIZE_IGNORED (rebuilt at load)
    m_resourceHandles = des.ReadClassPointerArray<hkMemoryResourceHandle>(br);
    m_children        = des.ReadClassPointerArray<hkMemoryResourceContainer>(br);
}

// ── Physics.h (collision shapes) ────────────────────────────────────────────────
void hkpShape::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
    m_userData = br.ReadUSize();
    br.Skip(8);   // m_type (SERIALIZE_IGNORED) + tail pad to 32
}
void hkpSphereRepShape::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkpShape::Read(des, br);
}
void hkpConvexShape::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkpSphereRepShape::Read(des, br);
    m_radius = br.ReadSingle();
    br.Skip(4);   // pad to 40
}
void hkpCapsuleShape::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkpConvexShape::Read(des, br);
    br.Skip(8);   // pad 40 -> 48 (16-align the Vector4 pair)
    m_vertexA = br.ReadVector4();
    m_vertexB = br.ReadVector4();
}

// ── Physics.h (rigid-body subtree) ──────────────────────────────────────────────
void hkTransform::Read(PackFileDeserializer&, BinaryReaderEx& br) {
    for (auto& v : m_data) v = br.ReadVector4();
}
void hkpPropertyValue::Read(PackFileDeserializer&, BinaryReaderEx& br) {
    m_data = br.ReadUInt64();
}
void hkMotionState::Read(PackFileDeserializer&, BinaryReaderEx& br) {
    for (auto& v : m_transform)      v = br.ReadVector4();   // hkTransform      @0
    for (auto& v : m_sweptTransform) v = br.ReadVector4();   // hkSweptTransform @64
    m_deltaAngle         = br.ReadVector4();                 // @144
    m_objectRadius       = br.ReadSingle();                  // @160
    m_linearDamping      = br.ReadHalf();                    // @164
    m_angularDamping     = br.ReadHalf();                    // @166
    m_timeFactor         = br.ReadHalf();                    // @168
    m_maxLinearVelocity  = br.ReadByte();                    // @170
    m_maxAngularVelocity = br.ReadByte();                    // @171
    m_deactivationClass  = br.ReadByte();                    // @172
    br.Skip(3);                                              // tail pad 173 -> 176
}
void hkpMaterial::Read(PackFileDeserializer&, BinaryReaderEx& br) {
    m_responseType = br.ReadByte();                 // @0
    br.Skip(1);                                     // @1 pad
    m_rollingFrictionMultiplier = br.ReadHalf();    // @2
    m_friction    = br.ReadSingle();                // @4
    m_restitution = br.ReadSingle();                // @8
    br.Skip(4);                                     // tail pad 12 -> 16 (round-to-8)
}
void hkpEntitySpuCollisionCallback::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    des.ReadEmptyPointer(br);        // m_util (SERIALIZE_IGNORED) @0
    br.Skip(2);                      // m_capacity (SERIALIZE_IGNORED) @8
    m_eventFilter = br.ReadByte();   // @10
    m_userFilter  = br.ReadByte();   // @11
    br.Skip(4);                      // tail pad 12 -> 16
}
void hkpProperty::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    m_key              = br.ReadUInt32();
    m_alignmentPadding = br.ReadUInt32();
    m_value.Read(des, br);
}
void hkMultiThreadCheck::Read(PackFileDeserializer&, BinaryReaderEx& br) {
    br.Skip(12);   // all fields SERIALIZE_IGNORED
}
void hkpTypedBroadPhaseHandle::Read(PackFileDeserializer&, BinaryReaderEx& br) {
    br.Skip(4);                                 // hkpBroadPhaseHandle base: m_id (IGNORED)
    m_type = br.ReadSByte();                    // @4
    br.Skip(1);                                 // @5 m_ownerOffset (IGNORED)
    m_objectQualityType = br.ReadSByte();       // @6
    br.Skip(1);                                 // @7 pad
    m_collisionFilterInfo = br.ReadUInt32();    // @8
}
void hkpCdBody::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    m_shape    = des.ReadClassPointer<hkpShape>(br);   // @0
    m_shapeKey = br.ReadUInt32();                      // @8
    br.Skip(4);                                        // @12 pad
    des.ReadEmptyPointer(br);                          // @16 m_motion (IGNORED)
    des.ReadEmptyPointer(br);                          // @24 m_parent (IGNORED)
}
void hkpMotion::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
    m_type                             = br.ReadByte();     // @16
    m_deactivationIntegrateCounter     = br.ReadByte();     // @17
    m_deactivationNumInactiveFrames[0] = br.ReadUInt16();   // @18
    m_deactivationNumInactiveFrames[1] = br.ReadUInt16();   // @20
    br.Skip(10);                                            // pad 22 -> 32
    m_motionState.Read(des, br);                            // @32 (176B)
    m_inertiaAndMassInv          = br.ReadVector4();        // @208
    m_linearVelocity             = br.ReadVector4();        // @224
    m_angularVelocity            = br.ReadVector4();        // @240
    m_deactivationRefPosition[0] = br.ReadVector4();        // @256
    m_deactivationRefPosition[1] = br.ReadVector4();        // @272
    m_deactivationRefOrientation[0] = br.ReadUInt32();      // @288
    m_deactivationRefOrientation[1] = br.ReadUInt32();      // @292
    des.ReadEmptyPointer(br);                               // @296 m_savedMotion (+nosave)
    m_savedQualityTypeIndex = br.ReadUInt16();              // @304
    m_gravityFactor         = br.ReadHalf();                // @306
    br.Skip(12);                                            // tail pad 308 -> 320
}
void hkpKeyframedRigidMotion::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkpMotion::Read(des, br);
}
void hkpMaxSizeMotion::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkpKeyframedRigidMotion::Read(des, br);
}
void hkpCollidable::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkpCdBody::Read(des, br);                       // base 32B
    br.Skip(1);                                     // @32 m_ownerOffset (IGNORED)
    m_forceCollideOntoPpu = br.ReadByte();          // @33
    br.Skip(2);                                     // @34 m_shapeSizeOnSpu (IGNORED)
    m_broadPhaseHandle.Read(des, br);               // @36 (12B)
    br.Skip(56);                                    // @48 m_boundingVolumeData (whole member IGNORED)
    m_allowedPenetrationDepth = br.ReadSingle();    // @104
    br.Skip(4);                                     // tail pad 108 -> 112
}
void hkpLinkedCollidable::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkpCollidable::Read(des, br);
    br.Skip(16);   // @112 m_collisionEntries hkArray (IGNORED, empty)
}
void hkpWorldObject::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
    des.ReadEmptyPointer(br);                 // @16 m_world (IGNORED)
    m_userData = br.ReadUSize();              // @24
    m_collidable.Read(des, br);               // @32 (128B)
    m_multiThreadCheck.Read(des, br);         // @160 (12B)
    br.Skip(4);                               // pad 172 -> 176
    m_name = des.ReadStringPointer(br);       // @176
    m_properties = des.ReadClassArray<hkpProperty>(br);  // @184
    des.ReadEmptyPointer(br);                 // @200 m_treeData (IGNORED)
}
void hkpEntity::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkpWorldObject::Read(des, br);                    // base 0..208
    m_material.Read(des, br);                         // @208 (16B)
    des.ReadEmptyPointer(br);                         // @224 m_limitContactImpulseUtilAndFlag (IGNORED)
    m_damageMultiplier = br.ReadSingle();             // @232
    br.Skip(4);                                       // pad 236 -> 240
    des.ReadEmptyPointer(br);                          // @240 m_breakableBody (IGNORED)
    br.Skip(4);                                        // @248 m_solverData (IGNORED)
    m_storageIndex = br.ReadUInt16();                  // @252
    m_contactPointCallbackDelay = br.ReadUInt16();     // @254
    br.Skip(16);                                       // @256 m_constraintsMaster (IGNORED)
    des.ReadEmptyArray(br);                            // @272 m_constraintsSlave (IGNORED empty hkArray)
    des.ReadEmptyArray(br);                            // @288 m_constraintRuntime (IGNORED empty hkArray)
    des.ReadEmptyPointer(br);                          // @304 m_simulationIsland (IGNORED)
    m_autoRemoveLevel = br.ReadSByte();                // @312
    m_numShapeKeysInContactPointProperties = br.ReadByte();  // @313
    m_responseModifierFlags = br.ReadByte();           // @314
    br.Skip(1);                                        // pad 315 -> 316
    m_uid = br.ReadUInt32();                            // @316
    m_spuCollisionCallback.Read(des, br);              // @320 (16B)
    m_motion.Read(des, br);                            // @336 (320B)
    br.Skip(16);                                       // @656 m_contactListeners (IGNORED)
    br.Skip(16);                                       // @672 m_actions (IGNORED)
    m_localFrame = des.ReadClassPointer<hkReferencedObject>(br);  // @688
    des.ReadEmptyPointer(br);                          // @696 m_extendedListeners (IGNORED)
    m_npData = br.ReadUInt32();                         // @704
    br.Skip(12);                                        // tail pad 708 -> 720
}
void hkpRigidBody::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkpEntity::Read(des, br);
}
void hkpShapeInfo::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);                       // base 0..15
    m_shape = des.ReadClassPointer<hkpShape>(br);            // @16
    m_isHierarchicalCompound = br.ReadBoolean();             // @24
    m_hkdShapesCollected     = br.ReadBoolean();             // @25
    br.Skip(6);                                              // pad 26 -> 32
    m_childShapeNames  = des.ReadStringPointerArray(br);     // @32
    m_childTransforms  = des.ReadClassArray<hkTransform>(br);// @48
    m_transform.Read(des, br);                               // @64 (64B)
}

// ── Physics.h (constraints / motors / physics systems / ragdoll / mapper) ────────
void hkpConstraintData::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
    m_userData = br.ReadUSize();   // @16 -> @24 (no tail pad; ConstraintType enum has zero width)
}
void hkpConstraintMotor::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
    m_type = br.ReadSByte();   // @16
    br.Skip(7);                // pad 17 -> 24
}
void hkpLimitedForceConstraintMotor::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkpConstraintMotor::Read(des, br);
    m_minForce = br.ReadSingle();   // @24
    m_maxForce = br.ReadSingle();   // @28 -> @32
}
void hkpPositionConstraintMotor::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkpLimitedForceConstraintMotor::Read(des, br);
    m_tau                          = br.ReadSingle();   // @32
    m_damping                      = br.ReadSingle();   // @36
    m_proportionalRecoveryVelocity = br.ReadSingle();   // @40
    m_constantRecoveryVelocity     = br.ReadSingle();   // @44 -> @48
}
void hkpSetLocalTransformsConstraintAtom::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    m_type = br.ReadUInt16();   // @0
    br.Skip(14);                // pad 2 -> 16
    m_transformA.Read(des, br); // @16 (64B)
    m_transformB.Read(des, br); // @80 (64B) -> @144
}
void hkpSetupStabilizationAtom::Read(PackFileDeserializer&, BinaryReaderEx& br) {
    m_type     = br.ReadUInt16();   // @0
    m_enabled  = br.ReadBoolean();  // @2
    br.Skip(1);                     // @3 pad
    m_maxAngle = br.ReadSingle();   // @4
    for (auto& b : m_padding) b = br.ReadByte();   // @8 -> @16 (real serialized [u8;8])
}
void hkpAngMotorConstraintAtom::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    m_type       = br.ReadUInt16();  // @0
    m_isEnabled  = br.ReadBoolean(); // @2
    m_motorAxis  = br.ReadByte();    // @3
    m_initializedOffset = br.ReadInt16();                        // @4
    m_previousTargetAngleOffset = br.ReadInt16();                // @6
    m_correspondingAngLimitSolverResultOffset = br.ReadInt16();  // @8
    br.Skip(2);                      // @10 pad
    m_targetAngle = br.ReadSingle(); // @12
    m_motor = des.ReadClassPointer<hkReferencedObject>(br);      // @16 -> @24
}
void hkpAngFrictionConstraintAtom::Read(PackFileDeserializer&, BinaryReaderEx& br) {
    m_type = br.ReadUInt16();              // @0
    m_isEnabled = br.ReadByte();           // @2
    m_firstFrictionAxis = br.ReadByte();   // @3
    m_numFrictionAxes = br.ReadByte();     // @4
    br.Skip(3);                            // @5 pad
    m_maxFrictionTorque = br.ReadSingle(); // @8 -> @12
}
void hkpAngLimitConstraintAtom::Read(PackFileDeserializer&, BinaryReaderEx& br) {
    m_type = br.ReadUInt16();                 // @0
    m_isEnabled = br.ReadByte();              // @2
    m_limitAxis = br.ReadByte();              // @3
    m_minAngle = br.ReadSingle();             // @4
    m_maxAngle = br.ReadSingle();             // @8
    m_angularLimitsTauFactor = br.ReadSingle();// @12 -> @16
}
void hkp2dAngConstraintAtom::Read(PackFileDeserializer&, BinaryReaderEx& br) {
    m_type = br.ReadUInt16();            // @0
    m_freeRotationAxis = br.ReadByte();  // @2
    br.Skip(1);                          // @3 pad -> @4
}
void hkpBallSocketConstraintAtom::Read(PackFileDeserializer&, BinaryReaderEx& br) {
    m_type = br.ReadUInt16();                        // @0
    m_solvingMethod = br.ReadByte();                 // @2
    m_bodiesToNotify = br.ReadByte();                // @3
    m_velocityStabilizationFactor = br.ReadByte();   // @4
    br.Skip(3);                                      // @5 pad
    m_maxImpulse = br.ReadSingle();                  // @8
    m_inertiaStabilizationFactor = br.ReadSingle();  // @12 -> @16
}
void hkpLimitedHingeConstraintDataAtoms::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    m_transforms.Read(des, br);          // @0   (144B)
    m_setupStabilization.Read(des, br);  // @144 (16B)
    m_angMotor.Read(des, br);            // @160 (24B)
    m_angFriction.Read(des, br);         // @184 (12B)
    m_angLimit.Read(des, br);            // @196 (16B)
    m_2dAng.Read(des, br);               // @212 (4B)
    m_ballSocket.Read(des, br);          // @216 (16B) -> @232
    br.Skip(8);                          // tail pad 232 -> 240
}
void hkpLimitedHingeConstraintData::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkpConstraintData::Read(des, br);   // base 0..24
    br.Skip(8);                         // pad 24 -> 32 (ALIGN_16)
    m_atoms.Read(des, br);              // @32 (240B) -> @272
}
void hkpRagdollMotorConstraintAtom::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    m_type       = br.ReadUInt16();   // @0
    m_isEnabled  = br.ReadBoolean();  // @2
    br.Skip(1);                       // @3 pad
    m_initializedOffset          = br.ReadInt16();   // @4
    m_previousTargetAnglesOffset = br.ReadInt16();   // @6
    br.Skip(8);                       // @8 pad -> 16
    for (auto& v : m_targetBRca) v = br.ReadVector4();  // @16 hkMatrix3 (48B)
    for (auto& m : m_motors) m = des.ReadClassPointer<hkReferencedObject>(br);  // @64 (3×8B) -> 88
    br.Skip(8);                       // tail pad 88 -> 96
}
void hkpTwistLimitConstraintAtom::Read(PackFileDeserializer&, BinaryReaderEx& br) {
    m_type = br.ReadUInt16();     // @0
    m_isEnabled = br.ReadByte();  // @2
    m_twistAxis = br.ReadByte();  // @3
    m_refAxis = br.ReadByte();    // @4
    br.Skip(3);                   // @5 pad -> 8
    m_minAngle = br.ReadSingle();               // @8
    m_maxAngle = br.ReadSingle();               // @12
    m_angularLimitsTauFactor = br.ReadSingle(); // @16 -> @20
}
void hkpConeLimitConstraintAtom::Read(PackFileDeserializer&, BinaryReaderEx& br) {
    m_type = br.ReadUInt16();                    // @0
    m_isEnabled = br.ReadByte();                 // @2
    m_twistAxisInA = br.ReadByte();              // @3
    m_refAxisInB = br.ReadByte();                // @4
    m_angleMeasurementMode = br.ReadByte();      // @5
    m_memOffsetToAngleOffset = br.ReadByte();    // @6
    br.Skip(1);                                  // @7 pad
    m_minAngle = br.ReadSingle();                // @8
    m_maxAngle = br.ReadSingle();                // @12
    m_angularLimitsTauFactor = br.ReadSingle();  // @16 -> @20
}
void hkpRagdollConstraintDataAtoms::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    m_transforms.Read(des, br);          // @0   (144B)
    m_setupStabilization.Read(des, br);  // @144 (16B)
    m_ragdollMotors.Read(des, br);       // @160 (96B)
    m_angFriction.Read(des, br);         // @256 (12B)
    m_twistLimit.Read(des, br);          // @268 (20B)
    m_coneLimit.Read(des, br);           // @288 (20B)
    m_planesLimit.Read(des, br);         // @308 (20B)
    m_ballSocket.Read(des, br);          // @328 (16B) -> @344
    br.Skip(8);                          // tail pad 344 -> 352
}
void hkpRagdollConstraintData::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkpConstraintData::Read(des, br);   // base 0..24
    br.Skip(8);                         // pad 24 -> 32 (ALIGN_16)
    m_atoms.Read(des, br);              // @32 (352B) -> @384
}
void hkpConstraintInstance::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);                            // base 0..15
    des.ReadEmptyPointer(br);                                     // @16 m_owner (IGNORED)
    m_data                = des.ReadClassPointer<hkReferencedObject>(br);  // @24
    m_constraintModifiers = des.ReadClassPointer<hkReferencedObject>(br);  // @32
    m_entities[0]         = des.ReadClassPointer<hkpEntity>(br);  // @40
    m_entities[1]         = des.ReadClassPointer<hkpEntity>(br);  // @48
    m_priority             = br.ReadByte();                       // @56
    m_wantRuntime          = br.ReadBoolean();                    // @57
    m_destructionRemapInfo = br.ReadByte();                       // @58
    br.Skip(5);                                                   // pad 59 -> 64
    br.Skip(16);                                                  // @64 m_listeners smallArray override (zeros, IGNORED)
    m_name     = des.ReadStringPointer(br);                       // @80
    m_userData = br.ReadUSize();                                  // @88
    des.ReadEmptyPointer(br);                                     // @96 m_internal (IGNORED)
    br.Skip(4);                                                   // @104 m_uid (IGNORED value)
    br.Skip(4);                                                   // tail pad 108 -> 112
}
void hkpPhysicsSystem::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
    m_rigidBodies = des.ReadClassPointerArray<hkpRigidBody>(br);        // @16
    m_constraints = des.ReadClassPointerArray<hkReferencedObject>(br);  // @32
    m_actions     = des.ReadClassPointerArray<hkReferencedObject>(br);  // @48
    m_phantoms    = des.ReadClassPointerArray<hkReferencedObject>(br);  // @64
    m_name        = des.ReadStringPointer(br);                          // @80
    m_userData    = br.ReadUSize();                                     // @88
    m_active      = br.ReadBoolean();                                   // @96
    br.Skip(7);                                                         // pad 97 -> 104
}
void hkpPhysicsData::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
    m_worldCinfo = des.ReadClassPointer<hkReferencedObject>(br);   // @16
    m_systems    = des.ReadClassPointerArray<hkpPhysicsSystem>(br);// @24 -> @40
}
void hkaRagdollInstance::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
    m_rigidBodies        = des.ReadClassPointerArray<hkpRigidBody>(br);       // @16
    m_constraints        = des.ReadClassPointerArray<hkReferencedObject>(br); // @32
    m_boneToRigidBodyMap = des.ReadInt32Array(br);                            // @48
    m_skeleton           = des.ReadClassPointer<hkaSkeleton>(br);             // @64 -> @72
}
void hkaSkeletonMapperDataSimpleMapping::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    m_boneA = br.ReadInt16();   // @0
    m_boneB = br.ReadInt16();   // @2
    br.Skip(12);                // pad 4 -> 16
    m_aFromBTransform = des.ReadQSTransform(br);   // @16 -> @64
}
void hkaSkeletonMapperDataChainMapping::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    m_startBoneA = br.ReadInt16();   // @0
    m_endBoneA   = br.ReadInt16();   // @2
    m_startBoneB = br.ReadInt16();   // @4
    m_endBoneB   = br.ReadInt16();   // @6
    br.Skip(8);                      // pad 8 -> 16
    m_startAFromBTransform = des.ReadQSTransform(br);  // @16 -> @64
    m_endAFromBTransform   = des.ReadQSTransform(br);  // @64 -> @112
}
void hkaSkeletonMapperData::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    m_skeletonA = des.ReadClassPointer<hkaSkeleton>(br);   // @0
    m_skeletonB = des.ReadClassPointer<hkaSkeleton>(br);   // @8
    m_simpleMappings = des.ReadClassArray<hkaSkeletonMapperDataSimpleMapping>(br);  // @16
    m_chainMappings  = des.ReadClassArray<hkaSkeletonMapperDataChainMapping>(br);   // @32
    m_unmappedBones  = des.ReadInt16Array(br);            // @48
    m_extractedMotionMapping = des.ReadQSTransform(br);   // @64 -> @112
    m_keepUnmappedLocal = br.ReadBoolean();               // @112
    br.Skip(3);                                           // pad 113 -> 116
    m_mappingType = br.ReadUInt32();                      // @116 -> @120
    br.Skip(8);                                           // tail pad 120 -> 128
}
void hkaSkeletonMapper::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
    m_mapping.Read(des, br);   // @16 (128B) -> @144
}

// ── Project.h ─────────────────────────────────────────────────────────────────
void hkbProjectStringData::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
    m_animationFilenames = des.ReadStringPointerArray(br);
    m_behaviorFilenames  = des.ReadStringPointerArray(br);
    m_characterFilenames = des.ReadStringPointerArray(br);
    m_eventNames         = des.ReadStringPointerArray(br);
    m_animationPath      = des.ReadStringPointer(br);
    m_behaviorPath       = des.ReadStringPointer(br);
    m_characterPath      = des.ReadStringPointer(br);
    m_fullPathToSource   = des.ReadStringPointer(br);
    des.ReadEmptyPointer(br);  // rootPath — SERIALIZE_IGNORED, always null
}
void hkbProjectData::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
    m_worldUpWS = des.ReadVector4(br);
    m_stringData = des.ReadClassPointer<hkbProjectStringData>(br);
    m_defaultEventMode = br.ReadSByte();
    br.Skip(7);
}

// ── registry ────────────────────────────────────────────────────────────────
#define REG(C) HavokRegistry::Register(#C, [] { return std::shared_ptr<IHavokObject>(std::make_shared<C>()); })

// Defined in src/classes/gen/ClassRead_gen.cpp — registers the 52 transpiler-
// generated (Tier-A expansion) classes that aren't in the hand-ported set.
void RegisterGeneratedHavokClasses();

// Defined in src/classes/ClassAnim.cpp — the hka* skeleton slice.
void RegisterAnimationHavokClasses();

void RegisterAllHavokClasses() {
    REG(hkBaseObject); REG(hkReferencedObject); REG(hkbBindable); REG(hkbNode); REG(hkbGenerator);
    REG(hkbEventBase); REG(hkbEvent); REG(hkbEventProperty); REG(hkbEventPayload);
    REG(hkbStringEventPayload); REG(hkbCondition);
    REG(hkbRoleAttribute); REG(hkbVariableInfo); REG(hkbVariableValue); REG(hkbEventInfo);
    REG(hkbVariableBindingSetBinding); REG(hkbVariableBindingSet); REG(hkbVariableValueSet);
    REG(hkbStateMachineTimeInterval); REG(hkbStateMachineTransitionInfo);
    REG(hkbStateMachineTransitionInfoArray); REG(hkbStateMachineEventPropertyArray);
    REG(hkbClipTrigger); REG(hkbClipTriggerArray); REG(hkbBoneWeightArray);
    REG(hkbStateChooser); REG(hkbStateListener);
    REG(hkbStateMachineStateInfo); REG(hkbStateMachine);
    REG(hkbClipGenerator); REG(hkbBlenderGeneratorChild); REG(hkbBlenderGenerator);
    REG(hkbManualSelectorGenerator); REG(BSCyclicBlendTransitionGenerator);
    REG(BSBoneSwitchGeneratorBoneData); REG(BSBoneSwitchGenerator); REG(BSiStateTaggingGenerator);
    REG(hkbBehaviorReferenceGenerator); REG(BGSGamebryoSequenceGenerator);
    REG(hkbTransitionEffect); REG(hkbBlendingTransitionEffect);
    REG(hkbModifier); REG(hkbTwistModifier);
    REG(hkbBehaviorGraphStringData); REG(hkbBehaviorGraphData); REG(hkbBehaviorGraph);
    REG(hkRootLevelContainerNamedVariant); REG(hkRootLevelContainer);
    REG(hkbProjectStringData); REG(hkbProjectData);
    REG(hkMemoryResourceHandleExternalLink); REG(hkMemoryResourceHandle); REG(hkMemoryResourceContainer);
    REG(hkpCapsuleShape);   // hkpShape/SphereRep/Convex bases are abstract (not instantiated)
    REG(hkpRigidBody); REG(hkpShapeInfo);   // the rigid-body subtree's two top-level objects
    // constraints / motors / physics systems / ragdoll + skeleton mapper (concrete objects only;
    // constraint data, atoms, and the mapper-data structs are inline value structs, not registered)
    REG(hkpConstraintInstance);
    REG(hkpLimitedHingeConstraintData); REG(hkpRagdollConstraintData); REG(hkpPositionConstraintMotor);
    REG(hkpPhysicsSystem); REG(hkpPhysicsData);
    REG(hkaRagdollInstance); REG(hkaSkeletonMapper);
    RegisterGeneratedHavokClasses();
    RegisterAnimationHavokClasses();
}

// Eager registration. The lazy on-first-Create trigger moved out with the packfile
// framing (havok-framing's HavokRegistry has no havok-core back-reference — that is what
// breaks havok-io's dependency on havok-core). Populate the typed registry at static-init
// instead, from THIS TU: its class Read() methods are linked wherever the typed
// deserialize path is used, so this initializer is pulled in alongside them.
namespace { const bool g_havokClassesRegistered = (RegisterAllHavokClasses(), true); }

} // namespace havok
