/// @file
/// @brief hkbHandIkControlsModifierUtils, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/HandIkControls/hkbHandIkControlsModifierUtils.h>

#include <Behavior/Generator/hkbGeneratorOutput.h>

namespace CB { namespace havok
{
    void hkbHandIkControlsModifierUtils::modify(const hkbHandIkControlsModifier& mod, hkbGeneratorOutput& output)
    {
        const int numHands = mod.m_hands.getSize();
        for (int i = 0; i < numHands; i++) {
            const int handIndex = mod.m_hands[i].m_handIndex;
            const int track     = hkbGeneratorOutput::TRACK_HAND_IK_CONTROLS_0 + handIndex;
            const int nbTrack   = hkbGeneratorOutput::TRACK_HAND_IK_CONTROLS_NON_BLENDABLE_0 + handIndex;
            if (mod.m_hands[i].m_enable) {
                hkbHandIkControlData* data   = static_cast<hkbHandIkControlData*>(output.getTrackData(track));
                hkbHandIkControlData* nbData = static_cast<hkbHandIkControlData*>(output.getTrackData(nbTrack));
                *data   = mod.m_hands[i].m_controlData;
                *nbData = mod.m_hands[i].m_controlData;
                if (mod.m_hands[i].m_controlData.m_transformOnFraction == 0.0f) {
                    data->m_targetPosition.setZero4();
                    nbData->m_targetPosition.setZero4();
                } else {
                    data->m_targetPosition(3)   = 1.0f;
                    nbData->m_targetPosition(3) = 1.0f;
                }
                output.getTrackHeader(track).m_onFraction   = 1.0f;
                output.getTrackHeader(nbTrack).m_onFraction = 1.0f;
            }
        }
    }
} }
