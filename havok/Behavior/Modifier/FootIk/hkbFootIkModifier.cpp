/// @file
/// @brief hkbFootIkModifier, reimplemented against vanilla (AE 1.6.1170).
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

namespace CB { namespace havok
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

    hkbFootIkModifier::hkbFootIkModifier()
    :   m_raycastDistanceUp(0.5f),
        m_raycastDistanceDown(0.8f),
        m_originalGroundHeightMS(0.0f),
        m_errorOut(0.0f),
        m_alignWithGroundRotation(hkQuaternion::getIdentity()),
        m_verticalOffset(0.0f),
        m_collisionFilterInfo(3),
        m_forwardAlignFraction(1.0f),
        m_sidewaysAlignFraction(0.0f),
        m_sidewaysSampleWidth(0.2f),
        m_useTrackData(false),
        m_lockFeetWhenPlanted(false),
        m_useCharacterUpVector(false),
        m_alignMode(ALIGN_MODE_FORWARD_RIGHT),
        m_prevIsFootIkEnabled(0.0f),
        m_isSetUp(false),
        m_isGroundPositionValid(false),
        m_timeStep(0.0f)
    {
    }

    /// @note The 0x48-byte memCpy copies m_raycastDistanceUp through m_alignMode.
    hkbFootIkModifier::hkbFootIkModifier(const hkbFootIkModifier& mod)
    :   hkbModifier(mod),
        m_alignWithGroundRotation(hkQuaternion::getIdentity()),
        m_prevIsFootIkEnabled(0.0f),
        m_isSetUp(false),
        m_timeStep(0.0f)
    {
        m_gains = mod.m_gains;
        m_legs  = mod.m_legs;
        hkString::memCpy(&m_raycastDistanceUp, &mod.m_raycastDistanceUp, 0x48);
    }

    hkbFootIkModifier::~hkbFootIkModifier()
    {
        for (int i = 0; i < m_internalLegData.getSize(); i++) {
            delete m_internalLegData[i].m_footIkSolver;
        }
    }

    void hkbFootIkModifier::activate(const hkbContext& context)
    {
        const int numLegs = m_legs.getSize();
        m_internalLegData.setSize(numLegs);
        for (int i = 0; i < numLegs; i++) {
            m_internalLegData[i].m_footIkSolver = HK_NULL;
            Leg& leg = m_legs[i];
            leg.m_hitSomething  = false;
            leg.m_isPlantedMS   = false;
            leg.m_verticalError = 0.0f;
        }
        m_isGroundPositionValid = false;
        m_errorOut              = 0.0f;
        m_verticalOffset        = 0.0f;
        m_timeStep              = 0.0f;
    }

    void hkbFootIkModifier::update(const hkbContext& context, hkReal timestep)
    {
        HK_TIMER_BEGIN("hkbFootIkModifier::update");
        m_timeStep = timestep;

        hkbEventQueue* queue   = context.m_eventQueue ? context.m_eventQueue : context.m_character->m_eventQueue;
        const int      numLegs = m_legs.getSize();
        for (int i = 0; i < numLegs; i++) {
            const Leg& leg = m_legs[i];
            if (leg.m_isPlantedMS && !leg.m_hitSomething && leg.m_ungroundedEvent.m_id != -1) {
                hkbNode* sender = context.m_character->m_behaviorGraph->getNodeTemplate(this);
                hkbEvent event(leg.m_ungroundedEvent.m_id, leg.m_ungroundedEvent.m_payload, sender);
                event.m_id = queue->toExternal(event.m_id);
                queue->enqueue(event);
            }
        }
        HK_TIMER_END();
    }

    void hkbFootIkModifier::deactivate(const hkbContext& context)
    {
        for (int i = 0; i < m_internalLegData.getSize(); i++) {
            delete m_internalLegData[i].m_footIkSolver;
            m_internalLegData[i].m_footIkSolver = HK_NULL;
            m_internalLegData[i].m_groundPosition.setZero4();
        }
        m_isSetUp             = false;
        m_prevIsFootIkEnabled = 0.0f;
    }

    bool hkbFootIkModifier::isValid(hkStringPtr& errorString) const
    {
        const hkbVariableBindingSet* bindings = m_variableBindingSet;
        for (int i = 0; i < m_legs.getSize(); i++) {
            const Leg& leg = m_legs[i];
            char ankleIndexPath[32];
            char kneeIndexPath[32];
            char hipIndexPath[32];
            hkString::sprintf(ankleIndexPath, "legs:%d/ankleIndex", i);
            hkString::sprintf(kneeIndexPath, "legs:%d/kneeIndex", i);
            hkString::sprintf(hipIndexPath, "legs:%d/hipIndex", i);

            const bool ankleValid = leg.m_ankleIndex >= 0 || (bindings && bindings->findBindingByMemberPath(ankleIndexPath));
            const bool kneeValid  = leg.m_kneeIndex >= 0 || (bindings && bindings->findBindingByMemberPath(kneeIndexPath));
            const bool hipValid   = leg.m_hipIndex >= 0 || (bindings && bindings->findBindingByMemberPath(hipIndexPath));
            if (!ankleValid || !kneeValid || !hipValid) {
                errorString = "You must specify the three bones for each leg.";
                return false;
            }
            if (leg.m_ankleIndex >= 0 && leg.m_kneeIndex >= 0 && leg.m_hipIndex >= 0 &&
                (leg.m_ankleIndex == leg.m_kneeIndex || leg.m_kneeIndex == leg.m_hipIndex || leg.m_hipIndex == leg.m_ankleIndex)) {
                errorString = "You must specify unique bone indices for the three bones.";
                return false;
            }
            if (!leg.m_kneeAxisLS.isNormalized3(1e-4f)) {
                errorString = "The kneeAxisLS must be normalized.";
                return false;
            }
            if (leg.m_maxKneeAngleDegrees <= leg.m_minKneeAngleDegrees) {
                errorString = "The maxKneeAngleDegrees must be greater than the minKneeAngleDegrees.";
                return false;
            }
            if (leg.m_footPlantedAnkleHeightMS < leg.m_minAnkleHeightMS || leg.m_footPlantedAnkleHeightMS > leg.m_maxAnkleHeightMS) {
                errorString = "The footPlantedAnkleHeightMS must be between minAnkleHeightMS and maxAnkleHeightMS.";
                return false;
            }
            if (leg.m_footRaisedAnkleHeightMS < leg.m_minAnkleHeightMS || leg.m_footRaisedAnkleHeightMS > leg.m_maxAnkleHeightMS) {
                errorString = "The footRaisedAnkleHeightMS must be between minAnkleHeightMS and maxAnkleHeightMS.";
                return false;
            }
            if (leg.m_footPlantedAnkleHeightMS > leg.m_footRaisedAnkleHeightMS) {
                errorString = "The footPlantedAnkleHeightMS cannot be greater than the footRaisedAnkleHeightMS.";
                return false;
            }
        }
        return true;
    }

    hkbNode* hkbFootIkModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbFootIkModifier(*this);
    }

    void hkbFootIkModifier::setup(const hkbContext& context, const hkaSkeleton* skeleton)
    {
        hkReal scale = 1.0f;
        if (BSIFootIkRaycastInterfaceDB::s_instance) scale = BSIFootIkRaycastInterfaceDB::s_instance->GetScale();

        hkaFootPlacementIkSolver::Setup setup;
        for (int i = 0; i < m_legs.getSize(); i++) {
            setup.m_hipIndex   = m_legs[i].m_hipIndex;
            setup.m_kneeIndex  = m_legs[i].m_kneeIndex;
            setup.m_ankleIndex = m_legs[i].m_ankleIndex;
            if (setup.m_hipIndex >= 0 && setup.m_kneeIndex >= 0 && setup.m_ankleIndex >= 0) {
                setup.m_skeleton                 = skeleton;
                setup.m_kneeAxisLS               = m_legs[i].m_kneeAxisLS;
                setup.m_footEndLS                = m_legs[i].m_footEndLS;
                setup.m_worldUpDirectionWS       = context.getWorldUpWS();
                setup.m_modelUpDirectionMS       = context.m_character->m_setup->m_data->m_modelUpMS;
                setup.m_originalGroundHeightMS   = m_originalGroundHeightMS;
                setup.m_footEndLS.mul4(hkSimdReal(scale));
                setup.m_minAnkleHeightMS         = scale * m_legs[i].m_minAnkleHeightMS;
                setup.m_maxAnkleHeightMS         = scale * m_legs[i].m_maxAnkleHeightMS;
                setup.m_footPlantedAnkleHeightMS = scale * m_legs[i].m_footPlantedAnkleHeightMS;
                setup.m_footRaisedAnkleHeightMS  = scale * m_legs[i].m_footRaisedAnkleHeightMS;
                setup.m_cosineMaxKneeAngle       = ::cosf(m_legs[i].m_maxKneeAngleDegrees * HK_REAL_DEG_TO_RAD);
                setup.m_cosineMinKneeAngle       = ::cosf(m_legs[i].m_minKneeAngleDegrees * HK_REAL_DEG_TO_RAD);
                setup.m_useFootLocking           = m_lockFeetWhenPlanted != false;
                setup.m_raycastDistanceUp        = scale * m_raycastDistanceUp;
                setup.m_raycastDistanceDown      = scale * m_raycastDistanceDown;
                m_internalLegData[i].m_footIkSolver = new hkaFootPlacementIkSolver(setup);
                m_internalLegData[i].m_groundPosition.setZero4();
            }
        }
        m_isSetUp = true;
    }

    void hkbFootIkModifier::alignWorldFromModelUpWithGroundNormal(const hkbContext& context, const hkVector4& upWS, hkbGeneratorOutput& inOut, hkReal gain)
    {
        if (gain > 0.0f) {
            hkVector4      groundNormalWS = hkVector4::getZero();
            hkQsTransform& worldFromModel = inOut.accessWorldFromModel();
            computeGroundNormal(context, worldFromModel, upWS, groundNormalWS);

            hkVector4 modelUpWS;
            modelUpWS.setRotatedDir(worldFromModel.m_rotation, context.m_character->m_setup->m_data->m_modelUpMS);
            modelUpWS.normalize3();
            if (!groundNormalWS.equals3(modelUpWS, 0.1f)) {
                hkQuaternion rotation;
                rotation.setShortestRotation(modelUpWS, groundNormalWS);
                if (getAngle(rotation) > 0.0f) {
                    hkVector4 axis;
                    rotation.getAxis(axis);
                    groundNormalWS = axis;
                    m_alignWithGroundRotation.setAxisAngle(groundNormalWS, getAngle(rotation) * gain);
                    hkQuaternion& rotationWS = inOut.accessWorldFromModel().m_rotation;
                    rotationWS.setMulOutOfLine(m_alignWithGroundRotation, rotationWS);
                }
            }
        }
    }

    /// @note A 2x2 least-squares solve of y = slope·x + offset, x along `axis` and y along `upWS`. The normal equations
    ///       keep both off-diagonal sums of x (vanilla accumulates them separately).
    bool hkbFootIkModifier::fitLine(const hkVector4* points, int numPoints, const hkVector4& axis, const hkVector4& upWS, hkVector4& directionOut) const
    {
        const hkReal n     = hkReal(numPoints);
        hkReal       sumXX = 0.0f;
        hkReal       sumXY = 0.0f;
        hkReal       sumY  = 0.0f;
        hkReal       sumX0 = 0.0f;
        hkReal       sumX1 = 0.0f;
        for (int i = 0; i < numPoints; i++) {
            const hkReal x = points[i].dot3(axis).getReal();
            const hkReal y = points[i].dot3(upWS).getReal();
            sumX0 += x;
            sumY += y;
            sumXY += y * x;
            sumX1 += x;
            sumXX += x * x;
        }

        const hkReal det = n * sumXX - sumX1 * sumX0;
        if (det == 0.0f) return false;

        const hkReal invDet = 1.0f / det;
        const hkReal inv00  = invDet * n;
        const hkReal inv01  = -(invDet * sumX0);
        const hkReal inv10  = -(invDet * sumX1);
        const hkReal inv11  = invDet * sumXX;
        const hkReal slope  = inv00 * sumXY + inv01 * sumY;
        const hkReal offset = inv10 * sumXY + inv11 * sumY;

        hkVector4 start;
        start.setMul4(hkSimdReal(offset), upWS);
        hkVector4 end;
        end.setMul4(hkSimdReal(slope + offset), upWS);
        end.add4(axis);
        hkVector4 direction;
        direction.setSub4(end, start);
        direction.normalize3();
        directionOut = direction;
        if (direction.dot3(axis).getReal() < 0.0f) directionOut.setMul4(hkSimdReal(-1.0f), direction);
        return true;
    }

    void hkbFootIkModifier::computeAverage(const hkVector4* points, int numPoints, hkVector4& averageOut) const
    {
        averageOut = points[0];
        for (int i = 1; i < numPoints; i++) averageOut.add4(points[i]);
        averageOut.mul4(hkSimdReal(1.0f / hkReal(numPoints)));
    }
} }
