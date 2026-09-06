#pragma once
// Umbrella include for the 52 transpiler-generated (Tier-A expansion) Havok
// classes that are NEW relative to the hand-ported set in havok/classes/*.h.
// Each gen header is self-sufficient (it includes the hand-port / sibling
// headers that define its base + by-value members), so include order here is
// not load-bearing; grouped by domain for readability.
//
// The 47 reproductions emitted by the transpiler are intentionally OMITTED —
// they are already provided by the hand-ported headers (havok/classes/Classes.h)
// and re-declaring them would be an ODR clash.

// ── element structs / control-data leaves ──
#include "havok/classes/gen/hkbFootIkGains.h"
#include "havok/classes/gen/hkbFootIkControlData.h"
#include "havok/classes/gen/hkbFootIkControlsModifierLeg.h"
#include "havok/classes/gen/hkbFootIkDriverInfoLeg.h"
#include "havok/classes/gen/hkbHandIkDriverInfoHand.h"
#include "havok/classes/gen/hkbWorldFromModelModeData.h"
#include "havok/classes/gen/hkbPoweredRagdollControlData.h"
#include "havok/classes/gen/hkaKeyFrameHierarchyUtilityControlData.h"
#include "havok/classes/gen/hkbRigidBodyRagdollControlData.h"
#include "havok/classes/gen/hkbKeyframeBonesModifierKeyframeInfo.h"
#include "havok/classes/gen/hkbExpressionData.h"
#include "havok/classes/gen/hkbEventRangeData.h"
#include "havok/classes/gen/BSLookAtModifierBoneData.h"
#include "havok/classes/gen/hkpCharacterControllerCinfo.h"
#include "havok/classes/gen/hkbCharacterDataCharacterControllerInfo.h"

// ── arrays / data ──
#include "havok/classes/gen/hkbBoneIndexArray.h"
#include "havok/classes/gen/hkbExpressionDataArray.h"
#include "havok/classes/gen/hkbEventRangeDataArray.h"
#include "havok/classes/gen/hkbMirroredSkeletonInfo.h"

// ── conditions ──
#include "havok/classes/gen/hkbExpressionCondition.h"
#include "havok/classes/gen/hkbStringCondition.h"

// ── character / IK / driver info ──
#include "havok/classes/gen/hkbFootIkDriverInfo.h"
#include "havok/classes/gen/hkbHandIkDriverInfo.h"
#include "havok/classes/gen/hkbCharacterStringData.h"
#include "havok/classes/gen/hkbCharacterData.h"

// ── modifiers (hkbModifier-derived) ──
#include "havok/classes/gen/hkbModifierWrapper.h"
#include "havok/classes/gen/hkbEventDrivenModifier.h"
#include "havok/classes/gen/hkbModifierList.h"
#include "havok/classes/gen/hkbModifierGenerator.h"
#include "havok/classes/gen/hkbEvaluateExpressionModifier.h"
#include "havok/classes/gen/hkbDampingModifier.h"
#include "havok/classes/gen/hkbRotateCharacterModifier.h"
#include "havok/classes/gen/hkbTimerModifier.h"
#include "havok/classes/gen/hkbKeyframeBonesModifier.h"
#include "havok/classes/gen/hkbFootIkControlsModifier.h"
#include "havok/classes/gen/hkbGetUpModifier.h"
#include "havok/classes/gen/hkbRigidBodyRagdollControlsModifier.h"
#include "havok/classes/gen/hkbPoweredRagdollControlsModifier.h"
#include "havok/classes/gen/hkbEventsFromRangeModifier.h"
#include "havok/classes/gen/BSIsActiveModifier.h"
#include "havok/classes/gen/BSDirectAtModifier.h"
#include "havok/classes/gen/BSEventOnFalseToTrueModifier.h"
#include "havok/classes/gen/BSEventOnDeactivateModifier.h"
#include "havok/classes/gen/BSEventEveryNEventsModifier.h"
#include "havok/classes/gen/BSInterpValueModifier.h"
#include "havok/classes/gen/hkbReferencePoseGenerator.h"
#include "havok/classes/gen/BSGetTimeStepModifier.h"
#include "havok/classes/gen/BSIStateManagerModifierBSiStateData.h"
#include "havok/classes/gen/BSIStateManagerModifier.h"
#include "havok/classes/gen/BSDecomposeVectorModifier.h"
#include "havok/classes/gen/hkbTransformVectorModifier.h"
#include "havok/classes/gen/BSLimbIKModifier.h"
#include "havok/classes/gen/BSTweenerModifier.h"
#include "havok/classes/gen/BSPassByTargetTriggerModifier.h"
#include "havok/classes/gen/BSTimerModifier.h"
#include "havok/classes/gen/hkbFootIkModifierLeg.h"
#include "havok/classes/gen/hkbFootIkModifier.h"
#include "havok/classes/gen/BSRagdollContactListenerModifier.h"
#include "havok/classes/gen/BSModifyOnceModifier.h"
#include "havok/classes/gen/BSLookAtModifier.h"
#include "havok/classes/gen/BSSpeedSamplerModifier.h"

// ── generators ──
#include "havok/classes/gen/BSSynchronizedClipGenerator.h"
#include "havok/classes/gen/BSOffsetAnimationGenerator.h"
#include "havok/classes/gen/hkbPoseMatchingGenerator.h"
