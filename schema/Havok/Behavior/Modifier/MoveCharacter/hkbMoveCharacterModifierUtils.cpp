/// @file
/// @brief hkbMoveCharacterModifierUtils, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/MoveCharacter/hkbMoveCharacterModifierUtils.h>

#include <Behavior/Generator/hkbGeneratorOutput.h>

namespace CB { namespace schema
{
    namespace hkbMoveCharacterModifierUtils
    {
        void modify(hkbMoveCharacterModifier& modifier, hkbGeneratorOutput& inOut)
        {
            if (modifier.m_timeSinceLastModify > 0.0f) {
                hkQsTransform& worldFromModel = inOut.accessWorldFromModel();
                hkVector4 offsetMS;
                offsetMS.setMul4(modifier.m_offsetPerSecondMS, hkSimdReal(modifier.m_timeSinceLastModify));
                hkVector4 offsetWS;
                offsetWS.setRotatedDir(worldFromModel.m_rotation, offsetMS);
                worldFromModel.m_translation.setAdd4(offsetWS, worldFromModel.m_translation);
                modifier.m_timeSinceLastModify = 0.0f;
            }
        }
    }
} }
