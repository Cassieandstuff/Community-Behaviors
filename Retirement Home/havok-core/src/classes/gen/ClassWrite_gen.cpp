// Generated Write/Read bodies for the 52 NEW transpiler classes.
// Integrated from tools/transpile/out/ (TODO-helper markers resolved;
// unnamed-serializer-param transpiler defect fixed).
#include "havok/classes/Classes.h"
#include "havok/classes/gen/ClassesGen.h"
#include "havok/core/PackFileSerializer.h"

namespace havok {

void BSOffsetAnimationGenerator::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbGenerator::Write(s, bw);
    bw.Skip(8);
    s.WriteClassPointer(bw, m_pDefaultGenerator);
    bw.Skip(8);
    s.WriteClassPointer(bw, m_pOffsetClipGenerator);
    bw.WriteSingle(m_fOffsetVariable);
    bw.WriteSingle(m_fOffsetRangeStart);
    bw.WriteSingle(m_fOffsetRangeEnd);
    bw.Skip(4);
    s.WriteVoidArray(bw);
    s.WriteVoidArray(bw);
    bw.WriteSingle(m_fCurrentPercentage);
    bw.WriteUInt32(m_iCurrentFrame);
    bw.WriteBoolean(m_bZeroOffset);
    bw.WriteBoolean(m_bOffsetValid);
    bw.Skip(14);
}

void BSSynchronizedClipGenerator::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbGenerator::Write(s, bw);
    bw.Skip(8);
    s.WriteClassPointer(bw, m_pClipGenerator);
    s.WriteCString(bw, m_SyncAnimPrefix);
    bw.WriteBoolean(m_bSyncClipIgnoreMarkPlacement);
    bw.Skip(3);
    bw.WriteSingle(m_fGetToMarkTime);
    bw.WriteSingle(m_fMarkErrorThreshold);
    bw.WriteBoolean(m_bLeadCharacter);
    bw.WriteBoolean(m_bReorientSupportChar);
    bw.WriteBoolean(m_bApplyMotionFromRoot);
    bw.Skip(1);
    s.WriteVoidPointer(bw);
    bw.Skip(8);
    s.WriteQSTransform(bw, m_StartMarkWS);
    s.WriteQSTransform(bw, m_EndMarkWS);
    s.WriteQSTransform(bw, m_StartMarkMS);
    bw.WriteSingle(m_fCurrentLerp);
    bw.Skip(4);
    s.WriteVoidPointer(bw);
    s.WriteVoidPointer(bw);
    bw.WriteInt16(m_sAnimationBindingIndex);
    bw.WriteBoolean(m_bAtMark);
    bw.WriteBoolean(m_bAllCharactersInScene);
    bw.WriteBoolean(m_bAllCharactersAtMarks);
    bw.Skip(3);
}

void hkbModifierGenerator::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbGenerator::Write(s, bw);
    s.WriteClassPointer(bw, m_modifier);
    s.WriteClassPointer(bw, m_generator);
}

void hkbPoseMatchingGenerator::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbBlenderGenerator::Write(s, bw);
    s.WriteQuaternion(bw, m_worldFromModelRotation);
    bw.WriteSingle(m_blendSpeed);
    bw.WriteSingle(m_minSpeedToSwitch);
    bw.WriteSingle(m_minSwitchTimeNoError);
    bw.WriteSingle(m_minSwitchTimeFullError);
    bw.WriteInt32(m_startPlayingEventId);
    bw.WriteInt32(m_startMatchingEventId);
    bw.WriteInt16(m_rootBoneIndex);
    bw.WriteInt16(m_otherBoneIndex);
    bw.WriteInt16(m_anotherBoneIndex);
    bw.WriteInt16(m_pelvisIndex);
    bw.WriteSByte(m_mode);
    bw.Skip(3);
    bw.WriteInt32(m_currentMatch);
    bw.WriteInt32(m_bestMatch);
    bw.WriteSingle(m_timeSinceBetterMatch);
    bw.WriteSingle(m_error);
    bw.WriteBoolean(m_resetCurrentMatchLocalTime);
    bw.Skip(3);
    s.WriteVoidPointer(bw);
}

void BSDirectAtModifier::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbModifier::Write(s, bw);
    bw.WriteBoolean(m_directAtTarget);
    bw.Skip(1);
    bw.WriteInt16(m_sourceBoneIndex);
    bw.WriteInt16(m_startBoneIndex);
    bw.WriteInt16(m_endBoneIndex);
    bw.WriteSingle(m_limitHeadingDegrees);
    bw.WriteSingle(m_limitPitchDegrees);
    bw.WriteSingle(m_offsetHeadingDegrees);
    bw.WriteSingle(m_offsetPitchDegrees);
    bw.WriteSingle(m_onGain);
    bw.WriteSingle(m_offGain);
    bw.WriteVector4(m_targetLocation);
    bw.WriteUInt32(m_userInfo);
    bw.WriteBoolean(m_directAtCamera);
    bw.Skip(3);
    bw.WriteSingle(m_directAtCameraX);
    bw.WriteSingle(m_directAtCameraY);
    bw.WriteSingle(m_directAtCameraZ);
    bw.WriteBoolean(m_active);
    bw.Skip(3);
    bw.WriteSingle(m_currentHeadingOffset);
    bw.WriteSingle(m_currentPitchOffset);
    bw.WriteSingle(m_timeStep);
    bw.Skip(4);
    s.WriteVoidPointer(bw);
    bw.WriteBoolean(m_hasTarget);
    bw.Skip(15);
    bw.WriteVector4(m_directAtTargetLocation);
    s.WriteVoidArray(bw);
}

void BSEventEveryNEventsModifier::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbModifier::Write(s, bw);
    m_eventToCheckFor.Write(s, bw);
    m_eventToSend.Write(s, bw);
    bw.WriteSByte(m_numberOfEventsBeforeSend);
    bw.WriteSByte(m_minimumNumberOfEventsBeforeSend);
    bw.WriteBoolean(m_randomizeNumberOfEvents);
    bw.Skip(1);
    bw.WriteInt32(m_numberOfEventsSeen);
    bw.WriteSByte(m_calculatedNumberOfEventsBeforeSend);
    bw.Skip(7);
}

void BSEventOnDeactivateModifier::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbModifier::Write(s, bw);
    m_event.Write(s, bw);
}

void BSEventOnFalseToTrueModifier::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbModifier::Write(s, bw);
    bw.WriteBoolean(m_bEnableEvent1);
    bw.WriteBoolean(m_bVariableToTest1);
    bw.Skip(6);
    m_EventToSend1.Write(s, bw);
    bw.WriteBoolean(m_bEnableEvent2);
    bw.WriteBoolean(m_bVariableToTest2);
    bw.Skip(6);
    m_EventToSend2.Write(s, bw);
    bw.WriteBoolean(m_bEnableEvent3);
    bw.WriteBoolean(m_bVariableToTest3);
    bw.Skip(6);
    m_EventToSend3.Write(s, bw);
    bw.WriteBoolean(m_bSlot1ActivatedLastFrame);
    bw.WriteBoolean(m_bSlot2ActivatedLastFrame);
    bw.WriteBoolean(m_bSlot3ActivatedLastFrame);
    bw.Skip(5);
}

void BSInterpValueModifier::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbModifier::Write(s, bw);
    bw.WriteSingle(m_source);
    bw.WriteSingle(m_target);
    bw.WriteSingle(m_result);
    bw.WriteSingle(m_gain);
    bw.WriteSingle(m_timeStep);
    bw.Skip(4);
}

void BSIsActiveModifier::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbModifier::Write(s, bw);
    bw.WriteBoolean(m_bIsActive0);
    bw.WriteBoolean(m_bInvertActive0);
    bw.WriteBoolean(m_bIsActive1);
    bw.WriteBoolean(m_bInvertActive1);
    bw.WriteBoolean(m_bIsActive2);
    bw.WriteBoolean(m_bInvertActive2);
    bw.WriteBoolean(m_bIsActive3);
    bw.WriteBoolean(m_bInvertActive3);
    bw.WriteBoolean(m_bIsActive4);
    bw.WriteBoolean(m_bInvertActive4);
    bw.Skip(6);
}

void BSLookAtModifier::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbModifier::Write(s, bw);
    bw.WriteBoolean(m_lookAtTarget);
    bw.Skip(7);
    s.WriteClassArray(bw, m_bones);
    s.WriteClassArray(bw, m_eyeBones);
    bw.WriteSingle(m_limitAngleDegrees);
    bw.WriteSingle(m_limitAngleThresholdDegrees);
    bw.WriteBoolean(m_continueLookOutsideOfLimit);
    bw.Skip(3);
    bw.WriteSingle(m_onGain);
    bw.WriteSingle(m_offGain);
    bw.WriteBoolean(m_useBoneGains);
    bw.Skip(3);
    bw.WriteVector4(m_targetLocation);
    bw.WriteBoolean(m_targetOutsideLimits);
    bw.Skip(7);
    m_targetOutOfLimitEvent.Write(s, bw);
    bw.WriteBoolean(m_lookAtCamera);
    bw.Skip(3);
    bw.WriteSingle(m_lookAtCameraX);
    bw.WriteSingle(m_lookAtCameraY);
    bw.WriteSingle(m_lookAtCameraZ);
    bw.WriteSingle(m_timeStep);
    bw.WriteBoolean(m_ballBonesValid);
    bw.Skip(3);
    s.WriteVoidPointer(bw);
    bw.Skip(8);
}

void BSModifyOnceModifier::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbModifier::Write(s, bw);
    s.WriteClassPointer(bw, m_pOnActivateModifier);
    bw.Skip(8);
    s.WriteClassPointer(bw, m_pOnDeactivateModifier);
    bw.Skip(8);
}

void BSRagdollContactListenerModifier::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbModifier::Write(s, bw);
    bw.Skip(8);
    m_contactEvent.Write(s, bw);
    s.WriteClassPointer(bw, m_bones);
    bw.WriteBoolean(m_throwEvent);
    bw.Skip(7);
    s.WriteVoidArray(bw);
}

void BSSpeedSamplerModifier::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbModifier::Write(s, bw);
    bw.WriteInt32(m_state);
    bw.WriteSingle(m_direction);
    bw.WriteSingle(m_goalSpeed);
    bw.WriteSingle(m_speedOut);
}

void hkbDampingModifier::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbModifier::Write(s, bw);
    bw.WriteSingle(m_kP);
    bw.WriteSingle(m_kI);
    bw.WriteSingle(m_kD);
    bw.WriteBoolean(m_enableScalarDamping);
    bw.WriteBoolean(m_enableVectorDamping);
    bw.Skip(2);
    bw.WriteSingle(m_rawValue);
    bw.WriteSingle(m_dampedValue);
    bw.Skip(8);
    bw.WriteVector4(m_rawVector);
    bw.WriteVector4(m_dampedVector);
    bw.WriteVector4(m_vecErrorSum);
    bw.WriteVector4(m_vecPreviousError);
    bw.WriteSingle(m_errorSum);
    bw.WriteSingle(m_previousError);
    bw.Skip(8);
}

void hkbEvaluateExpressionModifier::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbModifier::Write(s, bw);
    s.WriteClassPointer(bw, m_expressions);
    s.WriteVoidPointer(bw);
    s.WriteVoidArray(bw);
}

void hkbModifierWrapper::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbModifier::Write(s, bw);
    s.WriteClassPointer(bw, m_modifier);
}

void hkbEventDrivenModifier::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbModifierWrapper::Write(s, bw);
    bw.WriteInt32(m_activateEventId);
    bw.WriteInt32(m_deactivateEventId);
    bw.WriteBoolean(m_activeByDefault);
    bw.WriteBoolean(m_isActive);
    bw.Skip(6);
}

void hkbEventsFromRangeModifier::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbModifier::Write(s, bw);
    bw.WriteSingle(m_inputValue);
    bw.WriteSingle(m_lowerBound);
    s.WriteClassPointer(bw, m_eventRanges);
    s.WriteVoidArray(bw);
}

void hkbFootIkControlsModifier::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbModifier::Write(s, bw);
    m_controlData.Write(s, bw);
    s.WriteClassArray(bw, m_legs);
    bw.WriteVector4(m_errorOutTranslation);
    s.WriteQuaternion(bw, m_alignWithGroundRotation);
}

void hkbGetUpModifier::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbModifier::Write(s, bw);
    bw.WriteVector4(m_groundNormal);
    bw.WriteSingle(m_duration);
    bw.WriteSingle(m_alignWithGroundDuration);
    bw.WriteInt16(m_rootBoneIndex);
    bw.WriteInt16(m_otherBoneIndex);
    bw.WriteInt16(m_anotherBoneIndex);
    bw.Skip(2);
    bw.WriteSingle(m_timeSinceBegin);
    bw.WriteSingle(m_timeStep);
    bw.WriteBoolean(m_initNextModify);
    bw.Skip(7);
}

void hkbKeyframeBonesModifier::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbModifier::Write(s, bw);
    s.WriteClassArray(bw, m_keyframeInfo);
    s.WriteClassPointer(bw, m_keyframedBonesList);
}

void hkbModifierList::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbModifier::Write(s, bw);
    s.WriteClassPointerArray(bw, m_modifiers);
}

void hkbPoweredRagdollControlsModifier::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbModifier::Write(s, bw);
    m_controlData.Write(s, bw);
    s.WriteClassPointer(bw, m_bones);
    m_worldFromModelModeData.Write(s, bw);
    s.WriteClassPointer(bw, m_boneWeights);
    bw.Skip(8);
}

void hkbRigidBodyRagdollControlsModifier::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbModifier::Write(s, bw);
    m_controlData.Write(s, bw);
    s.WriteClassPointer(bw, m_bones);
    bw.Skip(8);
}

void hkbRotateCharacterModifier::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbModifier::Write(s, bw);
    bw.WriteSingle(m_degreesPerSecond);
    bw.WriteSingle(m_speedMultiplier);
    bw.Skip(8);
    bw.WriteVector4(m_axisOfRotation);
    bw.WriteSingle(m_angle);
    bw.Skip(12);
}

void hkbTimerModifier::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbModifier::Write(s, bw);
    bw.WriteSingle(m_alarmTimeSeconds);
    bw.Skip(4);
    m_alarmEvent.Write(s, bw);
    bw.WriteSingle(m_secondsElapsed);
    bw.Skip(4);
}

void hkbExpressionCondition::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbCondition::Write(s, bw);
    s.WriteStringPointer(bw, m_expression);
    s.WriteVoidPointer(bw);
}

void hkbStringCondition::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbCondition::Write(s, bw);
    s.WriteStringPointer(bw, m_conditionString);
}

void hkbBoneIndexArray::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbBindable::Write(s, bw);
    s.WriteInt16Array(bw, m_boneIndices);
}

void hkbExpressionDataArray::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    s.WriteClassArray(bw, m_expressionsData);
}

void hkbEventRangeDataArray::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    s.WriteClassArray(bw, m_eventData);
}

void hkbMirroredSkeletonInfo::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    bw.WriteVector4(m_mirrorAxis);
    s.WriteInt16Array(bw, m_bonePairMap);
}

void hkbCharacterData::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    m_characterControllerInfo.Write(s, bw);
    bw.Skip(8);
    bw.WriteVector4(m_modelUpMS);
    bw.WriteVector4(m_modelForwardMS);
    bw.WriteVector4(m_modelRightMS);
    s.WriteClassArray(bw, m_characterPropertyInfos);
    s.WriteInt32Array(bw, m_numBonesPerLod);
    s.WriteClassPointer(bw, m_characterPropertyValues);
    s.WriteClassPointer(bw, m_footIkDriverInfo);
    s.WriteClassPointer(bw, m_handIkDriverInfo);
    s.WriteClassPointer(bw, m_stringData);
    s.WriteClassPointer(bw, m_mirroredSkeletonInfo);
    bw.WriteSingle(m_scale);
    bw.WriteInt16(m_numHands);
    bw.WriteInt16(m_numFloatSlots);
}

void hkbCharacterStringData::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    s.WriteStringPointerArray(bw, m_deformableSkinNames);
    s.WriteStringPointerArray(bw, m_rigidSkinNames);
    s.WriteStringPointerArray(bw, m_animationNames);
    s.WriteStringPointerArray(bw, m_animationFilenames);
    s.WriteStringPointerArray(bw, m_characterPropertyNames);
    s.WriteStringPointerArray(bw, m_retargetingSkeletonMapperFilenames);
    s.WriteStringPointerArray(bw, m_lodNames);
    s.WriteStringPointerArray(bw, m_mirroredSyncPointSubstringsA);
    s.WriteStringPointerArray(bw, m_mirroredSyncPointSubstringsB);
    s.WriteStringPointer(bw, m_name);
    s.WriteStringPointer(bw, m_rigName);
    s.WriteStringPointer(bw, m_ragdollName);
    s.WriteStringPointer(bw, m_behaviorFilename);
}

void hkbFootIkDriverInfo::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    s.WriteClassArray(bw, m_legs);
    bw.WriteSingle(m_raycastDistanceUp);
    bw.WriteSingle(m_raycastDistanceDown);
    bw.WriteSingle(m_originalGroundHeightMS);
    bw.WriteSingle(m_verticalOffset);
    bw.WriteUInt32(m_collisionFilterInfo);
    bw.WriteSingle(m_forwardAlignFraction);
    bw.WriteSingle(m_sidewaysAlignFraction);
    bw.WriteSingle(m_sidewaysSampleWidth);
    bw.WriteBoolean(m_lockFeetWhenPlanted);
    bw.WriteBoolean(m_useCharacterUpVector);
    bw.WriteBoolean(m_isQuadrupedNarrow);
    bw.Skip(5);
}

void hkbHandIkDriverInfo::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    s.WriteClassArray(bw, m_hands);
    bw.WriteSByte(m_fadeInOutCurve);
    bw.Skip(7);
}

void BSLookAtModifierBoneData::Write(PackFileSerializer&, BinaryWriterEx& bw) const {
    bw.WriteInt16(m_index);
    bw.Skip(14);
    bw.WriteVector4(m_fwdAxisLS);
    bw.WriteSingle(m_limitAngleDegrees);
    bw.WriteSingle(m_onGain);
    bw.WriteSingle(m_offGain);
    bw.WriteBoolean(m_enabled);
    bw.Skip(3);
    bw.WriteVector4(m_currentFwdAxisLS);
}

void hkbExpressionData::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    s.WriteStringPointer(bw, m_expression);
    bw.WriteInt32(m_assignmentVariableIndex);
    bw.WriteInt32(m_assignmentEventIndex);
    bw.WriteSByte(m_eventMode);
    bw.WriteBoolean(m_raisedEvent);
    bw.WriteBoolean(m_wasTrueInPreviousFrame);
    bw.Skip(5);
}

void hkbEventRangeData::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    bw.WriteSingle(m_upperBound);
    bw.Skip(4);
    m_event.Write(s, bw);
    bw.WriteSByte(m_eventMode);
    bw.Skip(7);
}

void hkbKeyframeBonesModifierKeyframeInfo::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    bw.WriteVector4(m_keyframedPosition);
    s.WriteQuaternion(bw, m_keyframedRotation);
    bw.WriteInt16(m_boneIndex);
    bw.WriteBoolean(m_isValid);
    bw.Skip(13);
}

void hkbFootIkControlsModifierLeg::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    bw.WriteVector4(m_groundPosition);
    m_ungroundedEvent.Write(s, bw);
    bw.WriteSingle(m_verticalError);
    bw.WriteBoolean(m_hitSomething);
    bw.WriteBoolean(m_isPlantedMS);
    bw.Skip(10);
}

void hkbFootIkDriverInfoLeg::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    s.WriteQuaternion(bw, m_prevAnkleRotLS);
    bw.WriteVector4(m_kneeAxisLS);
    bw.WriteVector4(m_footEndLS);
    bw.WriteSingle(m_footPlantedAnkleHeightMS);
    bw.WriteSingle(m_footRaisedAnkleHeightMS);
    bw.WriteSingle(m_maxAnkleHeightMS);
    bw.WriteSingle(m_minAnkleHeightMS);
    bw.WriteSingle(m_maxKneeAngleDegrees);
    bw.WriteSingle(m_minKneeAngleDegrees);
    bw.WriteSingle(m_maxAnkleAngleDegrees);
    bw.WriteInt16(m_hipIndex);
    bw.WriteInt16(m_kneeIndex);
    bw.WriteInt16(m_ankleIndex);
    bw.Skip(14);
}

void hkbFootIkControlData::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    m_gains.Write(s, bw);
}

void hkbFootIkGains::Write(PackFileSerializer&, BinaryWriterEx& bw) const {
    bw.WriteSingle(m_onOffGain);
    bw.WriteSingle(m_groundAscendingGain);
    bw.WriteSingle(m_groundDescendingGain);
    bw.WriteSingle(m_footPlantedGain);
    bw.WriteSingle(m_footRaisedGain);
    bw.WriteSingle(m_footUnlockGain);
    bw.WriteSingle(m_worldFromModelFeedbackGain);
    bw.WriteSingle(m_errorUpDownBias);
    bw.WriteSingle(m_alignWorldFromModelGain);
    bw.WriteSingle(m_hipOrientationGain);
    bw.WriteSingle(m_maxKneeAngleDifference);
    bw.WriteSingle(m_ankleOrientationGain);
}

void hkbHandIkDriverInfoHand::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    bw.WriteVector4(m_elbowAxisLS);
    bw.WriteVector4(m_backHandNormalLS);
    bw.WriteVector4(m_handOffsetLS);
    s.WriteQuaternion(bw, m_handOrienationOffsetLS);
    bw.WriteSingle(m_maxElbowAngleDegrees);
    bw.WriteSingle(m_minElbowAngleDegrees);
    bw.WriteInt16(m_shoulderIndex);
    bw.WriteInt16(m_shoulderSiblingIndex);
    bw.WriteInt16(m_elbowIndex);
    bw.WriteInt16(m_elbowSiblingIndex);
    bw.WriteInt16(m_wristIndex);
    bw.WriteBoolean(m_enforceEndPosition);
    bw.WriteBoolean(m_enforceEndRotation);
    bw.Skip(4);
    s.WriteStringPointer(bw, m_localFrameName);
}

void hkbWorldFromModelModeData::Write(PackFileSerializer&, BinaryWriterEx& bw) const {
    bw.WriteInt16(m_poseMatchingBone0);
    bw.WriteInt16(m_poseMatchingBone1);
    bw.WriteInt16(m_poseMatchingBone2);
    bw.WriteSByte(m_mode);
    bw.Skip(1);
}

void hkbPoweredRagdollControlData::Write(PackFileSerializer&, BinaryWriterEx& bw) const {
    bw.WriteSingle(m_maxForce);
    bw.WriteSingle(m_tau);
    bw.WriteSingle(m_damping);
    bw.WriteSingle(m_proportionalRecoveryVelocity);
    bw.WriteSingle(m_constantRecoveryVelocity);
    bw.Skip(12);
}

void hkbRigidBodyRagdollControlData::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    m_keyFrameHierarchyControlData.Write(s, bw);
    bw.WriteSingle(m_durationToBlend);
    bw.Skip(12);
}

void hkaKeyFrameHierarchyUtilityControlData::Write(PackFileSerializer&, BinaryWriterEx& bw) const {
    bw.WriteSingle(m_hierarchyGain);
    bw.WriteSingle(m_velocityDamping);
    bw.WriteSingle(m_accelerationGain);
    bw.WriteSingle(m_velocityGain);
    bw.WriteSingle(m_positionGain);
    bw.WriteSingle(m_positionMaxLinearVelocity);
    bw.WriteSingle(m_positionMaxAngularVelocity);
    bw.WriteSingle(m_snapGain);
    bw.WriteSingle(m_snapMaxLinearVelocity);
    bw.WriteSingle(m_snapMaxAngularVelocity);
    bw.WriteSingle(m_snapMaxLinearDistance);
    bw.WriteSingle(m_snapMaxAngularDistance);
}

void hkbCharacterDataCharacterControllerInfo::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    bw.WriteSingle(m_capsuleHeight);
    bw.WriteSingle(m_capsuleRadius);
    bw.WriteUInt32(m_collisionFilterInfo);
    bw.Skip(4);
    s.WriteClassPointer(bw, m_characterControllerCinfo);
}

void hkpCharacterControllerCinfo::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
}

// ── creature-graph classes (mirror the Read bodies in ClassRead_gen.cpp) ─────────
void hkbReferencePoseGenerator::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbGenerator::Write(s, bw);
    s.WriteClassPointer(bw, {});   // m_skeleton null
}

void BSGetTimeStepModifier::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbModifier::Write(s, bw);
    bw.WriteSingle(m_timeStep);
    bw.Skip(4);
}

void BSIStateManagerModifierBSiStateData::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    s.WriteClassPointer(bw, m_pStateMachine);
    bw.WriteInt32(m_StateID);
    bw.WriteInt32(m_iStateToSetAs);
}

void BSIStateManagerModifier::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbModifier::Write(s, bw);
    bw.WriteInt32(m_iStateVar);
    bw.Skip(4);
    s.WriteClassArray(bw, m_stateData);
}

// ── dragon-only classes (mirror the Read bodies in ClassRead_gen.cpp) ──
void BSDecomposeVectorModifier::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbModifier::Write(s, bw);
    bw.WriteVector4(m_vector);
    bw.WriteSingle(m_x);
    bw.WriteSingle(m_y);
    bw.WriteSingle(m_z);
    bw.WriteSingle(m_w);
}

void hkbTransformVectorModifier::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbModifier::Write(s, bw);
    s.WriteQuaternion(bw, m_rotation);
    bw.WriteVector4(m_translation);
    bw.WriteVector4(m_vectorIn);
    bw.WriteVector4(m_vectorOut);
    bw.WriteBoolean(m_rotateOnly);
    bw.WriteBoolean(m_inverse);
    bw.WriteBoolean(m_computeOnActivate);
    bw.WriteBoolean(m_computeOnModify);
    bw.Skip(12);
}

void BSLimbIKModifier::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbModifier::Write(s, bw);
    bw.WriteSingle(m_limitAngleDegrees);
    bw.Skip(4);
    bw.WriteInt16(m_startBoneIndex);
    bw.WriteInt16(m_endBoneIndex);
    bw.WriteSingle(m_gain);
    bw.WriteSingle(m_boneRadius);
    bw.WriteSingle(m_castOffset);
    bw.Skip(4);
    bw.Skip(4);
    s.WriteClassPointer(bw, {});   // pSkeletonMemory null
}

void BSTweenerModifier::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbModifier::Write(s, bw);
    bw.WriteBoolean(m_tweenPosition);
    bw.WriteBoolean(m_tweenRotation);
    bw.WriteBoolean(m_useTweenDuration);
    bw.Skip(1);
    bw.WriteSingle(m_tweenDuration);
    bw.Skip(8);
    bw.WriteVector4(m_targetPosition);
    s.WriteQuaternion(bw, m_targetRotation);
    bw.Skip(80);
}

void BSPassByTargetTriggerModifier::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbModifier::Write(s, bw);
    bw.WriteVector4(m_targetPosition);
    bw.WriteSingle(m_radius);
    bw.Skip(12);
    bw.WriteVector4(m_movementDirection);
    m_triggerEvent.Write(s, bw);
    bw.Skip(16);
}

void BSTimerModifier::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbModifier::Write(s, bw);
    bw.WriteSingle(m_alarmTimeSeconds);
    bw.Skip(4);
    m_alarmEvent.Write(s, bw);
    bw.WriteBoolean(m_resetAlarm);
    bw.Skip(7);
}

void hkbFootIkModifierLeg::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    bw.Skip(48);   // originalAnkleTransformMS
    s.WriteQuaternion(bw, m_prevAnkleRotLS);
    bw.WriteVector4(m_kneeAxisLS);
    bw.WriteVector4(m_footEndLS);
    m_ungroundedEvent.Write(s, bw);
    bw.WriteSingle(m_footPlantedAnkleHeightMS);
    bw.WriteSingle(m_footRaisedAnkleHeightMS);
    bw.WriteSingle(m_maxAnkleHeightMS);
    bw.WriteSingle(m_minAnkleHeightMS);
    bw.WriteSingle(m_maxKneeAngleDegrees);
    bw.WriteSingle(m_minKneeAngleDegrees);
    bw.WriteSingle(m_verticalError);
    bw.WriteSingle(m_maxAnkleAngleDegrees);
    bw.WriteInt16(m_hipIndex);
    bw.WriteInt16(m_kneeIndex);
    bw.WriteInt16(m_ankleIndex);
    bw.WriteBoolean(m_hitSomething);
    bw.WriteBoolean(m_isPlantedMS);
    bw.WriteBoolean(m_isOriginalAnkleTransformMSSet);
    bw.Skip(7);
}

void hkbFootIkModifier::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkbModifier::Write(s, bw);
    m_gains.Write(s, bw);
    s.WriteClassArray(bw, m_legs);
    bw.WriteSingle(m_raycastDistanceUp);
    bw.WriteSingle(m_raycastDistanceDown);
    bw.WriteSingle(m_originalGroundHeightMS);
    bw.WriteSingle(m_errorOut);
    bw.WriteVector4(m_errorOutTranslation);
    s.WriteQuaternion(bw, m_alignWithGroundRotation);
    bw.WriteSingle(m_verticalOffset);
    bw.WriteUInt32(m_collisionFilterInfo);
    bw.WriteSingle(m_forwardAlignFraction);
    bw.WriteSingle(m_sidewaysAlignFraction);
    bw.WriteSingle(m_sidewaysSampleWidth);
    bw.WriteBoolean(m_useTrackData);
    bw.WriteBoolean(m_lockFeetWhenPlanted);
    bw.WriteBoolean(m_useCharacterUpVector);
    bw.WriteSByte(m_alignMode);
    // internalLegData: SERIALIZE_IGNORED empty hkArray. It is present in the packfile
    // and its capacity&flags word must carry the 0x80000000 "don't deallocate" flag
    // (an all-zero header is rejected by strict readers). ptr(8)+size(4)+capflags(4).
    bw.WriteUSize(0);
    bw.WriteUInt32(0);
    bw.WriteUInt32(0x80000000u);
    bw.Skip(24);   // prevIsFootIkEnabled/isSetUp/isGroundPositionValid/timeStep + pad -> size 256
}

} // namespace havok
