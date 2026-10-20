/// @file
/// @brief hkbFootIkControlsModifierUtils, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/FootIk/hkbFootIkControlsModifierUtils.h>

#include <Behavior/Generator/hkbGeneratorOutput.h>

namespace CB { namespace havok
{
    void hkbFootIkControlsModifierUtils::modify(hkbFootIkControlsModifier& mod, hkbGeneratorOutput& output, const hkbFootIkDriver::InternalLegData* legs, int numLegs)
    {
        if (output.isValid(hkbGeneratorOutput::TRACK_FOOT_IK_CONTROLS)) {
            hkbFootIkControlData* data = static_cast<hkbFootIkControlData*>(output.getTrackData(hkbGeneratorOutput::TRACK_FOOT_IK_CONTROLS));
            hkbGeneratorOutput::TrackHeader& header = output.getTrackHeader(hkbGeneratorOutput::TRACK_FOOT_IK_CONTROLS);
            if (header.m_numData > 0) {
                hkString::memCpy4(data, &mod.m_controlData, sizeof(hkbFootIkControlData) / 4);
                header.m_onFraction = 1.0f;
            }
            copyLegs(mod, legs, numLegs);
        }
    }

    void hkbFootIkControlsModifierUtils::copyLegs(hkbFootIkControlsModifier& mod, const hkbFootIkDriver::InternalLegData* legs, int numLegs)
    {
        const int numModLegs = mod.m_legs.getSize();
        for (int i = 0; i < numModLegs; i++) {
            hkbFootIkControlsModifier::Leg& leg = mod.m_legs[i];
            if (i < numLegs) {
                leg.m_groundPosition = legs[i].m_groundPosition;
                leg.m_verticalError  = legs[i].m_verticalError;
                leg.m_hitSomething   = legs[i].m_hitSomething;
                leg.m_isPlantedMS    = legs[i].m_isPlantedMS;
            } else {
                leg.m_verticalError = 0.0f;
                leg.m_groundPosition.setZero4();
                leg.m_hitSomething = false;
                leg.m_isPlantedMS  = false;
            }
        }
    }
} }
