/// @file
/// @brief hkbHandIkControlsModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/HandIkControls/hkbHandIkControlsModifier.h>

#include <Behavior/Modifier/HandIkControls/hkbHandIkControlsModifierUtils.h>
#include <Behavior/Utils/hkbHandle.h>
#include <Common/Base/Container/LocalArray/hkLocalBuffer.h>

namespace CB { namespace havok
{
    hkbHandIkControlsModifier::hkbHandIkControlsModifier(const hkbHandIkControlsModifier& mod)
    :   hkbModifier(mod)
    {
        m_hands = mod.m_hands;
    }

    void hkbHandIkControlsModifier::deactivate(const hkbContext& context)
    {
        for (int i = 0; i < m_hands.getSize(); i++) {
            hkbHandle* handle = m_hands[i].m_controlData.m_targetHandle;
            if (handle) {
                handle->removeReference();
                m_hands[i].m_controlData.m_targetHandle = HK_NULL;
            }
        }
    }

    bool hkbHandIkControlsModifier::isValid(hkStringPtr& errorString) const
    {
        if (m_hands.getSize() > 4) {
            errorString = "Too many hands.";
            return false;
        }

        hkLocalBuffer<hkBool> used(4);
        hkString::memSet(&used[0], 0, 4);
        for (int i = 0; i < m_hands.getSize(); i++) {
            const int handIndex = m_hands[i].m_handIndex;
            if (used[handIndex]) {
                errorString = "Each hand must have a unique index.";
                return false;
            }
            used[handIndex] = true;
            if (m_hands[i].m_handIndex >= 4) {
                errorString = "Invalid hand index.";
                return false;
            }
        }
        return true;
    }

    hkbNode* hkbHandIkControlsModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        if (m_variableBindingSet == HK_NULL) {
            hkbHandIkControlsModifier* self = const_cast<hkbHandIkControlsModifier*>(this);
            self->m_cloneState              = CLONE_STATE_SHARABLE;
            addReference();
            return self;
        }
        return new hkbHandIkControlsModifier(*this);
    }

    void hkbHandIkControlsModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        hkbHandIkControlsModifierUtils::modify(*this, inOut);
    }
} }
