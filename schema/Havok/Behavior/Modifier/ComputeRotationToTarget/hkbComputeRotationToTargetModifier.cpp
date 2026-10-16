/// @file
/// @brief hkbComputeRotationToTargetModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/ComputeRotationToTarget/hkbComputeRotationToTargetModifierUtils.h>

namespace CB { namespace schema
{
    /// @note The two direction defaults are compiler literals (_mm_setr_ps), the rest Havok's constant table. VARIANT only
    ///       for store scheduling: vanilla stores hkbNode's m_cloneState later and m_resultIsDelta between the literals.
    hkbComputeRotationToTargetModifier::hkbComputeRotationToTargetModifier()
    {
        m_rotationOut     = hkQuaternion::getIdentity();
        m_targetPosition  = hkVector4::getZero();
        m_currentPosition = hkVector4::getZero();
        m_currentRotation = hkQuaternion::getIdentity();
        m_resultIsDelta   = false;
        m_localAxisOfRotation.set(0.0f, 0.0f, 1.0f);
        m_localFacingDirection.set(1.0f, 0.0f, 0.0f);
    }

    hkbComputeRotationToTargetModifier::hkbComputeRotationToTargetModifier(const hkbComputeRotationToTargetModifier& mod)
    :   hkbModifier(mod),
        m_rotationOut(mod.m_rotationOut),
        m_targetPosition(mod.m_targetPosition),
        m_currentPosition(mod.m_currentPosition),
        m_currentRotation(mod.m_currentRotation),
        m_localAxisOfRotation(mod.m_localAxisOfRotation),
        m_localFacingDirection(mod.m_localFacingDirection),
        m_resultIsDelta(mod.m_resultIsDelta)
    {
    }

    void hkbComputeRotationToTargetModifier::activate(const hkbContext& context)
    {
        m_rotationOut.setIdentity();
    }

    hkbNode* hkbComputeRotationToTargetModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbComputeRotationToTargetModifier(*this);
    }

    void hkbComputeRotationToTargetModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        hkbComputeRotationToTargetModifierUtils::modify(*this);
    }

    hkReferencedObject* hkbComputeRotationToTargetModifier::createInternalState()
    {
        return new hkbComputeRotationToTargetModifierInternalState;
    }

    void hkbComputeRotationToTargetModifier::getInternalState(hkReferencedObject& internalState) const
    {
        static_cast<hkbComputeRotationToTargetModifierInternalState&>(internalState).m_rotationOut = m_rotationOut;
    }

    void hkbComputeRotationToTargetModifier::setInternalState(const hkReferencedObject& internalState)
    {
        m_rotationOut = static_cast<const hkbComputeRotationToTargetModifierInternalState&>(internalState).m_rotationOut;
    }
} }
