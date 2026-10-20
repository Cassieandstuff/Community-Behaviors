/// @file
/// @brief hkbHandIkModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/HandIk/hkbHandIkModifier.h>

#include <Animation/Rig/hkaPose.h>
#include <Behavior/Character/hkbCharacter.h>
#include <Behavior/Context/hkbContext.h>
#include <Behavior/Generator/hkbGeneratorOutputUtils.h>
#include <Behavior/Modifier/HandIkControls/hkbHandIkControlData.h>
#include <Behavior/Utils/hkbBlendCurveUtils.h>
#include <Behavior/Utils/hkbHandleUtils.h>
#include <Common/Base/Memory/Util/Stats/hkStatisticsCollector.h>
#include <Common/Base/Monitor/hkMonitorStream.h>

namespace CB { namespace havok
{
    namespace
    {
        /// @brief Moves `current` toward `desired`: by timestep/fadeInDuration when rising, timestep/fadeOutDuration when
        ///        falling, straight to it for a zero duration.
        HK_FORCE_INLINE void fadeFraction(hkReal& current, hkReal desired, hkReal timestep, hkReal fadeInDuration, hkReal fadeOutDuration)
        {
            const hkReal delta = desired - current;
            if (delta > 0.0f) {
                if (fadeInDuration == 0.0f) current = desired;
                else                        current = hkMath::min2(current + timestep / fadeInDuration, desired);
            } else if (delta < 0.0f) {
                if (fadeOutDuration == 0.0f) current = desired;
                else                         current = hkMath::max2(current - timestep / fadeOutDuration, desired);
            }
        }
    }

    hkbHandIkModifier::hkbHandIkModifier()
    :   m_fadeInOutCurve(hkbBlendCurveUtils::BLEND_CURVE_SMOOTH)
    {
    }

    hkbHandIkModifier::hkbHandIkModifier(const hkbHandIkModifier& mod)
    :   hkbModifier(mod)
    {
        m_hands = mod.m_hands;
    }

    void hkbHandIkModifier::calcContentStatistics(hkStatisticsCollector* collector, const hkClass* cls) const
    {
        hkStatisticsCollectorUtil::addDeclaredMembers(this, &hkbHandIkModifierClass, collector);
        collector->addArray("InternalHandsData", m_internalHandData, hkStatisticsCollector::MEMORY_RUNTIME);
        for (int i = 0; i < m_internalHandData.getSize(); i++) collector->addNormalChunk("InternalHandData", &m_internalHandData[i], sizeof(InternalHandData));
        hkbModifier::calcContentStatistics(collector, &hkbModifierClass);
    }

    /// @note The whole state is zeroed (handle vtables included) before each handle is cleared and looked up: vanilla's.
    void hkbHandIkModifier::activate(const hkbContext& context)
    {
        m_internalHandData.setSize(m_hands.getSize());
        hkString::memSet4(m_internalHandData.begin(), 0, int(m_hands.getSize() * sizeof(InternalHandData) / sizeof(hkUint32)));
        const int numHands = m_hands.getSize();
        for (int i = 0; i < numHands; i++) {
            m_internalHandData[i].m_handle.clear();
            hkbHandleUtils::getHandleFromLocalFrameName(context, m_hands[i].m_localFrameName.cString(), m_internalHandData[i].m_handle);
        }
    }

    void hkbHandIkModifier::update(const hkbContext& context, hkReal timestep)
    {
        const int numHands = m_internalHandData.getSize();
        for (int i = 0; i < numHands; i++) {
            InternalHandData& data = m_internalHandData[i];
            fadeFraction(data.m_transformOnFraction, data.m_desiredTransformOnFraction, timestep, data.m_fadeInDuration, data.m_fadeOutDuration);
            fadeFraction(data.m_normalOnFraction, data.m_desiredNormalOnFraction, timestep, data.m_fadeInDuration, data.m_fadeOutDuration);
        }
    }

    bool hkbHandIkModifier::isValid(hkStringPtr& errorString) const
    {
        if (m_hands.getSize() > 4) {
            errorString = "You have exceeded the maximum number of hands.";
            return false;
        }

        const hkbVariableBindingSet* bindings = m_variableBindingSet;
        for (int i = 0; i < m_hands.getSize(); i++) {
            char shoulderPath[32];
            hkString::sprintf(shoulderPath, "hands:%d/shoulderIndex", i);
            char elbowPath[32];
            hkString::sprintf(elbowPath, "hands:%d/elbowIndex", i);
            char wristPath[32];
            hkString::sprintf(wristPath, "hands:%d/wristIndex", i);

            const bool shoulderOk = m_hands[i].m_shoulderIndex >= 0 || (bindings && bindings->findBindingByMemberPath(shoulderPath));
            const bool elbowOk    = m_hands[i].m_elbowIndex >= 0 || (bindings && bindings->findBindingByMemberPath(elbowPath));
            const bool wristOk    = m_hands[i].m_wristIndex >= 0 || (bindings && bindings->findBindingByMemberPath(wristPath));
            if (!shoulderOk || !elbowOk || !wristOk) {
                errorString = "You must specify all three bone indices.";
                return false;
            }

            const Hand& hand = m_hands[i];
            if (hand.m_shoulderIndex >= 0 && hand.m_elbowIndex >= 0 && hand.m_wristIndex >= 0 &&
                (hand.m_shoulderIndex == hand.m_elbowIndex || hand.m_shoulderIndex == hand.m_wristIndex || hand.m_elbowIndex == hand.m_wristIndex)) {
                errorString = "You must specify unique bone indices for the three bones.";
                return false;
            }
            if (!m_hands[i].m_elbowAxisLS.isNormalized3()) {
                errorString = "Elbow axis must be normalized.";
                return false;
            }
            if (!m_hands[i].m_backHandNormalLS.isNormalized3()) {
                errorString = "Back hand normal must be normalized.";
                return false;
            }
        }
        return true;
    }

    hkbNode* hkbHandIkModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbHandIkModifier(*this);
    }

    void hkbHandIkModifier::alignHandNormal(int handIndex, hkReal fraction, const hkVector4& targetNormalMS, const hkQsTransform& worldFromModel,
                                            hkaPose& pose)
    {
        const int          wristIndex    = m_hands[handIndex].m_wristIndex;
        const hkQuaternion wristRotation = pose.getBoneModelSpace(wristIndex).m_rotation;

        hkVector4 backHandNormalMS;
        backHandNormalMS.setRotatedDir(wristRotation, m_hands[handIndex].m_backHandNormalLS);
        const hkReal cosAngle = backHandNormalMS.dot3(targetNormalMS).getReal();

        hkQuaternion rotation;
        rotation.setIdentity();
        if (cosAngle - 1.0f < -HK_REAL_EPSILON) {
            hkReal angle;
            if (hkMath::fabs(cosAngle) >= 1.0f) angle = (cosAngle > 0.0f) ? 0.0f : HK_REAL_PI;
            else                                angle = ::acosf(cosAngle);

            hkVector4 axis;
            axis.setCross(backHandNormalMS, targetNormalMS);
            if (axis.length3().getReal() > 1e-4f) {
                axis.normalize3();
                rotation.setAxisAngle(axis, angle * fraction);
            }
        }

        hkQuaternion newRotation;
        newRotation.setMul(rotation, wristRotation);
        newRotation.normalize();
        pose.accessBoneModelSpace(wristIndex, hkaPose::PROPAGATE).m_rotation = newRotation;
    }
} }
