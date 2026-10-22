/// @file
/// @brief hkbSetWorldFromModelModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/SetWorldFromModel/hkbSetWorldFromModelModifier.h>

namespace CB { namespace havok
{
    /// @note Assigned in the body, flags last: vanilla stores the flags after the vectors. VARIANT only because
    ///       vanilla schedules hkbNode's m_cloneState store later (no spelling of hkbNode() moved it).
    hkbSetWorldFromModelModifier::hkbSetWorldFromModelModifier()
    {
        m_translation    = hkVector4::getZero();
        m_rotation       = hkQuaternion::getIdentity();
        m_setTranslation = true;
        m_setRotation    = true;
    }

    /// @note The 0x22-byte memCpy copies m_translation through m_setRotation.
    hkbSetWorldFromModelModifier::hkbSetWorldFromModelModifier(const hkbSetWorldFromModelModifier& mod)
    :   hkbModifier(mod)
    {
        hkString::memCpy(&m_translation, &mod.m_translation, 0x22);
    }

    hkbNode* hkbSetWorldFromModelModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbSetWorldFromModelModifier(*this);
    }

    void hkbSetWorldFromModelModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        if (m_setTranslation) inOut.accessWorldFromModel().m_translation = m_translation;
        if (m_setRotation)    inOut.accessWorldFromModel().m_rotation    = m_rotation;
    }
} }
