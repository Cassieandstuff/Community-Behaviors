// Per-class Write(PackFileSerializer&, BinaryWriterEx&) definitions for the full
// M1 class subset. Exact ports for the base chain / events / state-machine /
// clip; the remainder reconstructed from the HKX2E field specs (kind + order +
// padding + OMIT). Byte-exactness vs the .NET oracle is verified at M2/M3; here
// every method is structurally faithful and compiles into the serializer engine.

#include "havok/classes/Classes.h"
#include "havok/core/PackFileSerializer.h"

namespace havok {

// ── Base.h ──────────────────────────────────────────────────────────────────
void hkBaseObject::Write(PackFileSerializer&, BinaryWriterEx& bw) const {
    bw.WriteUSize(0);  // vtable slot
}
void hkReferencedObject::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkBaseObject::Write(s, bw);
    bw.WriteUInt16(m_memSizeAndFlags);
    bw.WriteInt16(m_referenceCount);
    if (s._header.PointerSize == 8) bw.Pad(8);
}
void hkbBindable::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    s.WriteClassPointer(bw, m_variableBindingSet);
    s.WriteVoidArray(bw);
    bw.WriteBoolean(m_areBindablesCached);
    bw.Skip(7);
}
void hkbNode::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbBindable::Write(s, bw);
    bw.WriteUInt64(m_userData);
    s.WriteStringPointer(bw, m_name);
    bw.WriteInt16(m_id);
    bw.WriteSByte(m_cloneState);
    s.WriteBooleanCStyleArray(bw, m_padNode);
    bw.Skip(4);
}
void hkbGenerator::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbNode::Write(s, bw);
}

// ── Events.h ────────────────────────────────────────────────────────────────
void hkbEventBase::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    bw.WriteInt32(m_id);
    bw.Skip(4);
    s.WriteClassPointer(bw, m_payload);
}
void hkbEvent::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbEventBase::Write(s, bw);
    s.WriteVoidPointer(bw);  // m_sender
}
void hkbEventProperty::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbEventBase::Write(s, bw);
}
void hkbEventPayload::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
}
void hkbStringEventPayload::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbEventPayload::Write(s, bw);
    s.WriteStringPointer(bw, m_data);
}
void hkbCondition::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
}

// ── Variables.h ─────────────────────────────────────────────────────────────
void hkbRoleAttribute::Write(PackFileSerializer&, BinaryWriterEx& bw) const {
    bw.WriteInt16(m_role);
    bw.WriteInt16(m_flags);
}
void hkbVariableInfo::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    m_role.Write(s, bw);  // inline
    bw.WriteSByte(m_type);
    bw.Skip(1);
}
void hkbVariableValue::Write(PackFileSerializer&, BinaryWriterEx& bw) const {
    bw.WriteInt32(m_value);
}
void hkbEventInfo::Write(PackFileSerializer&, BinaryWriterEx& bw) const {
    bw.WriteUInt32(m_flags);
}
void hkbVariableBindingSetBinding::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    s.WriteStringPointer(bw, m_memberPath);
    s.WriteVoidPointer(bw);  // m_memberClass
    bw.WriteInt32(m_offsetInObjectPlusOne);
    bw.WriteInt32(m_offsetInArrayPlusOne);
    bw.WriteInt32(m_rootVariableIndex);
    bw.WriteInt32(m_variableIndex);
    bw.WriteSByte(m_bitIndex);
    bw.WriteSByte(m_bindingType);
    bw.WriteByte(m_memberType);
    bw.WriteSByte(m_variableType);
    bw.WriteSByte(m_flags);
    bw.Skip(3);
}
void hkbVariableBindingSet::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    s.WriteClassArray(bw, m_bindings);
    bw.WriteInt32(m_indexOfBindingToEnable);
    bw.WriteBoolean(m_hasOutputBinding);
    bw.Skip(3);
}
void hkbVariableValueSet::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    s.WriteClassArray(bw, m_wordVariableValues);
    s.WriteVector4Array(bw, m_quadVariableValues);
    s.WriteClassPointerArray(bw, m_variantVariableValues);
}

// ── Arrays.h ────────────────────────────────────────────────────────────────
void hkbStateMachineTimeInterval::Write(PackFileSerializer&, BinaryWriterEx& bw) const {
    bw.WriteInt32(m_enterEventId);
    bw.WriteInt32(m_exitEventId);
    bw.WriteSingle(m_enterTime);
    bw.WriteSingle(m_exitTime);
}
void hkbStateMachineTransitionInfo::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    m_triggerInterval.Write(s, bw);
    m_initiateInterval.Write(s, bw);
    s.WriteClassPointer(bw, m_transition);
    s.WriteClassPointer(bw, m_condition);
    bw.WriteInt32(m_eventId);
    bw.WriteInt32(m_toStateId);
    bw.WriteInt32(m_fromNestedStateId);
    bw.WriteInt32(m_toNestedStateId);
    bw.WriteInt16(m_priority);
    bw.WriteInt16(m_flags);
    bw.Skip(4);
}
void hkbStateMachineTransitionInfoArray::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    s.WriteClassArray(bw, m_transitions);
}
void hkbStateMachineEventPropertyArray::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    s.WriteClassArray(bw, m_events);
}
void hkbClipTrigger::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    bw.WriteSingle(m_localTime);
    bw.Skip(4);
    m_event.Write(s, bw);  // inline
    bw.WriteBoolean(m_relativeToEndOfClip);
    bw.WriteBoolean(m_acyclic);
    bw.WriteBoolean(m_isAnnotation);
    bw.Skip(5);
}
void hkbClipTriggerArray::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    s.WriteClassArray(bw, m_triggers);
}
void hkbBoneWeightArray::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbBindable::Write(s, bw);
    s.WriteSingleArray(bw, m_boneWeights);
}

// ── StateMachine.h (exact) ──────────────────────────────────────────────────
void hkbStateChooser::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
}
void hkbStateListener::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
}
void hkbStateMachineStateInfo::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbBindable::Write(s, bw);
    s.WriteClassPointerArray(bw, m_listeners);
    s.WriteClassPointer(bw, m_enterNotifyEvents);
    s.WriteClassPointer(bw, m_exitNotifyEvents);
    s.WriteClassPointer(bw, m_transitions);
    s.WriteClassPointer(bw, m_generator);
    s.WriteStringPointer(bw, m_name);
    bw.WriteInt32(m_stateId);
    bw.WriteSingle(m_probability);
    bw.WriteBoolean(m_enable);
    bw.Skip(7);
}
void hkbStateMachine::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbGenerator::Write(s, bw);
    m_eventToSendWhenStateOrTransitionChanges.Write(s, bw);  // inline
    s.WriteClassPointer(bw, m_startStateChooser);
    bw.WriteInt32(m_startStateId);
    bw.WriteInt32(m_returnToPreviousStateEventId);
    bw.WriteInt32(m_randomTransitionEventId);
    bw.WriteInt32(m_transitionToNextHigherStateEventId);
    bw.WriteInt32(m_transitionToNextLowerStateEventId);
    bw.WriteInt32(m_syncVariableIndex);
    bw.WriteInt32(m_currentStateId);
    bw.WriteBoolean(m_wrapAroundStateId);
    bw.WriteSByte(m_maxSimultaneousTransitions);
    bw.WriteSByte(m_startStateMode);
    bw.WriteSByte(m_selfTransitionMode);
    bw.WriteBoolean(m_isActive);
    bw.Skip(7);
    s.WriteClassPointerArray(bw, m_states);
    s.WriteClassPointer(bw, m_wildcardTransitions);
    s.WriteVoidPointer(bw);
    s.WriteVoidArray(bw);
    s.WriteVoidArray(bw);
    s.WriteVoidArray(bw);
    s.WriteVoidArray(bw);
    bw.WriteSingle(m_timeInState);
    bw.WriteSingle(m_lastLocalTime);
    bw.WriteInt32(m_previousStateId);
    bw.WriteInt32(m_nextStartStateIndexOverride);
    bw.WriteBoolean(m_stateOrTransitionChanged);
    bw.WriteBoolean(m_echoNextUpdate);
    bw.WriteUInt16(m_sCurrentStateIndexAndEntered);
    bw.Skip(4);
}

// ── Generators.h ────────────────────────────────────────────────────────────
void hkbClipGenerator::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbGenerator::Write(s, bw);
    s.WriteStringPointer(bw, m_animationName);
    s.WriteClassPointer(bw, m_triggers);
    bw.WriteSingle(m_cropStartAmountLocalTime);
    bw.WriteSingle(m_cropEndAmountLocalTime);
    bw.WriteSingle(m_startTime);
    bw.WriteSingle(m_playbackSpeed);
    bw.WriteSingle(m_enforcedDuration);
    bw.WriteSingle(m_userControlledTimeFraction);
    bw.WriteInt16(m_animationBindingIndex);
    bw.WriteSByte(m_mode);
    bw.WriteSByte(m_flags);
    bw.Skip(4);
    s.WriteVoidArray(bw);
    s.WriteVoidPointer(bw);
    s.WriteVoidPointer(bw);
    s.WriteVoidPointer(bw);
    s.WriteVoidPointer(bw);
    s.WriteVoidPointer(bw);
    s.WriteQSTransform(bw, m_extractedMotion);
    s.WriteVoidArray(bw);
    bw.WriteSingle(m_localTime);
    bw.WriteSingle(m_time);
    bw.WriteSingle(m_previousUserControlledTimeFraction);
    bw.WriteInt32(m_bufferSize);
    bw.WriteInt32(m_echoBufferSize);
    bw.WriteBoolean(m_atEnd);
    bw.WriteBoolean(m_ignoreStartTime);
    bw.WriteBoolean(m_pingPongBackward);
    bw.Skip(9);
}
void hkbBlenderGeneratorChild::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbBindable::Write(s, bw);
    s.WriteClassPointer(bw, m_generator);
    s.WriteClassPointer(bw, m_boneWeights);
    bw.WriteSingle(m_weight);
    bw.WriteSingle(m_worldFromModelWeight);
    bw.Skip(8);
}
void hkbBlenderGenerator::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbGenerator::Write(s, bw);
    bw.WriteSingle(m_referencePoseWeightThreshold);
    bw.WriteSingle(m_blendParameter);
    bw.WriteSingle(m_minCyclicBlendParameter);
    bw.WriteSingle(m_maxCyclicBlendParameter);
    bw.WriteInt16(m_indexOfSyncMasterChild);
    bw.WriteInt16(m_flags);
    bw.WriteBoolean(m_subtractLastChild);
    bw.Skip(3);
    s.WriteClassPointerArray(bw, m_children);
    s.WriteVoidArray(bw);
    s.WriteVoidArray(bw);
    bw.WriteSingle(m_endIntervalWeight);
    bw.WriteInt32(m_numActiveChildren);
    bw.WriteInt16(m_beginIntervalIndex);
    bw.WriteInt16(m_endIntervalIndex);
    bw.WriteBoolean(m_initSync);
    bw.WriteBoolean(m_doSubtractiveBlend);
    bw.Skip(2);
}
void hkbManualSelectorGenerator::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbGenerator::Write(s, bw);
    s.WriteClassPointerArray(bw, m_generators);
    bw.WriteSByte(m_selectedGeneratorIndex);
    bw.WriteSByte(m_currentGeneratorIndex);
    bw.Skip(6);
}
void BSCyclicBlendTransitionGenerator::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbGenerator::Write(s, bw);
    bw.Skip(8);
    s.WriteClassPointer(bw, m_pBlenderGenerator);
    m_EventToFreezeBlendValue.Write(s, bw);  // inline
    m_EventToCrossBlend.Write(s, bw);        // inline
    bw.WriteSingle(m_fBlendParameter);
    bw.WriteSingle(m_fTransitionDuration);
    bw.WriteSByte(m_eBlendCurve);
    bw.Skip(15);
    s.WriteVoidPointer(bw);
    bw.Skip(8);
    s.WriteVoidPointer(bw);
    bw.WriteSByte(m_currentMode);
    bw.Skip(7);
}
void BSBoneSwitchGeneratorBoneData::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbBindable::Write(s, bw);
    s.WriteClassPointer(bw, m_pGenerator);
    s.WriteClassPointer(bw, m_spBoneWeight);
}
void BSBoneSwitchGenerator::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbGenerator::Write(s, bw);
    bw.Skip(8);
    s.WriteClassPointer(bw, m_pDefaultGenerator);
    s.WriteClassPointerArray(bw, m_ChildrenA);
    bw.Skip(8);
}
void BSiStateTaggingGenerator::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbGenerator::Write(s, bw);
    bw.Skip(8);
    s.WriteClassPointer(bw, m_pDefaultGenerator);
    bw.WriteInt32(m_iStateToSetAs);
    bw.WriteInt32(m_iPriority);
}
void hkbBehaviorReferenceGenerator::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbGenerator::Write(s, bw);
    s.WriteStringPointer(bw, m_behaviorName);
    s.WriteVoidPointer(bw);  // m_behavior
}
void BGSGamebryoSequenceGenerator::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbGenerator::Write(s, bw);              // 0..72
    s.WriteStringPointer(bw, m_pSequence);   // 72..80
    bw.WriteSByte(m_eBlendModeFunction);     // 80
    bw.Skip(3);                              // 81..84 pad
    bw.WriteSingle(m_fPercent);              // 84..88
    s.WriteVoidArray(bw);                    // 88..104 m_events (empty; capflags 0x80000000)
    bw.WriteSingle(m_fTime);                 // 104..108 (SERIALIZE_IGNORED, zero)
    bw.WriteBoolean(m_bDelayedActivate);     // 108 (SERIALIZE_IGNORED, zero)
    bw.WriteBoolean(m_bLooping);             // 109 (SERIALIZE_IGNORED, zero)
    bw.Skip(2);                              // 110..112 pad
}

// ── Effects.h ───────────────────────────────────────────────────────────────
void hkbTransitionEffect::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbGenerator::Write(s, bw);
    bw.WriteSByte(m_selfTransitionMode);
    bw.WriteSByte(m_eventMode);
    bw.WriteSByte(m_defaultEventMode);
    bw.Skip(5);
}
void hkbBlendingTransitionEffect::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbTransitionEffect::Write(s, bw);
    bw.WriteSingle(m_duration);
    bw.WriteSingle(m_toGeneratorStartTimeFraction);
    bw.WriteUInt16(m_flags);
    bw.WriteSByte(m_endMode);
    bw.WriteSByte(m_blendCurve);
    bw.Skip(4);
    s.WriteVoidPointer(bw);  // m_fromGenerator
    s.WriteVoidPointer(bw);  // m_toGenerator
    s.WriteVoidArray(bw);    // m_characterPoseAtBeginningOfTransition
    bw.WriteSingle(m_timeRemaining);
    bw.WriteSingle(m_timeInTransition);
    bw.WriteBoolean(m_applySelfTransition);
    bw.WriteBoolean(m_initializeCharacterPose);
    bw.Skip(6);
}

// ── Modifiers.h ─────────────────────────────────────────────────────────────
void hkbModifier::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbNode::Write(s, bw);
    bw.WriteBoolean(m_enable);
    s.WriteBooleanCStyleArray(bw, m_padModifier);
    bw.Skip(4);
}
void hkbTwistModifier::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbModifier::Write(s, bw);
    s.WriteVector4(bw, m_axisOfRotation);
    bw.WriteSingle(m_twistAngle);
    bw.WriteInt16(m_startBoneIndex);
    bw.WriteInt16(m_endBoneIndex);
    bw.WriteSByte(m_setAngleMethod);
    bw.WriteSByte(m_rotationAxisCoordinates);
    bw.WriteBoolean(m_isAdditive);
    bw.Skip(5);   // pad to the 8-byte boundary of the following arrays
    s.WriteInt16Array(bw, m_boneChainIndices);
    s.WriteInt16Array(bw, m_parentBoneIndices);
}

// ── Graph.h ─────────────────────────────────────────────────────────────────
void hkbBehaviorGraphStringData::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    s.WriteStringPointerArray(bw, m_eventNames);
    s.WriteStringPointerArray(bw, m_attributeNames);
    s.WriteStringPointerArray(bw, m_variableNames);
    s.WriteStringPointerArray(bw, m_characterPropertyNames);
}
void hkbBehaviorGraphData::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    s.WriteSingleArray(bw, m_attributeDefaults);
    s.WriteClassArray(bw, m_variableInfos);
    s.WriteClassArray(bw, m_characterPropertyInfos);
    s.WriteClassArray(bw, m_eventInfos);
    s.WriteClassArray(bw, m_wordMinVariableValues);
    s.WriteClassArray(bw, m_wordMaxVariableValues);
    s.WriteClassPointer(bw, m_variableInitialValues);
    s.WriteClassPointer(bw, m_stringData);
}
void hkbBehaviorGraph::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbGenerator::Write(s, bw);
    bw.WriteSByte(m_variableMode);
    bw.Skip(7);
    s.WriteVoidArray(bw);    // m_uniqueIdPool
    s.WriteVoidPointer(bw);  // m_idToStateMachineTemplateMap
    s.WriteVoidArray(bw);    // m_mirroredExternalIdMap
    s.WriteVoidPointer(bw);  // m_pseudoRandomGenerator
    s.WriteClassPointer(bw, m_rootGenerator);
    s.WriteClassPointer(bw, m_data);
    for (int i = 0; i < 14; ++i) s.WriteVoidPointer(bw);  // rootGeneratorClone .. nodePartitionInfo
    bw.WriteInt32(m_numIntermediateOutputs);
    bw.Skip(4);
    s.WriteVoidArray(bw);  // m_jobs
    s.WriteVoidArray(bw);  // m_allPartitionMemory
    bw.WriteInt16(m_numStaticNodes);
    bw.WriteInt16(m_nextUniqueId);
    bw.WriteBoolean(m_isActive);
    bw.WriteBoolean(m_isLinked);
    bw.WriteBoolean(m_updateActiveNodes);
    bw.WriteBoolean(m_stateOrTransitionChanged);
}
void hkRootLevelContainerNamedVariant::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    s.WriteStringPointer(bw, m_name);
    s.WriteStringPointer(bw, m_className);
    s.WriteClassPointer(bw, m_variant);
}
void hkRootLevelContainer::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    s.WriteClassArray(bw, m_namedVariants);
}

// ── Resource.h ────────────────────────────────────────────────────────────────
void hkMemoryResourceHandleExternalLink::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    s.WriteStringPointer(bw, m_memberName);
    s.WriteStringPointer(bw, m_externalId);
}
void hkMemoryResourceHandle::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    s.WriteClassPointer(bw, m_variant);
    s.WriteStringPointer(bw, m_name);
    s.WriteClassArray(bw, m_references);
}
void hkMemoryResourceContainer::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    s.WriteStringPointer(bw, m_name);
    s.WriteClassPointer(bw, nullptr);   // m_parent — SERIALIZE_IGNORED (empty)
    s.WriteClassPointerArray(bw, m_resourceHandles);
    s.WriteClassPointerArray(bw, m_children);
}

// ── Physics.h (collision shapes) ────────────────────────────────────────────────
void hkpShape::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    bw.WriteUSize(m_userData);
    bw.Skip(8);   // m_type (SERIALIZE_IGNORED) + tail pad to 32
}
void hkpSphereRepShape::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkpShape::Write(s, bw);
}
void hkpConvexShape::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkpSphereRepShape::Write(s, bw);
    bw.WriteSingle(m_radius);
    bw.Skip(4);   // pad to 40
}
void hkpCapsuleShape::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkpConvexShape::Write(s, bw);
    bw.Skip(8);   // pad 40 -> 48
    bw.WriteVector4(m_vertexA);
    bw.WriteVector4(m_vertexB);
}

// ── Physics.h (rigid-body subtree) ──────────────────────────────────────────────
void hkTransform::Write(PackFileSerializer&, BinaryWriterEx& bw) const {
    for (const auto& v : m_data) bw.WriteVector4(v);
}
void hkpPropertyValue::Write(PackFileSerializer&, BinaryWriterEx& bw) const {
    bw.WriteUInt64(m_data);
}
void hkMotionState::Write(PackFileSerializer&, BinaryWriterEx& bw) const {
    for (const auto& v : m_transform)      bw.WriteVector4(v);
    for (const auto& v : m_sweptTransform) bw.WriteVector4(v);
    bw.WriteVector4(m_deltaAngle);
    bw.WriteSingle(m_objectRadius);
    bw.WriteHalf(m_linearDamping);
    bw.WriteHalf(m_angularDamping);
    bw.WriteHalf(m_timeFactor);
    bw.WriteByte(m_maxLinearVelocity);
    bw.WriteByte(m_maxAngularVelocity);
    bw.WriteByte(m_deactivationClass);
    bw.Skip(3);
}
void hkpMaterial::Write(PackFileSerializer&, BinaryWriterEx& bw) const {
    bw.WriteByte(m_responseType);
    bw.Skip(1);
    bw.WriteHalf(m_rollingFrictionMultiplier);
    bw.WriteSingle(m_friction);
    bw.WriteSingle(m_restitution);
    bw.Skip(4);
}
void hkpEntitySpuCollisionCallback::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    s.WriteClassPointer(bw, nullptr);
    bw.Skip(2);
    bw.WriteByte(m_eventFilter);
    bw.WriteByte(m_userFilter);
    bw.Skip(4);
}
void hkpProperty::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    bw.WriteUInt32(m_key);
    bw.WriteUInt32(m_alignmentPadding);
    m_value.Write(s, bw);
}
void hkMultiThreadCheck::Write(PackFileSerializer&, BinaryWriterEx& bw) const {
    bw.Skip(12);
}
void hkpTypedBroadPhaseHandle::Write(PackFileSerializer&, BinaryWriterEx& bw) const {
    bw.Skip(4);
    bw.WriteSByte(m_type);
    bw.Skip(1);
    bw.WriteSByte(m_objectQualityType);
    bw.Skip(1);
    bw.WriteUInt32(m_collisionFilterInfo);
}
void hkpCdBody::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    s.WriteClassPointer(bw, m_shape);
    bw.WriteUInt32(m_shapeKey);
    bw.Skip(4);
    s.WriteClassPointer(bw, nullptr);   // m_motion (IGNORED)
    s.WriteClassPointer(bw, nullptr);   // m_parent (IGNORED)
}
void hkpMotion::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    bw.WriteByte(m_type);
    bw.WriteByte(m_deactivationIntegrateCounter);
    bw.WriteUInt16(m_deactivationNumInactiveFrames[0]);
    bw.WriteUInt16(m_deactivationNumInactiveFrames[1]);
    bw.Skip(10);
    m_motionState.Write(s, bw);
    bw.WriteVector4(m_inertiaAndMassInv);
    bw.WriteVector4(m_linearVelocity);
    bw.WriteVector4(m_angularVelocity);
    bw.WriteVector4(m_deactivationRefPosition[0]);
    bw.WriteVector4(m_deactivationRefPosition[1]);
    bw.WriteUInt32(m_deactivationRefOrientation[0]);
    bw.WriteUInt32(m_deactivationRefOrientation[1]);
    s.WriteClassPointer(bw, nullptr);   // m_savedMotion (+nosave)
    bw.WriteUInt16(m_savedQualityTypeIndex);
    bw.WriteHalf(m_gravityFactor);
    bw.Skip(12);
}
void hkpKeyframedRigidMotion::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkpMotion::Write(s, bw);
}
void hkpMaxSizeMotion::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkpKeyframedRigidMotion::Write(s, bw);
}
void hkpCollidable::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkpCdBody::Write(s, bw);
    bw.Skip(1);
    bw.WriteByte(m_forceCollideOntoPpu);
    bw.Skip(2);
    m_broadPhaseHandle.Write(s, bw);
    bw.Skip(56);
    bw.WriteSingle(m_allowedPenetrationDepth);
    bw.Skip(4);
}
void hkpLinkedCollidable::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkpCollidable::Write(s, bw);
    s.WriteVoidArray(bw);   // m_collisionEntries (IGNORED empty hkArray)
}
void hkpWorldObject::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    s.WriteClassPointer(bw, nullptr);   // m_world (IGNORED)
    bw.WriteUSize(m_userData);
    m_collidable.Write(s, bw);
    m_multiThreadCheck.Write(s, bw);
    bw.Skip(4);
    s.WriteStringPointer(bw, m_name);
    s.WriteClassArray(bw, m_properties);
    s.WriteClassPointer(bw, nullptr);   // m_treeData (IGNORED)
}
void hkpEntity::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkpWorldObject::Write(s, bw);
    m_material.Write(s, bw);
    s.WriteVoidPointer(bw);             // m_limitContactImpulseUtilAndFlag (IGNORED)
    bw.WriteSingle(m_damageMultiplier);
    bw.Skip(4);
    s.WriteVoidPointer(bw);             // m_breakableBody (IGNORED)
    bw.Skip(4);                         // m_solverData (IGNORED)
    bw.WriteUInt16(m_storageIndex);
    bw.WriteUInt16(m_contactPointCallbackDelay);
    bw.Skip(16);                        // m_constraintsMaster (IGNORED)
    s.WriteVoidArray(bw);              // m_constraintsSlave (IGNORED empty hkArray)
    s.WriteVoidArray(bw);              // m_constraintRuntime (IGNORED empty hkArray)
    s.WriteVoidPointer(bw);            // m_simulationIsland (IGNORED)
    bw.WriteSByte(m_autoRemoveLevel);
    bw.WriteByte(m_numShapeKeysInContactPointProperties);
    bw.WriteByte(m_responseModifierFlags);
    bw.Skip(1);
    bw.WriteUInt32(m_uid);
    m_spuCollisionCallback.Write(s, bw);
    m_motion.Write(s, bw);
    bw.Skip(16);                        // m_contactListeners (IGNORED)
    bw.Skip(16);                        // m_actions (IGNORED)
    s.WriteClassPointer(bw, m_localFrame);
    s.WriteVoidPointer(bw);            // m_extendedListeners (IGNORED)
    bw.WriteUInt32(m_npData);
    bw.Skip(12);
}
void hkpRigidBody::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkpEntity::Write(s, bw);
}
void hkpShapeInfo::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    s.WriteClassPointer(bw, m_shape);
    bw.WriteBoolean(m_isHierarchicalCompound);
    bw.WriteBoolean(m_hkdShapesCollected);
    bw.Skip(6);
    s.WriteStringPointerArray(bw, m_childShapeNames);
    s.WriteClassArray(bw, m_childTransforms);
    m_transform.Write(s, bw);
}

// ── Physics.h (constraints / motors / physics systems / ragdoll / mapper) ────────
void hkpConstraintData::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    bw.WriteUSize(m_userData);
}
void hkpConstraintMotor::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    bw.WriteSByte(m_type);
    bw.Skip(7);
}
void hkpLimitedForceConstraintMotor::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkpConstraintMotor::Write(s, bw);
    bw.WriteSingle(m_minForce);
    bw.WriteSingle(m_maxForce);
}
void hkpPositionConstraintMotor::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkpLimitedForceConstraintMotor::Write(s, bw);
    bw.WriteSingle(m_tau);
    bw.WriteSingle(m_damping);
    bw.WriteSingle(m_proportionalRecoveryVelocity);
    bw.WriteSingle(m_constantRecoveryVelocity);
}
void hkpSetLocalTransformsConstraintAtom::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    bw.WriteUInt16(m_type);
    bw.Skip(14);
    m_transformA.Write(s, bw);
    m_transformB.Write(s, bw);
}
void hkpSetupStabilizationAtom::Write(PackFileSerializer&, BinaryWriterEx& bw) const {
    bw.WriteUInt16(m_type);
    bw.WriteBoolean(m_enabled);
    bw.Skip(1);
    bw.WriteSingle(m_maxAngle);
    for (auto b : m_padding) bw.WriteByte(b);
}
void hkpAngMotorConstraintAtom::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    bw.WriteUInt16(m_type);
    bw.WriteBoolean(m_isEnabled);
    bw.WriteByte(m_motorAxis);
    bw.WriteInt16(m_initializedOffset);
    bw.WriteInt16(m_previousTargetAngleOffset);
    bw.WriteInt16(m_correspondingAngLimitSolverResultOffset);
    bw.Skip(2);
    bw.WriteSingle(m_targetAngle);
    s.WriteClassPointer(bw, m_motor);
}
void hkpAngFrictionConstraintAtom::Write(PackFileSerializer&, BinaryWriterEx& bw) const {
    bw.WriteUInt16(m_type);
    bw.WriteByte(m_isEnabled);
    bw.WriteByte(m_firstFrictionAxis);
    bw.WriteByte(m_numFrictionAxes);
    bw.Skip(3);
    bw.WriteSingle(m_maxFrictionTorque);
}
void hkpAngLimitConstraintAtom::Write(PackFileSerializer&, BinaryWriterEx& bw) const {
    bw.WriteUInt16(m_type);
    bw.WriteByte(m_isEnabled);
    bw.WriteByte(m_limitAxis);
    bw.WriteSingle(m_minAngle);
    bw.WriteSingle(m_maxAngle);
    bw.WriteSingle(m_angularLimitsTauFactor);
}
void hkp2dAngConstraintAtom::Write(PackFileSerializer&, BinaryWriterEx& bw) const {
    bw.WriteUInt16(m_type);
    bw.WriteByte(m_freeRotationAxis);
    bw.Skip(1);
}
void hkpBallSocketConstraintAtom::Write(PackFileSerializer&, BinaryWriterEx& bw) const {
    bw.WriteUInt16(m_type);
    bw.WriteByte(m_solvingMethod);
    bw.WriteByte(m_bodiesToNotify);
    bw.WriteByte(m_velocityStabilizationFactor);
    bw.Skip(3);
    bw.WriteSingle(m_maxImpulse);
    bw.WriteSingle(m_inertiaStabilizationFactor);
}
void hkpLimitedHingeConstraintDataAtoms::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    m_transforms.Write(s, bw);
    m_setupStabilization.Write(s, bw);
    m_angMotor.Write(s, bw);
    m_angFriction.Write(s, bw);
    m_angLimit.Write(s, bw);
    m_2dAng.Write(s, bw);
    m_ballSocket.Write(s, bw);
    bw.Skip(8);
}
void hkpLimitedHingeConstraintData::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkpConstraintData::Write(s, bw);
    bw.Skip(8);
    m_atoms.Write(s, bw);
}
void hkpRagdollMotorConstraintAtom::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    bw.WriteUInt16(m_type);
    bw.WriteBoolean(m_isEnabled);
    bw.Skip(1);
    bw.WriteInt16(m_initializedOffset);
    bw.WriteInt16(m_previousTargetAnglesOffset);
    bw.Skip(8);
    for (const auto& v : m_targetBRca) bw.WriteVector4(v);
    for (const auto& m : m_motors) s.WriteClassPointer(bw, m);
    bw.Skip(8);
}
void hkpTwistLimitConstraintAtom::Write(PackFileSerializer&, BinaryWriterEx& bw) const {
    bw.WriteUInt16(m_type);
    bw.WriteByte(m_isEnabled);
    bw.WriteByte(m_twistAxis);
    bw.WriteByte(m_refAxis);
    bw.Skip(3);
    bw.WriteSingle(m_minAngle);
    bw.WriteSingle(m_maxAngle);
    bw.WriteSingle(m_angularLimitsTauFactor);
}
void hkpConeLimitConstraintAtom::Write(PackFileSerializer&, BinaryWriterEx& bw) const {
    bw.WriteUInt16(m_type);
    bw.WriteByte(m_isEnabled);
    bw.WriteByte(m_twistAxisInA);
    bw.WriteByte(m_refAxisInB);
    bw.WriteByte(m_angleMeasurementMode);
    bw.WriteByte(m_memOffsetToAngleOffset);
    bw.Skip(1);
    bw.WriteSingle(m_minAngle);
    bw.WriteSingle(m_maxAngle);
    bw.WriteSingle(m_angularLimitsTauFactor);
}
void hkpRagdollConstraintDataAtoms::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    m_transforms.Write(s, bw);
    m_setupStabilization.Write(s, bw);
    m_ragdollMotors.Write(s, bw);
    m_angFriction.Write(s, bw);
    m_twistLimit.Write(s, bw);
    m_coneLimit.Write(s, bw);
    m_planesLimit.Write(s, bw);
    m_ballSocket.Write(s, bw);
    bw.Skip(8);
}
void hkpRagdollConstraintData::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkpConstraintData::Write(s, bw);
    bw.Skip(8);
    m_atoms.Write(s, bw);
}
void hkpConstraintInstance::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    s.WriteVoidPointer(bw);                    // @16 m_owner
    s.WriteClassPointer(bw, m_data);           // @24
    s.WriteClassPointer(bw, m_constraintModifiers);  // @32
    s.WriteClassPointer(bw, m_entities[0]);    // @40
    s.WriteClassPointer(bw, m_entities[1]);    // @48
    bw.WriteByte(m_priority);
    bw.WriteBoolean(m_wantRuntime);
    bw.WriteByte(m_destructionRemapInfo);
    bw.Skip(5);
    bw.Skip(16);                               // @64 m_listeners (zeros)
    s.WriteStringPointer(bw, m_name);
    bw.WriteUSize(m_userData);
    s.WriteVoidPointer(bw);                    // @96 m_internal
    bw.Skip(4);                                // @104 m_uid
    bw.Skip(4);                                // tail pad
}
void hkpPhysicsSystem::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    s.WriteClassPointerArray(bw, m_rigidBodies);
    s.WriteClassPointerArray(bw, m_constraints);
    s.WriteClassPointerArray(bw, m_actions);
    s.WriteClassPointerArray(bw, m_phantoms);
    s.WriteStringPointer(bw, m_name);
    bw.WriteUSize(m_userData);
    bw.WriteBoolean(m_active);
    bw.Skip(7);
}
void hkpPhysicsData::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    s.WriteClassPointer(bw, m_worldCinfo);
    s.WriteClassPointerArray(bw, m_systems);
}
void hkaRagdollInstance::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    s.WriteClassPointerArray(bw, m_rigidBodies);
    s.WriteClassPointerArray(bw, m_constraints);
    s.WriteInt32Array(bw, m_boneToRigidBodyMap);
    s.WriteClassPointer(bw, m_skeleton);
}
void hkaSkeletonMapperDataSimpleMapping::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    bw.WriteInt16(m_boneA);
    bw.WriteInt16(m_boneB);
    bw.Skip(12);
    s.WriteQSTransform(bw, m_aFromBTransform);
}
void hkaSkeletonMapperDataChainMapping::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    bw.WriteInt16(m_startBoneA);
    bw.WriteInt16(m_endBoneA);
    bw.WriteInt16(m_startBoneB);
    bw.WriteInt16(m_endBoneB);
    bw.Skip(8);
    s.WriteQSTransform(bw, m_startAFromBTransform);
    s.WriteQSTransform(bw, m_endAFromBTransform);
}
void hkaSkeletonMapperData::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    s.WriteClassPointer(bw, m_skeletonA);
    s.WriteClassPointer(bw, m_skeletonB);
    s.WriteClassArray(bw, m_simpleMappings);
    s.WriteClassArray(bw, m_chainMappings);
    s.WriteInt16Array(bw, m_unmappedBones);
    s.WriteQSTransform(bw, m_extractedMotionMapping);
    bw.WriteBoolean(m_keepUnmappedLocal);
    bw.Skip(3);
    bw.WriteUInt32(m_mappingType);
    bw.Skip(8);
}
void hkaSkeletonMapper::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    m_mapping.Write(s, bw);
}

// ── Project.h ─────────────────────────────────────────────────────────────────
void hkbProjectStringData::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    s.WriteStringPointerArray(bw, m_animationFilenames);
    s.WriteStringPointerArray(bw, m_behaviorFilenames);
    s.WriteStringPointerArray(bw, m_characterFilenames);
    s.WriteStringPointerArray(bw, m_eventNames);
    s.WriteStringPointer(bw, m_animationPath);
    s.WriteStringPointer(bw, m_behaviorPath);
    s.WriteStringPointer(bw, m_characterPath);
    s.WriteStringPointer(bw, m_fullPathToSource);
    s.WriteVoidPointer(bw);  // rootPath — SERIALIZE_IGNORED, null pointer slot
}
void hkbProjectData::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    s.WriteVector4(bw, m_worldUpWS);
    s.WriteClassPointer(bw, m_stringData);
    bw.WriteSByte(m_defaultEventMode);
    bw.Skip(7);
}

} // namespace havok
