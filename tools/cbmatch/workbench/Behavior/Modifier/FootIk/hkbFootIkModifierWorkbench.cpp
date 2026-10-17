/// @file
/// @brief Workbench: the functions of hkbFootIkModifier.cpp still being matched (tools/cbmatch/parked.yaml says why each is
///        stuck). Compiled only by tools/cbmatch/manifests/workbench.yaml; a body that matches graduates back into
///        schema/Havok/Behavior/Modifier/FootIk/hkbFootIkModifier.cpp.
#include <Behavior/Modifier/FootIk/hkbFootIkModifier.h>
#include <Animation/Ik/FootPlacement/hkaFootPlacementIkSolver.h>
#include <Animation/Rig/hkaPose.h>
#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Character/hkbCharacter.h>
#include <Behavior/Context/hkbContext.h>
#include <Behavior/Driver/FootIk/BSIFootIkRaycastInterfaceDB.h>
#include <Behavior/Event/hkbEventQueue.h>
#include <Behavior/Generator/hkbGeneratorOutputUtils.h>
#include <Common/Base/Math/Matrix/hkMatrixNm.h>
#include <Animation/Ik/FootPlacement/hkaRaycastInterface.h>

namespace CB { namespace schema
{
    namespace
    {
        /// @brief The rotation angle of `q` in radians, in [0, 2pi].
        /// @note hkQuaternion::getAngle with the real part read from memory (`m_vec(3)`), as this file's vanilla reads it
        ///       (the shared inline shuffles it out of a register).
        HK_FORCE_INLINE hkReal getAngle(const hkQuaternion& q)
        {
            hkReal angle = hkMath::fabs(q.m_vec(3));
            if (hkMath::fabs(angle) >= 1.0f) angle = (angle > 0.0f) ? 0.0f : HK_REAL_PI;
            else                             angle = ::acosf(angle);
            return angle * 2.0f;
        }
    }

    /// @note Every leg's three bones must lie inside the output's pose, else nothing happens (and the timer opened
    ///       first is left open, as in vanilla). The gains come from the foot IK controls track when m_useTrackData is
    ///       set and the track is on, else from m_gains; each but errorUpDownBias and maxKneeAngleDifference is turned
    ///       into this step's fraction. The vertical offset is applied to the root and to every solver for the step.
    void hkbFootIkModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        hkaRaycastInterface* raycastInterface = HK_NULL;
        if (BSIFootIkRaycastInterfaceDB::s_instance) raycastInterface = BSIFootIkRaycastInterfaceDB::s_instance->GetRaycastInterface(context.m_character);
        if (raycastInterface == HK_NULL) raycastInterface = context.m_character->getRaycastInterface();
        m_alignWithGroundRotation.setIdentity();

        const int numPoseLocal = inOut.getNumPoseLocal();
        if (m_legs.getSize() == 0 || raycastInterface == HK_NULL || numPoseLocal == 0) return;

        HK_TIMER_BEGIN("FootIkModifier");
        for (int i = 0; i < m_legs.getSize(); i++) {
            const int ankleIndex = m_legs[i].m_ankleIndex;
            const int kneeIndex  = m_legs[i].m_kneeIndex;
            const int hipIndex   = m_legs[i].m_hipIndex;
            if (ankleIndex < 0 || kneeIndex < 0 || hipIndex < 0 || ankleIndex >= numPoseLocal || kneeIndex >= numPoseLocal || hipIndex >= numPoseLocal) {
                return;
            }
        }

        hkReal         isFootIkEnabled;
        hkbFootIkGains gains;
        if (inOut.isTrackOn(hkbGeneratorOutput::TRACK_FOOT_IK_CONTROLS)) {
            if (m_useTrackData) {
                isFootIkEnabled = inOut.getTrackHeader(hkbGeneratorOutput::TRACK_FOOT_IK_CONTROLS).m_onFraction;
                gains = static_cast<const hkbFootIkControlData*>(inOut.getTrackData(hkbGeneratorOutput::TRACK_FOOT_IK_CONTROLS))->m_gains;
            } else {
                isFootIkEnabled = 1.0f;
                gains           = m_gains;
            }
        } else {
            isFootIkEnabled = m_useTrackData ? 0.0f : 1.0f;
            gains           = m_gains;
        }

        const hkReal alignWorldFromModelGain    = (gains.m_alignWorldFromModelGain == 1.0f) ? 1.0f : hkbFootIkGains::computeGainFraction(gains.m_alignWorldFromModelGain, m_timeStep);
        const hkReal ankleOrientationGain       = (gains.m_ankleOrientationGain == 1.0f) ? 1.0f : hkbFootIkGains::computeGainFraction(gains.m_ankleOrientationGain, m_timeStep);
        const hkReal footPlantedGain            = (gains.m_footPlantedGain == 1.0f) ? 1.0f : hkbFootIkGains::computeGainFraction(gains.m_footPlantedGain, m_timeStep);
        const hkReal footRaisedGain             = (gains.m_footRaisedGain == 1.0f) ? 1.0f : hkbFootIkGains::computeGainFraction(gains.m_footRaisedGain, m_timeStep);
        const hkReal footUnlockGain             = (gains.m_footUnlockGain == 1.0f) ? 1.0f : hkbFootIkGains::computeGainFraction(gains.m_footUnlockGain, m_timeStep);
        const hkReal groundAscendingGain        = (gains.m_groundAscendingGain == 1.0f) ? 1.0f : hkbFootIkGains::computeGainFraction(gains.m_groundAscendingGain, m_timeStep);
        const hkReal groundDescendingGain       = (gains.m_groundDescendingGain == 1.0f) ? 1.0f : hkbFootIkGains::computeGainFraction(gains.m_groundDescendingGain, m_timeStep);
        const hkReal hipOrientationGain         = (gains.m_hipOrientationGain == 1.0f) ? 1.0f : hkbFootIkGains::computeGainFraction(gains.m_hipOrientationGain, m_timeStep);
        const hkReal onOffGain                  = (gains.m_onOffGain == 1.0f) ? 1.0f : hkbFootIkGains::computeGainFraction(gains.m_onOffGain, m_timeStep);
        const hkReal worldFromModelFeedbackGain = (gains.m_worldFromModelFeedbackGain == 1.0f) ? 1.0f : hkbFootIkGains::computeGainFraction(gains.m_worldFromModelFeedbackGain, m_timeStep);

        const bool footPlacementOn = (isFootIkEnabled == 1.0f) || (isFootIkEnabled != 0.0f && isFootIkEnabled - m_prevIsFootIkEnabled > 0.0f);
        const bool wasSetUp        = m_isSetUp;
        m_prevIsFootIkEnabled      = isFootIkEnabled;

        const hkaSkeleton* skeleton = context.m_character->m_setup->m_animationSkeleton;
        if (!wasSetUp) setup(context, skeleton);

        hkaPose pose(skeleton);
        hkbGeneratorOutputUtils::getFilledPoseLocal(inOut, skeleton, pose.accessUnsyncedPoseLocalSpace().begin());

        hkReal    maxVerticalError = m_legs[0].m_verticalError;
        hkReal    minVerticalError = maxVerticalError;
        hkVector4 upWS;
        if (m_useCharacterUpVector) upWS.setRotatedDir(inOut.accessWorldFromModel().m_rotation, context.m_character->m_setup->m_data->m_modelUpMS);
        else                        upWS = context.getWorldUpWS();

        {
            hkVector4 upMS;
            upMS.setRotatedInverseDir(inOut.accessWorldFromModel().m_rotation, upWS);
            pose.accessBoneLocalSpace(0).m_translation.addMul4(upMS, hkSimdReal(m_verticalOffset));
        }
        for (int i = 0; i < m_legs.getSize(); i++) {
            m_internalLegData[i].m_footIkSolver->m_setup.m_maxAnkleHeightMS += m_verticalOffset;
            m_internalLegData[i].m_footIkSolver->m_setup.m_minAnkleHeightMS += m_verticalOffset;
            m_internalLegData[i].m_footIkSolver->m_setup.m_originalGroundHeightMS += m_verticalOffset;
        }

        for (int i = 1; i < m_internalLegData.getSize(); i++) {
            const hkReal verticalError = m_legs[i].m_verticalError;
            maxVerticalError           = hkMath::max2(verticalError, maxVerticalError);
            minVerticalError           = hkMath::min2(verticalError, minVerticalError);
        }
        m_errorOut = (1.0f - gains.m_errorUpDownBias) * maxVerticalError + gains.m_errorUpDownBias * minVerticalError;
        if (worldFromModelFeedbackGain > 0.0f) {
            m_errorOutTranslation.setMul4(hkSimdReal(m_errorOut * worldFromModelFeedbackGain), upWS);
            inOut.accessWorldFromModel().m_translation.add4(m_errorOutTranslation);
        }

        if (m_legs.getSize() > 2 && m_isGroundPositionValid) alignWorldFromModelUpWithGroundNormal(context, upWS, inOut, alignWorldFromModelGain);
        if (m_useCharacterUpVector) upWS.setRotatedDir(inOut.accessWorldFromModel().m_rotation, context.m_character->m_setup->m_data->m_modelUpMS);

        for (int i = 0; i < m_legs.getSize(); i++) {
            if (!m_legs[i].m_isOriginalAnkleTransformMSSet) m_legs[i].m_originalAnkleTransformMS = pose.getBoneModelSpace(m_legs[i].m_ankleIndex);
        }

        orientHips(context, inOut, pose, upWS, hipOrientationGain);

        hkaFootPlacementIkSolver::Input input;
        input.m_raycastInterface       = raycastInterface;
        input.m_worldFromModel         = inOut.accessWorldFromModel();
        input.m_collisionFilterInfo    = m_collisionFilterInfo;
        input.m_footRaisedGain         = footRaisedGain;
        input.m_footUnlockGain         = footUnlockGain;
        input.m_groundAscendingGain    = groundAscendingGain;
        input.m_onOffGain              = onOffGain;
        input.m_useCollisionFilterInfo = true;
        input.m_footPlacementOn        = footPlacementOn;
        input.m_groundDescendingGain   = groundDescendingGain;
        input.m_footPlantedGain        = footPlantedGain;
        for (int i = 0; i < m_legs.getSize(); i++) {
            Leg&             leg     = m_legs[i];
            InternalLegData& legData = m_internalLegData[i];
            input.m_originalAnkleTransformMS                    = leg.m_originalAnkleTransformMS;
            legData.m_footIkSolver->m_setup.m_worldUpDirectionWS = upWS;

            hkaFootPlacementIkSolver::Output output;
            legData.m_footIkSolver->doFootPlacement(input, output, pose);
            leg.m_verticalError = output.m_verticalError;
            leg.m_hitSomething  = output.m_hitSomething;
            if (output.m_hitSomething) leg.m_verticalError = output.m_verticalError + m_verticalOffset;
            leg.m_isPlantedMS = legData.m_footIkSolver->m_setup.m_modelUpDirectionMS.dot3(input.m_originalAnkleTransformMS.m_translation).getReal() <
                                leg.m_footPlantedAnkleHeightMS;
            leg.m_isOriginalAnkleTransformMSSet = false;

            if (alignWorldFromModelGain > 0.0f) {
                legData.m_groundPosition = pose.getBoneModelSpace(leg.m_ankleIndex).m_translation;
                legData.m_groundPosition.setTransformedPos(inOut.accessWorldFromModel(), legData.m_groundPosition);
                legData.m_groundPosition.subMul4(legData.m_groundPosition.dot3(upWS), upWS);
                const hkReal heightWS = inOut.accessWorldFromModel().m_translation.dot3(upWS).getReal() + leg.m_verticalError;
                legData.m_groundPosition.addMul4(upWS, hkSimdReal(heightWS));
                m_isGroundPositionValid = true;
            }

            if (wasSetUp) {
                const hkQsTransform& ankleLS = pose.getBoneLocalSpace(leg.m_ankleIndex);
                hkQuaternion         rotation;
                rotation.setSlerp(leg.m_prevAnkleRotLS, ankleLS.m_rotation, ankleOrientationGain);
                hkQsTransform blendedLS;
                blendedLS.m_translation = ankleLS.m_translation;
                blendedLS.m_rotation    = rotation;
                blendedLS.m_scale       = ankleLS.m_scale;
                pose.setBoneLocalSpace(leg.m_ankleIndex, blendedLS);
            }

            {
                const hkQsTransform& referenceLS = skeleton->m_referencePose[leg.m_ankleIndex];
                const hkQsTransform& ankleLS     = pose.getBoneLocalSpace(leg.m_ankleIndex);
                hkQuaternion         delta;
                delta.setInverseMul(referenceLS.m_rotation, ankleLS.m_rotation);
                const hkReal maxAngle = leg.m_maxAnkleAngleDegrees * HK_REAL_DEG_TO_RAD;
                if (maxAngle < delta.getAngle() && delta.hasValidAxis()) {
                    hkVector4 axis;
                    delta.getAxis(axis);
                    delta.setAxisAngle(axis, maxAngle);
                    hkQsTransform limitedLS;
                    limitedLS.m_translation = ankleLS.m_translation;
                    limitedLS.m_rotation.setMul(referenceLS.m_rotation, delta);
                    limitedLS.m_scale = ankleLS.m_scale;
                    pose.setBoneLocalSpace(leg.m_ankleIndex, limitedLS);
                }
            }

            leg.m_prevAnkleRotLS = pose.getBoneLocalSpace(leg.m_ankleIndex).m_rotation;
        }

        for (int i = 0; i < m_legs.getSize(); i++) {
            m_internalLegData[i].m_footIkSolver->m_setup.m_maxAnkleHeightMS -= m_verticalOffset;
            m_internalLegData[i].m_footIkSolver->m_setup.m_minAnkleHeightMS -= m_verticalOffset;
            m_internalLegData[i].m_footIkSolver->m_setup.m_originalGroundHeightMS -= m_verticalOffset;
        }

        hkString::memCpy(inOut.accessPoseLocal(), pose.getSyncedPoseLocalSpace().begin(), numPoseLocal * int(sizeof(hkQsTransform)));
        hkbGeneratorOutputUtils::markPoseDense(inOut);
        HK_TIMER_END();
    }

    void hkbFootIkModifier::orientHips(const hkbContext& context, hkbGeneratorOutput& inOut, hkaPose& pose, const hkVector4& upWS, hkReal gain)
    {
        if (gain > 0.0f) {
            hkVector4 currentUpWS;
            currentUpWS.setRotatedDir(inOut.accessWorldFromModel().m_rotation, context.m_character->m_setup->m_data->m_modelUpMS);
            currentUpWS.normalize3();
            if (!currentUpWS.equals3(upWS, 1e-3f)) {
                hkQuaternion rotation;
                rotation.setShortestRotation(currentUpWS, upWS);
                for (int i = 0; i < m_legs.getSize(); i++) {
                    const int            hipIndex = m_legs[i].m_hipIndex;
                    const hkQsTransform& hipMS    = pose.getBoneModelSpace(hipIndex);
                    hkQuaternion         hipWS;
                    hipWS.setMul(inOut.accessWorldFromModel().m_rotation, hipMS.m_rotation);
                    if (rotation.hasValidAxis()) {
                        hkVector4 axisWS;
                        rotation.getAxis(axisWS);
                        hkVector4 axisLS;
                        axisLS.setRotatedInverseDir(hipWS, axisWS);
                        hkQuaternion delta;
                        delta.setAxisAngle(axisLS, getAngle(rotation) * gain);
                        hkQsTransform& hipLS = pose.accessBoneLocalSpace(hipIndex);
                        hipLS.m_rotation.setMul(hipLS.m_rotation, delta);
                    }
                }
            }
        }
    }

    /// @note ALIGN_MODE_FORWARD fits a line along the forward axis and crosses it with a sideways direction (sampled by
    ///       two ray casts m_sidewaysSampleWidth to either side when that is non-zero); each is turned from the
    ///       horizontal toward the ground by its align fraction. The other mode fits a plane. Vanilla reuses locals
    ///       for unrelated vectors (the axes are written into the locals they replace); the spelling follows it.
    /// @warning As in vanilla, a ray that misses leaves its hit flag unwritten (the buffer is not cleared), and a
    ///          right axis parallel to the world up returns without writing `groundNormalOut`.
    void hkbFootIkModifier::computeGroundNormal(const hkbContext& context, const hkQsTransform& worldFromModel, const hkVector4& upWS, hkVector4& groundNormalOut)
    {
        hkVector4 rightWS;
        rightWS.setRotatedDir(worldFromModel.m_rotation, context.m_character->m_setup->m_data->m_modelRightMS);
        rightWS.normalize3();

        const int                numLegs = m_internalLegData.getSize();
        hkLocalBuffer<hkVector4> groundPoints(numLegs);
        for (int i = 0; i < numLegs; i++) {
            groundPoints[i] = m_internalLegData[i].m_groundPosition;
            if (m_alignMode == ALIGN_MODE_FORWARD) groundPoints[i].subMul4(rightWS.dot3(groundPoints[i]), rightWS);
        }

        if (m_alignMode == ALIGN_MODE_FORWARD) {
            hkVector4 forwardAxisWS;
            forwardAxisWS.setCross(upWS, rightWS);
            forwardAxisWS.normalize3();
            hkVector4 forwardWS;
            if (!fitLine(&groundPoints[0], numLegs, forwardAxisWS, upWS, forwardWS)) {
                forwardWS.setRotatedDir(worldFromModel.m_rotation, context.m_character->m_setup->m_data->m_modelForwardMS);
            }

            hkVector4 horizontalForwardWS;
            horizontalForwardWS.setCross(context.getWorldUpWS(), rightWS);
            if (horizontalForwardWS.lengthSquared3().getReal() < HK_REAL_EPSILON) return;
            horizontalForwardWS.normalize3();

            hkQuaternion rotation;
            if (m_forwardAlignFraction < 1.0f) {
                rotation.setShortestRotation(horizontalForwardWS, forwardWS);
                const hkReal angle = getAngle(rotation) * m_forwardAlignFraction;
                if (getAngle(rotation) > 0.0f && angle > 0.0f) {
                    hkVector4 axis;
                    rotation.getAxis(axis);
                    forwardAxisWS = axis;
                    rotation.setAxisAngle(forwardAxisWS, angle);
                    forwardWS.setRotatedDir(rotation, horizontalForwardWS);
                    forwardWS.normalize3();
                } else {
                    forwardWS = horizontalForwardWS;
                }
            }

            hkVector4 sideWS;
            sideWS.setCross(horizontalForwardWS, context.getWorldUpWS());
            hkVector4 sampledSideWS = sideWS;
            if (m_sidewaysSampleWidth != 0.0f) {
                hkVector4 upOffsetWS;
                upOffsetWS.setMul4(hkSimdReal(m_raycastDistanceUp), context.getWorldUpWS());
                hkVector4 downOffsetWS;
                downOffsetWS.setMul4(hkSimdReal(-m_raycastDistanceDown), context.getWorldUpWS());
                hkVector4 sampleOffsetWS;
                sampleOffsetWS.setMul4(hkSimdReal(m_sidewaysSampleWidth), sideWS);

                hkLocalBuffer<hkBool>    hits(2);
                hkLocalBuffer<hkVector4> hitPoints(2);
                for (int i = 0; i < 2; i++) {
                    hkVector4 sampleWS;
                    sampleWS.setAdd4(sampleOffsetWS, worldFromModel.m_translation);
                    hkVector4 fromWS;
                    fromWS.setAdd4(sampleWS, upOffsetWS);
                    hkVector4 toWS;
                    toWS.setAdd4(sampleWS, downOffsetWS);
                    hkReal    hitFraction = HK_REAL_MAX;
                    hkVector4 hitNormalWS = hkVector4::getZero();
                    if (context.m_character->getRaycastInterface()->castRay(fromWS, toWS, m_collisionFilterInfo, hitFraction, hitNormalWS)) {
                        hitPoints[i] = fromWS;
                        hkVector4 rayWS;
                        rayWS.setSub4(toWS, fromWS);
                        hitPoints[i].addMul4(rayWS, hkSimdReal(hitFraction));
                        hits[i] = true;
                    }
                    sampleOffsetWS.mul4(hkSimdReal(-1.0f));
                }

                if (hits[0] || hits[1]) {
                    const hkVector4& p0 = hits[0] ? hitPoints[0] : worldFromModel.m_translation;
                    const hkVector4& p1 = hits[1] ? hitPoints[1] : worldFromModel.m_translation;
                    hkVector4        hitSideWS;
                    hitSideWS.setSub4(p0, p1);
                    hitSideWS.normalize3();
                    sampledSideWS = hitSideWS;
                    if (m_sidewaysAlignFraction < 1.0f) {
                        rotation.setShortestRotation(sideWS, hitSideWS);
                        const hkReal angle = getAngle(rotation) * m_sidewaysAlignFraction;
                        if (getAngle(rotation) > 0.0f && angle > 0.0f) {
                            hkVector4 axis;
                            rotation.getAxis(axis);
                            hitSideWS = axis;
                            rotation.setAxisAngle(hitSideWS, angle);
                            sampledSideWS.setRotatedDir(rotation, sideWS);
                            sampledSideWS.normalize3();
                        } else {
                            sampledSideWS = sideWS;
                        }
                    }
                } else {
                    sampledSideWS = rightWS;
                }
            }

            const hkReal cosAngle = sampledSideWS.dot3(forwardWS).getReal();
            if (hkMath::fabs(hkMath::fabs(cosAngle) - 1.0f) < 1e-3f) {
                groundNormalOut.setRotatedDir(worldFromModel.m_rotation, context.m_character->m_setup->m_data->m_modelUpMS);
            } else {
                groundNormalOut.setCross(sampledSideWS, forwardWS);
            }
            groundNormalOut.normalize3();
        } else {
            if (!fitPlane(&groundPoints[0], numLegs, upWS, groundNormalOut)) {
                groundNormalOut.setRotatedDir(worldFromModel.m_rotation, context.m_character->m_setup->m_data->m_modelUpMS);
            }
        }
    }

    /// @note A 3x3 least-squares solve of height = a·u + b·w + c over the points centred on their mean, u and w the two
    ///       axes across `upWS`. The plane's two slopes are crossed into its normal.
    bool hkbFootIkModifier::fitPlane(const hkVector4* points, int numPoints, const hkVector4& upWS, hkVector4& normalOut) const
    {
        hkVector4 axis = hkVector4::getZero();
        int       index = 0;
        if (upWS(0) > upWS(1)) index = 1;
        if (upWS(0) > upWS(2)) index = 2;
        axis(index) = 1.0f;

        hkVector4 tangent;
        tangent.setCross(axis, upWS);
        tangent.normalize3();
        hkVector4 bitangent;
        bitangent.setCross(upWS, tangent);
        bitangent.normalize3();

        hkReal sumUV = 0.0f;
        hkReal sumV  = 0.0f;
        hkReal sumWV = 0.0f;
        hkReal a;
        hkReal b;
        hkReal c;
        {
            hkMatrixNm m;
            m.setSize(3, 3);
            m(2, 2) = hkReal(numPoints);

            hkVector4 average;
            computeAverage(points, numPoints, average);
            for (int i = 0; i < numPoints; i++) {
                hkVector4 d;
                d.setSub4(points[i], average);
                const hkReal u = d.dot3(bitangent).getReal();
                const hkReal v = d.dot3(upWS).getReal();
                const hkReal w = d.dot3(tangent).getReal();
                sumV += v;
                sumUV += v * u;
                sumWV += w * v;
                m(0, 0) += u * u;
                m(0, 1) += w * u;
                m(0, 2) += u;
                m(1, 0) += w * u;
                m(1, 1) += w * w;
                m(1, 2) += w;
                m(2, 0) += u;
                m(2, 1) += w;
            }
            if (m.invert() == HK_FAILURE) return false;

            a = m(0, 0) * sumUV + m(0, 1) * sumWV + m(0, 2) * sumV;
            b = m(1, 0) * sumUV + m(1, 1) * sumWV + m(1, 2) * sumV;
            c = m(2, 0) * sumUV + m(2, 1) * sumWV + m(2, 2) * sumV;
        }

        hkVector4 origin;
        origin.setMul4(hkSimdReal(c), upWS);
        hkVector4 alongU;
        alongU.setMul4(hkSimdReal(c + a), upWS);
        alongU.add4(bitangent);
        alongU.sub4(origin);
        hkVector4 alongW;
        alongW.setMul4(hkSimdReal(c + b), upWS);
        alongW.add4(tangent);
        alongW.sub4(origin);
        alongU.normalize3();
        alongW.normalize3();

        hkVector4 normal;
        normal.setCross(alongW, alongU);
        normal.normalize3();
        normalOut = normal;
        if (upWS.dot3(normal).getReal() < 0.0f) normalOut.setMul4(hkSimdReal(-1.0f), normal);
        return true;
    }

} }
