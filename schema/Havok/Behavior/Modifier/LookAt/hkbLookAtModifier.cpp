/// @file
/// @brief hkbLookAtModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/LookAt/hkbLookAtModifier.h>

#include <Animation/Ik/LookAt/hkaLookAtIkSolver.h>
#include <Animation/Rig/hkaPose.h>
#include <Behavior/Character/hkbCharacter.h>
#include <Behavior/Generator/hkbGeneratorOutputUtils.h>

namespace CB { namespace schema
{
    hkbLookAtModifier::hkbLookAtModifier()
    :   m_targetWS(hkVector4::getZero()),
        m_headForwardLS(0.0f, 1.0f, 0.0f, 0.0f),
        m_neckForwardLS(0.0f, 1.0f, 0.0f, 0.0f),
        m_neckRightLS(1.0f, 0.0f, 0.0f, 0.0f),
        m_eyePositionHS(0.0f, 0.0f, 0.0f, 0.0f),
        m_newTargetGain(0.2f),
        m_onGain(0.05f),
        m_offGain(0.05f),
        m_limitAngleDegrees(45.0f),
        m_limitAngleLeft(90.0f),
        m_limitAngleRight(-90.0f),
        m_limitAngleUp(30.0f),
        m_limitAngleDown(-30.0f),
        m_headIndex(-1),
        m_neckIndex(-1),
        m_isOn(true),
        m_individualLimitsOn(false),
        m_isTargetInsideLimitCone(false),
        m_lookAtLastTargetWS(hkVector4::getZero()),
        m_lookAtWeight(0.0f)
    {
    }

    /// @note The settings (m_targetWS through m_individualLimitsOn) copy as one 0x76-byte block.
    hkbLookAtModifier::hkbLookAtModifier(const hkbLookAtModifier& mod)
    :   hkbModifier(mod),
        m_isTargetInsideLimitCone(false),
        m_lookAtLastTargetWS(hkVector4::getZero()),
        m_lookAtWeight(0.0f)
    {
        hkString::memCpy(&m_targetWS, &mod.m_targetWS, 0x76);
    }

    void hkbLookAtModifier::activate(const hkbContext& context)
    {
        m_lookAtLastTargetWS.setZero4();
        m_lookAtWeight            = 0.0f;
        m_isTargetInsideLimitCone = false;
    }

    bool hkbLookAtModifier::isValid(hkStringPtr& errorString) const
    {
        const hkbVariableBindingSet* bindings = m_variableBindingSet;
        const bool boneValid[2] = { m_headIndex >= 0 || (bindings && bindings->findBindingByMemberPath("headIndex")),
                                     m_neckIndex >= 0 || (bindings && bindings->findBindingByMemberPath("neckIndex")) };

        if (boneValid[0] + boneValid[1] < 2) {
            errorString = "You must specify both the head and the neck bone.";
            return false;
        }
        return true;
    }

    hkbNode* hkbLookAtModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbLookAtModifier(*this);
    }

    hkReferencedObject* hkbLookAtModifier::createInternalState()
    {
        return new hkbLookAtModifierInternalState;
    }

    void hkbLookAtModifier::getInternalState(hkReferencedObject& internalState) const
    {
        hkbLookAtModifierInternalState& state = static_cast<hkbLookAtModifierInternalState&>(internalState);
        state.m_lookAtLastTargetWS      = m_lookAtLastTargetWS;
        state.m_lookAtWeight            = m_lookAtWeight;
        state.m_isTargetInsideLimitCone = m_isTargetInsideLimitCone;
    }

    void hkbLookAtModifier::setInternalState(const hkReferencedObject& internalState)
    {
        const hkbLookAtModifierInternalState& state = static_cast<const hkbLookAtModifierInternalState&>(internalState);
        m_lookAtLastTargetWS      = state.m_lookAtLastTargetWS;
        m_lookAtWeight            = state.m_lookAtWeight;
        m_isTargetInsideLimitCone = state.m_isTargetInsideLimitCone;
    }

} }
