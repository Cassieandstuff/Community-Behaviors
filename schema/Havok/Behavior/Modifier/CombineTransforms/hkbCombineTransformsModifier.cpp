/// @file
/// @brief hkbCombineTransformsModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/CombineTransforms/hkbCombineTransformsModifierUtils.h>

namespace CB { namespace schema
{
    /// @note VARIANT only because vanilla schedules hkbNode's m_cloneState store later (as in the other modifiers).
    hkbCombineTransformsModifier::hkbCombineTransformsModifier()
    {
        m_translationOut       = hkVector4::getZero();
        m_rotationOut          = hkQuaternion::getIdentity();
        m_leftTranslation      = hkVector4::getZero();
        m_leftRotation         = hkQuaternion::getIdentity();
        m_rightTranslation     = hkVector4::getZero();
        m_rightRotation        = hkQuaternion::getIdentity();
        m_invertLeftTransform  = false;
        m_invertRightTransform = false;
        m_invertResult         = false;
    }

    hkbCombineTransformsModifier::hkbCombineTransformsModifier(const hkbCombineTransformsModifier& mod)
    :   hkbModifier(mod),
        m_translationOut(mod.m_translationOut),
        m_rotationOut(mod.m_rotationOut),
        m_leftTranslation(mod.m_leftTranslation),
        m_leftRotation(mod.m_leftRotation),
        m_rightTranslation(mod.m_rightTranslation),
        m_rightRotation(mod.m_rightRotation),
        m_invertLeftTransform(mod.m_invertLeftTransform),
        m_invertRightTransform(mod.m_invertRightTransform),
        m_invertResult(mod.m_invertResult)
    {
    }

    void hkbCombineTransformsModifier::activate(const hkbContext& context)
    {
        m_translationOut.setZero4();
        m_rotationOut.setIdentity();
    }

    hkbNode* hkbCombineTransformsModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbCombineTransformsModifier(*this);
    }

    void hkbCombineTransformsModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        hkbCombineTransformsModifierUtils::modify(*this);
    }

    hkReferencedObject* hkbCombineTransformsModifier::createInternalState()
    {
        return new hkbCombineTransformsModifierInternalState;
    }

    void hkbCombineTransformsModifier::getInternalState(hkReferencedObject& internalState) const
    {
        hkbCombineTransformsModifierInternalState& state = static_cast<hkbCombineTransformsModifierInternalState&>(internalState);
        state.m_translationOut = m_translationOut;
        state.m_rotationOut    = m_rotationOut;
    }

    void hkbCombineTransformsModifier::setInternalState(const hkReferencedObject& internalState)
    {
        const hkbCombineTransformsModifierInternalState& state =
            static_cast<const hkbCombineTransformsModifierInternalState&>(internalState);
        m_translationOut = state.m_translationOut;
        m_rotationOut    = state.m_rotationOut;
    }
} }
