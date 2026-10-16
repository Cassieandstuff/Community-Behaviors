/// @file
/// @brief hkbComputeRotationFromAxisAngleModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/ComputeRotationFromAxisAngle/hkbComputeRotationFromAxisAngleModifier.h>

namespace CB { namespace schema
{
    hkbComputeRotationFromAxisAngleModifier::hkbComputeRotationFromAxisAngleModifier()
    {
        m_rotationOut  = hkQuaternion::getIdentity();
        m_axis.set(1.0f, 0.0f, 0.0f);
        m_angleDegrees = 0.0f;
    }

    hkbComputeRotationFromAxisAngleModifier::hkbComputeRotationFromAxisAngleModifier(const hkbComputeRotationFromAxisAngleModifier& mod)
    :   hkbModifier(mod),
        m_rotationOut(mod.m_rotationOut),
        m_axis(mod.m_axis),
        m_angleDegrees(mod.m_angleDegrees)
    {
    }

    void hkbComputeRotationFromAxisAngleModifier::activate(const hkbContext& context)
    {
        m_rotationOut.setIdentity();
    }

    hkbNode* hkbComputeRotationFromAxisAngleModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbComputeRotationFromAxisAngleModifier(*this);
    }

    void hkbComputeRotationFromAxisAngleModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        m_rotationOut.setAxisAngle(m_axis, m_angleDegrees * HK_REAL_DEG_TO_RAD);
    }

    hkReferencedObject* hkbComputeRotationFromAxisAngleModifier::createInternalState()
    {
        return new hkbComputeRotationFromAxisAngleModifierInternalState;
    }

    void hkbComputeRotationFromAxisAngleModifier::getInternalState(hkReferencedObject& internalState) const
    {
        static_cast<hkbComputeRotationFromAxisAngleModifierInternalState&>(internalState).m_rotationOut = m_rotationOut;
    }

    void hkbComputeRotationFromAxisAngleModifier::setInternalState(const hkReferencedObject& internalState)
    {
        m_rotationOut = static_cast<const hkbComputeRotationFromAxisAngleModifierInternalState&>(internalState).m_rotationOut;
    }
} }
