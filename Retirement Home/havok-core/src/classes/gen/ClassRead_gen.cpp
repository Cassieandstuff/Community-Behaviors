// Generated Write/Read bodies for the 52 NEW transpiler classes.
// Integrated from tools/transpile/out/ (TODO-helper markers resolved;
// unnamed-serializer-param transpiler defect fixed).
#include "havok/classes/Classes.h"
#include "havok/classes/gen/ClassesGen.h"
#include "havok/core/HavokRegistry.h"
#include "havok/core/PackFileDeserializer.h"

namespace havok {

#define REG(C) HavokRegistry::Register(#C, [] { return std::shared_ptr<IHavokObject>(std::make_shared<C>()); })

void RegisterGeneratedHavokClasses() {
    REG(BSOffsetAnimationGenerator);
    REG(BSSynchronizedClipGenerator);
    REG(hkbModifierGenerator);
    REG(hkbPoseMatchingGenerator);
    REG(BSDirectAtModifier);
    REG(BSEventEveryNEventsModifier);
    REG(BSEventOnDeactivateModifier);
    REG(BSEventOnFalseToTrueModifier);
    REG(BSInterpValueModifier);
    REG(BSIsActiveModifier);
    REG(BSLookAtModifier);
    REG(BSModifyOnceModifier);
    REG(BSRagdollContactListenerModifier);
    REG(BSSpeedSamplerModifier);
    REG(hkbDampingModifier);
    REG(hkbEvaluateExpressionModifier);
    REG(hkbModifierWrapper);
    REG(hkbEventDrivenModifier);
    REG(hkbEventsFromRangeModifier);
    REG(hkbFootIkControlsModifier);
    REG(hkbGetUpModifier);
    REG(hkbKeyframeBonesModifier);
    REG(hkbModifierList);
    REG(hkbPoweredRagdollControlsModifier);
    REG(hkbRigidBodyRagdollControlsModifier);
    REG(hkbRotateCharacterModifier);
    REG(hkbTimerModifier);
    REG(hkbExpressionCondition);
    REG(hkbStringCondition);
    REG(hkbBoneIndexArray);
    REG(hkbExpressionDataArray);
    REG(hkbEventRangeDataArray);
    REG(hkbMirroredSkeletonInfo);
    REG(hkbCharacterData);
    REG(hkbCharacterStringData);
    REG(hkbFootIkDriverInfo);
    REG(hkbHandIkDriverInfo);
    REG(BSLookAtModifierBoneData);
    REG(hkbExpressionData);
    REG(hkbEventRangeData);
    REG(hkbKeyframeBonesModifierKeyframeInfo);
    REG(hkbFootIkControlsModifierLeg);
    REG(hkbFootIkDriverInfoLeg);
    REG(hkbFootIkControlData);
    REG(hkbFootIkGains);
    REG(hkbHandIkDriverInfoHand);
    REG(hkbWorldFromModelModeData);
    REG(hkbPoweredRagdollControlData);
    REG(hkbRigidBodyRagdollControlData);
    REG(hkaKeyFrameHierarchyUtilityControlData);
    REG(hkbCharacterDataCharacterControllerInfo);
    REG(hkpCharacterControllerCinfo);
    REG(hkbReferencePoseGenerator);
    REG(BSGetTimeStepModifier);
    REG(BSIStateManagerModifier);
    REG(BSDecomposeVectorModifier);
    REG(hkbTransformVectorModifier);
    REG(BSLimbIKModifier);
    REG(BSTweenerModifier);
    REG(BSPassByTargetTriggerModifier);
    REG(BSTimerModifier);
    REG(hkbFootIkModifier);
}

void BSOffsetAnimationGenerator::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbGenerator::Read(des, br);
    br.Skip(8);
    m_pDefaultGenerator = des.ReadClassPointer<hkbGenerator>(br);
    br.Skip(8);
    m_pOffsetClipGenerator = des.ReadClassPointer<hkbGenerator>(br);
    m_fOffsetVariable = br.ReadSingle();
    m_fOffsetRangeStart = br.ReadSingle();
    m_fOffsetRangeEnd = br.ReadSingle();
    br.Skip(4);
    des.ReadEmptyArray(br);
    des.ReadEmptyArray(br);
    m_fCurrentPercentage = br.ReadSingle();
    m_iCurrentFrame = br.ReadUInt32();
    m_bZeroOffset = br.ReadBoolean();
    m_bOffsetValid = br.ReadBoolean();
    br.Skip(14);
}

void BSSynchronizedClipGenerator::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbGenerator::Read(des, br);
    br.Skip(8);
    m_pClipGenerator = des.ReadClassPointer<hkbGenerator>(br);
    m_SyncAnimPrefix = des.ReadCString(br);
    m_bSyncClipIgnoreMarkPlacement = br.ReadBoolean();
    br.Skip(3);
    m_fGetToMarkTime = br.ReadSingle();
    m_fMarkErrorThreshold = br.ReadSingle();
    m_bLeadCharacter = br.ReadBoolean();
    m_bReorientSupportChar = br.ReadBoolean();
    m_bApplyMotionFromRoot = br.ReadBoolean();
    br.Skip(1);
    des.ReadEmptyPointer(br);
    br.Skip(8);
    m_StartMarkWS = des.ReadQSTransform(br);
    m_EndMarkWS = des.ReadQSTransform(br);
    m_StartMarkMS = des.ReadQSTransform(br);
    m_fCurrentLerp = br.ReadSingle();
    br.Skip(4);
    des.ReadEmptyPointer(br);
    des.ReadEmptyPointer(br);
    m_sAnimationBindingIndex = br.ReadInt16();
    m_bAtMark = br.ReadBoolean();
    m_bAllCharactersInScene = br.ReadBoolean();
    m_bAllCharactersAtMarks = br.ReadBoolean();
    br.Skip(3);
}

void hkbModifierGenerator::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbGenerator::Read(des, br);
    m_modifier = des.ReadClassPointer<hkbModifier>(br);
    m_generator = des.ReadClassPointer<hkbGenerator>(br);
}

void hkbPoseMatchingGenerator::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbBlenderGenerator::Read(des, br);
    m_worldFromModelRotation = des.ReadQuaternion(br);
    m_blendSpeed = br.ReadSingle();
    m_minSpeedToSwitch = br.ReadSingle();
    m_minSwitchTimeNoError = br.ReadSingle();
    m_minSwitchTimeFullError = br.ReadSingle();
    m_startPlayingEventId = br.ReadInt32();
    m_startMatchingEventId = br.ReadInt32();
    m_rootBoneIndex = br.ReadInt16();
    m_otherBoneIndex = br.ReadInt16();
    m_anotherBoneIndex = br.ReadInt16();
    m_pelvisIndex = br.ReadInt16();
    m_mode = br.ReadSByte();
    br.Skip(3);
    m_currentMatch = br.ReadInt32();
    m_bestMatch = br.ReadInt32();
    m_timeSinceBetterMatch = br.ReadSingle();
    m_error = br.ReadSingle();
    m_resetCurrentMatchLocalTime = br.ReadBoolean();
    br.Skip(3);
    des.ReadEmptyPointer(br);
}

void BSDirectAtModifier::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbModifier::Read(des, br);
    m_directAtTarget = br.ReadBoolean();
    br.Skip(1);
    m_sourceBoneIndex = br.ReadInt16();
    m_startBoneIndex = br.ReadInt16();
    m_endBoneIndex = br.ReadInt16();
    m_limitHeadingDegrees = br.ReadSingle();
    m_limitPitchDegrees = br.ReadSingle();
    m_offsetHeadingDegrees = br.ReadSingle();
    m_offsetPitchDegrees = br.ReadSingle();
    m_onGain = br.ReadSingle();
    m_offGain = br.ReadSingle();
    m_targetLocation = br.ReadVector4();
    m_userInfo = br.ReadUInt32();
    m_directAtCamera = br.ReadBoolean();
    br.Skip(3);
    m_directAtCameraX = br.ReadSingle();
    m_directAtCameraY = br.ReadSingle();
    m_directAtCameraZ = br.ReadSingle();
    m_active = br.ReadBoolean();
    br.Skip(3);
    m_currentHeadingOffset = br.ReadSingle();
    m_currentPitchOffset = br.ReadSingle();
    m_timeStep = br.ReadSingle();
    br.Skip(4);
    des.ReadEmptyPointer(br);
    m_hasTarget = br.ReadBoolean();
    br.Skip(15);
    m_directAtTargetLocation = br.ReadVector4();
    des.ReadEmptyArray(br);
}

void BSEventEveryNEventsModifier::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbModifier::Read(des, br);
    m_eventToCheckFor.Read(des, br);
    m_eventToSend.Read(des, br);
    m_numberOfEventsBeforeSend = br.ReadSByte();
    m_minimumNumberOfEventsBeforeSend = br.ReadSByte();
    m_randomizeNumberOfEvents = br.ReadBoolean();
    br.Skip(1);
    m_numberOfEventsSeen = br.ReadInt32();
    m_calculatedNumberOfEventsBeforeSend = br.ReadSByte();
    br.Skip(7);
}

void BSEventOnDeactivateModifier::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbModifier::Read(des, br);
    m_event.Read(des, br);
}

void BSEventOnFalseToTrueModifier::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbModifier::Read(des, br);
    m_bEnableEvent1 = br.ReadBoolean();
    m_bVariableToTest1 = br.ReadBoolean();
    br.Skip(6);
    m_EventToSend1.Read(des, br);
    m_bEnableEvent2 = br.ReadBoolean();
    m_bVariableToTest2 = br.ReadBoolean();
    br.Skip(6);
    m_EventToSend2.Read(des, br);
    m_bEnableEvent3 = br.ReadBoolean();
    m_bVariableToTest3 = br.ReadBoolean();
    br.Skip(6);
    m_EventToSend3.Read(des, br);
    m_bSlot1ActivatedLastFrame = br.ReadBoolean();
    m_bSlot2ActivatedLastFrame = br.ReadBoolean();
    m_bSlot3ActivatedLastFrame = br.ReadBoolean();
    br.Skip(5);
}

void BSInterpValueModifier::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbModifier::Read(des, br);
    m_source = br.ReadSingle();
    m_target = br.ReadSingle();
    m_result = br.ReadSingle();
    m_gain = br.ReadSingle();
    m_timeStep = br.ReadSingle();
    br.Skip(4);
}

void BSIsActiveModifier::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbModifier::Read(des, br);
    m_bIsActive0 = br.ReadBoolean();
    m_bInvertActive0 = br.ReadBoolean();
    m_bIsActive1 = br.ReadBoolean();
    m_bInvertActive1 = br.ReadBoolean();
    m_bIsActive2 = br.ReadBoolean();
    m_bInvertActive2 = br.ReadBoolean();
    m_bIsActive3 = br.ReadBoolean();
    m_bInvertActive3 = br.ReadBoolean();
    m_bIsActive4 = br.ReadBoolean();
    m_bInvertActive4 = br.ReadBoolean();
    br.Skip(6);
}

void BSLookAtModifier::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbModifier::Read(des, br);
    m_lookAtTarget = br.ReadBoolean();
    br.Skip(7);
    m_bones = des.ReadClassArray<BSLookAtModifierBoneData>(br);
    m_eyeBones = des.ReadClassArray<BSLookAtModifierBoneData>(br);
    m_limitAngleDegrees = br.ReadSingle();
    m_limitAngleThresholdDegrees = br.ReadSingle();
    m_continueLookOutsideOfLimit = br.ReadBoolean();
    br.Skip(3);
    m_onGain = br.ReadSingle();
    m_offGain = br.ReadSingle();
    m_useBoneGains = br.ReadBoolean();
    br.Skip(3);
    m_targetLocation = br.ReadVector4();
    m_targetOutsideLimits = br.ReadBoolean();
    br.Skip(7);
    m_targetOutOfLimitEvent.Read(des, br);
    m_lookAtCamera = br.ReadBoolean();
    br.Skip(3);
    m_lookAtCameraX = br.ReadSingle();
    m_lookAtCameraY = br.ReadSingle();
    m_lookAtCameraZ = br.ReadSingle();
    m_timeStep = br.ReadSingle();
    m_ballBonesValid = br.ReadBoolean();
    br.Skip(3);
    des.ReadEmptyPointer(br);
    br.Skip(8);
}

void BSModifyOnceModifier::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbModifier::Read(des, br);
    m_pOnActivateModifier = des.ReadClassPointer<hkbModifier>(br);
    br.Skip(8);
    m_pOnDeactivateModifier = des.ReadClassPointer<hkbModifier>(br);
    br.Skip(8);
}

void BSRagdollContactListenerModifier::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbModifier::Read(des, br);
    br.Skip(8);
    m_contactEvent.Read(des, br);
    m_bones = des.ReadClassPointer<hkbBoneIndexArray>(br);
    m_throwEvent = br.ReadBoolean();
    br.Skip(7);
    des.ReadEmptyArray(br);
}

void BSSpeedSamplerModifier::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbModifier::Read(des, br);
    m_state = br.ReadInt32();
    m_direction = br.ReadSingle();
    m_goalSpeed = br.ReadSingle();
    m_speedOut = br.ReadSingle();
}

void hkbDampingModifier::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbModifier::Read(des, br);
    m_kP = br.ReadSingle();
    m_kI = br.ReadSingle();
    m_kD = br.ReadSingle();
    m_enableScalarDamping = br.ReadBoolean();
    m_enableVectorDamping = br.ReadBoolean();
    br.Skip(2);
    m_rawValue = br.ReadSingle();
    m_dampedValue = br.ReadSingle();
    br.Skip(8);
    m_rawVector = br.ReadVector4();
    m_dampedVector = br.ReadVector4();
    m_vecErrorSum = br.ReadVector4();
    m_vecPreviousError = br.ReadVector4();
    m_errorSum = br.ReadSingle();
    m_previousError = br.ReadSingle();
    br.Skip(8);
}

void hkbEvaluateExpressionModifier::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbModifier::Read(des, br);
    m_expressions = des.ReadClassPointer<hkbExpressionDataArray>(br);
    des.ReadEmptyPointer(br);
    des.ReadEmptyArray(br);
}

void hkbModifierWrapper::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbModifier::Read(des, br);
    m_modifier = des.ReadClassPointer<hkbModifier>(br);
}

void hkbEventDrivenModifier::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbModifierWrapper::Read(des, br);
    m_activateEventId = br.ReadInt32();
    m_deactivateEventId = br.ReadInt32();
    m_activeByDefault = br.ReadBoolean();
    m_isActive = br.ReadBoolean();
    br.Skip(6);
}

void hkbEventsFromRangeModifier::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbModifier::Read(des, br);
    m_inputValue = br.ReadSingle();
    m_lowerBound = br.ReadSingle();
    m_eventRanges = des.ReadClassPointer<hkbEventRangeDataArray>(br);
    des.ReadEmptyArray(br);
}

void hkbFootIkControlsModifier::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbModifier::Read(des, br);
    m_controlData.Read(des, br);
    m_legs = des.ReadClassArray<hkbFootIkControlsModifierLeg>(br);
    m_errorOutTranslation = br.ReadVector4();
    m_alignWithGroundRotation = des.ReadQuaternion(br);
}

void hkbGetUpModifier::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbModifier::Read(des, br);
    m_groundNormal = br.ReadVector4();
    m_duration = br.ReadSingle();
    m_alignWithGroundDuration = br.ReadSingle();
    m_rootBoneIndex = br.ReadInt16();
    m_otherBoneIndex = br.ReadInt16();
    m_anotherBoneIndex = br.ReadInt16();
    br.Skip(2);
    m_timeSinceBegin = br.ReadSingle();
    m_timeStep = br.ReadSingle();
    m_initNextModify = br.ReadBoolean();
    br.Skip(7);
}

void hkbKeyframeBonesModifier::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbModifier::Read(des, br);
    m_keyframeInfo = des.ReadClassArray<hkbKeyframeBonesModifierKeyframeInfo>(br);
    m_keyframedBonesList = des.ReadClassPointer<hkbBoneIndexArray>(br);
}

void hkbModifierList::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbModifier::Read(des, br);
    m_modifiers = des.ReadClassPointerArray<hkbModifier>(br);
}

void hkbPoweredRagdollControlsModifier::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbModifier::Read(des, br);
    m_controlData.Read(des, br);
    m_bones = des.ReadClassPointer<hkbBoneIndexArray>(br);
    m_worldFromModelModeData.Read(des, br);
    m_boneWeights = des.ReadClassPointer<hkbBoneWeightArray>(br);
    br.Skip(8);
}

void hkbRigidBodyRagdollControlsModifier::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbModifier::Read(des, br);
    m_controlData.Read(des, br);
    m_bones = des.ReadClassPointer<hkbBoneIndexArray>(br);
    br.Skip(8);
}

void hkbRotateCharacterModifier::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbModifier::Read(des, br);
    m_degreesPerSecond = br.ReadSingle();
    m_speedMultiplier = br.ReadSingle();
    br.Skip(8);
    m_axisOfRotation = br.ReadVector4();
    m_angle = br.ReadSingle();
    br.Skip(12);
}

void hkbTimerModifier::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbModifier::Read(des, br);
    m_alarmTimeSeconds = br.ReadSingle();
    br.Skip(4);
    m_alarmEvent.Read(des, br);
    m_secondsElapsed = br.ReadSingle();
    br.Skip(4);
}

void hkbExpressionCondition::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbCondition::Read(des, br);
    m_expression = des.ReadStringPointer(br);
    des.ReadEmptyPointer(br);
}

void hkbStringCondition::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbCondition::Read(des, br);
    m_conditionString = des.ReadStringPointer(br);
}

void hkbBoneIndexArray::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbBindable::Read(des, br);
    m_boneIndices = des.ReadInt16Array(br);
}

void hkbExpressionDataArray::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
    m_expressionsData = des.ReadClassArray<hkbExpressionData>(br);
}

void hkbEventRangeDataArray::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
    m_eventData = des.ReadClassArray<hkbEventRangeData>(br);
}

void hkbMirroredSkeletonInfo::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
    m_mirrorAxis = br.ReadVector4();
    m_bonePairMap = des.ReadInt16Array(br);
}

void hkbCharacterData::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
    m_characterControllerInfo.Read(des, br);
    br.Skip(8);
    m_modelUpMS = br.ReadVector4();
    m_modelForwardMS = br.ReadVector4();
    m_modelRightMS = br.ReadVector4();
    m_characterPropertyInfos = des.ReadClassArray<hkbVariableInfo>(br);
    m_numBonesPerLod = des.ReadInt32Array(br);
    m_characterPropertyValues = des.ReadClassPointer<hkbVariableValueSet>(br);
    m_footIkDriverInfo = des.ReadClassPointer<hkbFootIkDriverInfo>(br);
    m_handIkDriverInfo = des.ReadClassPointer<hkbHandIkDriverInfo>(br);
    m_stringData = des.ReadClassPointer<hkbCharacterStringData>(br);
    m_mirroredSkeletonInfo = des.ReadClassPointer<hkbMirroredSkeletonInfo>(br);
    m_scale = br.ReadSingle();
    m_numHands = br.ReadInt16();
    m_numFloatSlots = br.ReadInt16();
}

void hkbCharacterStringData::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
    m_deformableSkinNames = des.ReadStringPointerArray(br);
    m_rigidSkinNames = des.ReadStringPointerArray(br);
    m_animationNames = des.ReadStringPointerArray(br);
    m_animationFilenames = des.ReadStringPointerArray(br);
    m_characterPropertyNames = des.ReadStringPointerArray(br);
    m_retargetingSkeletonMapperFilenames = des.ReadStringPointerArray(br);
    m_lodNames = des.ReadStringPointerArray(br);
    m_mirroredSyncPointSubstringsA = des.ReadStringPointerArray(br);
    m_mirroredSyncPointSubstringsB = des.ReadStringPointerArray(br);
    m_name = des.ReadStringPointer(br);
    m_rigName = des.ReadStringPointer(br);
    m_ragdollName = des.ReadStringPointer(br);
    m_behaviorFilename = des.ReadStringPointer(br);
}

void hkbFootIkDriverInfo::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
    m_legs = des.ReadClassArray<hkbFootIkDriverInfoLeg>(br);
    m_raycastDistanceUp = br.ReadSingle();
    m_raycastDistanceDown = br.ReadSingle();
    m_originalGroundHeightMS = br.ReadSingle();
    m_verticalOffset = br.ReadSingle();
    m_collisionFilterInfo = br.ReadUInt32();
    m_forwardAlignFraction = br.ReadSingle();
    m_sidewaysAlignFraction = br.ReadSingle();
    m_sidewaysSampleWidth = br.ReadSingle();
    m_lockFeetWhenPlanted = br.ReadBoolean();
    m_useCharacterUpVector = br.ReadBoolean();
    m_isQuadrupedNarrow = br.ReadBoolean();
    br.Skip(5);
}

void hkbHandIkDriverInfo::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
    m_hands = des.ReadClassArray<hkbHandIkDriverInfoHand>(br);
    m_fadeInOutCurve = br.ReadSByte();
    br.Skip(7);
}

void BSLookAtModifierBoneData::Read(PackFileDeserializer&, BinaryReaderEx& br) {
    m_index = br.ReadInt16();
    br.Skip(14);
    m_fwdAxisLS = br.ReadVector4();
    m_limitAngleDegrees = br.ReadSingle();
    m_onGain = br.ReadSingle();
    m_offGain = br.ReadSingle();
    m_enabled = br.ReadBoolean();
    br.Skip(3);
    m_currentFwdAxisLS = br.ReadVector4();
}

void hkbExpressionData::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    m_expression = des.ReadStringPointer(br);
    m_assignmentVariableIndex = br.ReadInt32();
    m_assignmentEventIndex = br.ReadInt32();
    m_eventMode = br.ReadSByte();
    m_raisedEvent = br.ReadBoolean();
    m_wasTrueInPreviousFrame = br.ReadBoolean();
    br.Skip(5);
}

void hkbEventRangeData::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    m_upperBound = br.ReadSingle();
    br.Skip(4);
    m_event.Read(des, br);
    m_eventMode = br.ReadSByte();
    br.Skip(7);
}

void hkbKeyframeBonesModifierKeyframeInfo::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    m_keyframedPosition = br.ReadVector4();
    m_keyframedRotation = des.ReadQuaternion(br);
    m_boneIndex = br.ReadInt16();
    m_isValid = br.ReadBoolean();
    br.Skip(13);
}

void hkbFootIkControlsModifierLeg::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    m_groundPosition = br.ReadVector4();
    m_ungroundedEvent.Read(des, br);
    m_verticalError = br.ReadSingle();
    m_hitSomething = br.ReadBoolean();
    m_isPlantedMS = br.ReadBoolean();
    br.Skip(10);
}

void hkbFootIkDriverInfoLeg::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    m_prevAnkleRotLS = des.ReadQuaternion(br);
    m_kneeAxisLS = br.ReadVector4();
    m_footEndLS = br.ReadVector4();
    m_footPlantedAnkleHeightMS = br.ReadSingle();
    m_footRaisedAnkleHeightMS = br.ReadSingle();
    m_maxAnkleHeightMS = br.ReadSingle();
    m_minAnkleHeightMS = br.ReadSingle();
    m_maxKneeAngleDegrees = br.ReadSingle();
    m_minKneeAngleDegrees = br.ReadSingle();
    m_maxAnkleAngleDegrees = br.ReadSingle();
    m_hipIndex = br.ReadInt16();
    m_kneeIndex = br.ReadInt16();
    m_ankleIndex = br.ReadInt16();
    br.Skip(14);
}

void hkbFootIkControlData::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    m_gains.Read(des, br);
}

void hkbFootIkGains::Read(PackFileDeserializer&, BinaryReaderEx& br) {
    m_onOffGain = br.ReadSingle();
    m_groundAscendingGain = br.ReadSingle();
    m_groundDescendingGain = br.ReadSingle();
    m_footPlantedGain = br.ReadSingle();
    m_footRaisedGain = br.ReadSingle();
    m_footUnlockGain = br.ReadSingle();
    m_worldFromModelFeedbackGain = br.ReadSingle();
    m_errorUpDownBias = br.ReadSingle();
    m_alignWorldFromModelGain = br.ReadSingle();
    m_hipOrientationGain = br.ReadSingle();
    m_maxKneeAngleDifference = br.ReadSingle();
    m_ankleOrientationGain = br.ReadSingle();
}

void hkbHandIkDriverInfoHand::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    m_elbowAxisLS = br.ReadVector4();
    m_backHandNormalLS = br.ReadVector4();
    m_handOffsetLS = br.ReadVector4();
    m_handOrienationOffsetLS = des.ReadQuaternion(br);
    m_maxElbowAngleDegrees = br.ReadSingle();
    m_minElbowAngleDegrees = br.ReadSingle();
    m_shoulderIndex = br.ReadInt16();
    m_shoulderSiblingIndex = br.ReadInt16();
    m_elbowIndex = br.ReadInt16();
    m_elbowSiblingIndex = br.ReadInt16();
    m_wristIndex = br.ReadInt16();
    m_enforceEndPosition = br.ReadBoolean();
    m_enforceEndRotation = br.ReadBoolean();
    br.Skip(4);
    m_localFrameName = des.ReadStringPointer(br);
}

void hkbWorldFromModelModeData::Read(PackFileDeserializer&, BinaryReaderEx& br) {
    m_poseMatchingBone0 = br.ReadInt16();
    m_poseMatchingBone1 = br.ReadInt16();
    m_poseMatchingBone2 = br.ReadInt16();
    m_mode = br.ReadSByte();
    br.Skip(1);
}

void hkbPoweredRagdollControlData::Read(PackFileDeserializer&, BinaryReaderEx& br) {
    m_maxForce = br.ReadSingle();
    m_tau = br.ReadSingle();
    m_damping = br.ReadSingle();
    m_proportionalRecoveryVelocity = br.ReadSingle();
    m_constantRecoveryVelocity = br.ReadSingle();
    br.Skip(12);
}

void hkbRigidBodyRagdollControlData::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    m_keyFrameHierarchyControlData.Read(des, br);
    m_durationToBlend = br.ReadSingle();
    br.Skip(12);
}

void hkaKeyFrameHierarchyUtilityControlData::Read(PackFileDeserializer&, BinaryReaderEx& br) {
    m_hierarchyGain = br.ReadSingle();
    m_velocityDamping = br.ReadSingle();
    m_accelerationGain = br.ReadSingle();
    m_velocityGain = br.ReadSingle();
    m_positionGain = br.ReadSingle();
    m_positionMaxLinearVelocity = br.ReadSingle();
    m_positionMaxAngularVelocity = br.ReadSingle();
    m_snapGain = br.ReadSingle();
    m_snapMaxLinearVelocity = br.ReadSingle();
    m_snapMaxAngularVelocity = br.ReadSingle();
    m_snapMaxLinearDistance = br.ReadSingle();
    m_snapMaxAngularDistance = br.ReadSingle();
}

void hkbCharacterDataCharacterControllerInfo::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    m_capsuleHeight = br.ReadSingle();
    m_capsuleRadius = br.ReadSingle();
    m_collisionFilterInfo = br.ReadUInt32();
    br.Skip(4);
    m_characterControllerCinfo = des.ReadClassPointer<hkpCharacterControllerCinfo>(br);
}

void hkpCharacterControllerCinfo::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
}

// ── creature-graph classes (hand-ported; layouts from the vanilla binaries) ──────
void hkbReferencePoseGenerator::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbGenerator::Read(des, br);
    des.ReadEmptyPointer(br);   // m_skeleton (SERIALIZE_IGNORED, null) — size 80
}

void BSGetTimeStepModifier::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbModifier::Read(des, br);
    m_timeStep = br.ReadSingle();
    br.Skip(4);                 // pad to size 88 (8-aligned)
}

void BSIStateManagerModifierBSiStateData::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    m_pStateMachine = des.ReadClassPointer<hkbStateMachine>(br);
    m_StateID       = br.ReadInt32();
    m_iStateToSetAs = br.ReadInt32();   // size 16
}

void BSIStateManagerModifier::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbModifier::Read(des, br);
    m_iStateVar = br.ReadInt32();
    br.Skip(4);                 // align the array pointer to 8
    m_stateData = des.ReadClassArray<BSIStateManagerModifierBSiStateData>(br);   // size 104
}

// ── dragon-only classes (layouts from serde-hkx, offsets in each header) ──
void BSDecomposeVectorModifier::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbModifier::Read(des, br);
    m_vector = br.ReadVector4();
    m_x = br.ReadSingle();
    m_y = br.ReadSingle();
    m_z = br.ReadSingle();
    m_w = br.ReadSingle();      // size 112
}

void hkbTransformVectorModifier::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbModifier::Read(des, br);
    m_rotation    = des.ReadQuaternion(br);
    m_translation = br.ReadVector4();
    m_vectorIn    = br.ReadVector4();
    m_vectorOut   = br.ReadVector4();
    m_rotateOnly        = br.ReadBoolean();
    m_inverse           = br.ReadBoolean();
    m_computeOnActivate = br.ReadBoolean();
    m_computeOnModify   = br.ReadBoolean();
    br.Skip(12);               // pad to size 160
}

void BSLimbIKModifier::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbModifier::Read(des, br);
    m_limitAngleDegrees = br.ReadSingle();
    br.Skip(4);                // currentAngle (ignored)
    m_startBoneIndex = br.ReadInt16();
    m_endBoneIndex   = br.ReadInt16();
    m_gain       = br.ReadSingle();
    m_boneRadius = br.ReadSingle();
    m_castOffset = br.ReadSingle();
    br.Skip(4);                // timeStep (ignored)
    br.Skip(4);                // pad to align pSkeletonMemory at +112
    des.ReadEmptyPointer(br);  // pSkeletonMemory (ignored, null) -> size 120
}

void BSTweenerModifier::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbModifier::Read(des, br);
    m_tweenPosition    = br.ReadBoolean();
    m_tweenRotation    = br.ReadBoolean();
    m_useTweenDuration = br.ReadBoolean();
    br.Skip(1);                // pad to 4-align tweenDuration
    m_tweenDuration    = br.ReadSingle();
    br.Skip(8);                // pad to 16-align targetPosition
    m_targetPosition   = br.ReadVector4();
    m_targetRotation   = des.ReadQuaternion(br);
    br.Skip(80);               // duration+startTransform+time (ignored) -> size 208
}

void BSPassByTargetTriggerModifier::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbModifier::Read(des, br);
    m_targetPosition = br.ReadVector4();
    m_radius = br.ReadSingle();
    br.Skip(12);               // pad to 16-align movementDirection
    m_movementDirection = br.ReadVector4();
    m_triggerEvent.Read(des, br);
    br.Skip(16);               // targetPassed(bool)+pad -> size 160
}

void BSTimerModifier::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbModifier::Read(des, br);
    m_alarmTimeSeconds = br.ReadSingle();
    br.Skip(4);                // pad to 8-align alarmEvent
    m_alarmEvent.Read(des, br);
    m_resetAlarm = br.ReadBoolean();
    br.Skip(7);                // pad + secondsElapsed(ignored) -> size 112
}

void hkbFootIkModifierLeg::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    br.Skip(48);               // originalAnkleTransformMS (hkQsTransform) — runtime state
    m_prevAnkleRotLS = des.ReadQuaternion(br);
    m_kneeAxisLS = br.ReadVector4();
    m_footEndLS  = br.ReadVector4();
    m_ungroundedEvent.Read(des, br);
    m_footPlantedAnkleHeightMS = br.ReadSingle();
    m_footRaisedAnkleHeightMS  = br.ReadSingle();
    m_maxAnkleHeightMS = br.ReadSingle();
    m_minAnkleHeightMS = br.ReadSingle();
    m_maxKneeAngleDegrees = br.ReadSingle();
    m_minKneeAngleDegrees = br.ReadSingle();
    m_verticalError = br.ReadSingle();
    m_maxAnkleAngleDegrees = br.ReadSingle();
    m_hipIndex   = br.ReadInt16();
    m_kneeIndex  = br.ReadInt16();
    m_ankleIndex = br.ReadInt16();
    m_hitSomething = br.ReadBoolean();
    m_isPlantedMS  = br.ReadBoolean();
    m_isOriginalAnkleTransformMSSet = br.ReadBoolean();
    br.Skip(7);                // pad to size 160
}

void hkbFootIkModifier::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkbModifier::Read(des, br);
    m_gains.Read(des, br);
    m_legs = des.ReadClassArray<hkbFootIkModifierLeg>(br);
    m_raycastDistanceUp      = br.ReadSingle();
    m_raycastDistanceDown    = br.ReadSingle();
    m_originalGroundHeightMS = br.ReadSingle();
    m_errorOut               = br.ReadSingle();
    m_errorOutTranslation     = br.ReadVector4();
    m_alignWithGroundRotation = des.ReadQuaternion(br);
    m_verticalOffset       = br.ReadSingle();
    m_collisionFilterInfo  = br.ReadUInt32();
    m_forwardAlignFraction = br.ReadSingle();
    m_sidewaysAlignFraction = br.ReadSingle();
    m_sidewaysSampleWidth  = br.ReadSingle();
    m_useTrackData         = br.ReadBoolean();
    m_lockFeetWhenPlanted  = br.ReadBoolean();
    m_useCharacterUpVector = br.ReadBoolean();
    m_alignMode            = br.ReadSByte();
    br.Skip(40);               // internalLegData + runtime state (ignored) -> size 256
}

} // namespace havok
