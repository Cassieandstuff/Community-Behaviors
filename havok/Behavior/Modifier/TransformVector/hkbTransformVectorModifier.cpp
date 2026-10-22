/// @file
/// @brief hkbTransformVectorModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/TransformVector/hkbTransformVectorModifierUtils.h>

namespace CB { namespace havok
{
    /// @note Assigned in the body, flags last: vanilla stores the flags after the vectors. VARIANT only because
    ///       vanilla schedules hkbNode's m_cloneState store later (no spelling of hkbNode() moved it).
    hkbTransformVectorModifier::hkbTransformVectorModifier()
    {
        m_rotation          = hkQuaternion::getIdentity();
        m_translation       = hkVector4::getZero();
        m_vectorIn          = hkVector4::getZero();
        m_vectorOut         = hkVector4::getZero();
        m_rotateOnly        = false;
        m_inverse           = false;
        m_computeOnActivate = true;
        m_computeOnModify   = false;
    }

    /// @note The 0x44-byte memCpy copies m_rotation through m_computeOnModify.
    hkbTransformVectorModifier::hkbTransformVectorModifier(const hkbTransformVectorModifier& mod)
    :   hkbModifier(mod)
    {
        hkString::memCpy(&m_rotation, &mod.m_rotation, 0x44);
        m_vectorOut.setZero4();
    }

    void hkbTransformVectorModifier::activate(const hkbContext& context)
    {
        if (m_computeOnActivate) hkbTransformVectorModifierUtils::computeVectorOut(*this);
        else                     m_vectorOut.setZero4();
    }

    hkbNode* hkbTransformVectorModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbTransformVectorModifier(*this);
    }

    void hkbTransformVectorModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        if (m_computeOnModify) hkbTransformVectorModifierUtils::computeVectorOut(*this);
    }

    hkReferencedObject* hkbTransformVectorModifier::createInternalState()
    {
        return new hkbTransformVectorModifierInternalState;
    }

    void hkbTransformVectorModifier::getInternalState(hkReferencedObject& internalState) const
    {
        static_cast<hkbTransformVectorModifierInternalState&>(internalState).m_vectorOut = m_vectorOut;
    }

    void hkbTransformVectorModifier::setInternalState(const hkReferencedObject& internalState)
    {
        m_vectorOut = static_cast<const hkbTransformVectorModifierInternalState&>(internalState).m_vectorOut;
    }
} }
