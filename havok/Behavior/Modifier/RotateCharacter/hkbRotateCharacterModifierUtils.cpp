/// @file
/// @brief hkbRotateCharacterModifierUtils, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/RotateCharacter/hkbRotateCharacterModifierUtils.h>

namespace CB { namespace havok
{
    namespace hkbRotateCharacterModifierUtils
    {
        /// @note Spelling closest to vanilla's code (VARIANT through rule E, SIMD scheduling only): the character transform copied to a
        ///       local before the call (vanilla keeps it in registers across it), one setAxisAngle call per axis (a
        ///       pointer/reference ternary becomes cmov, vanilla branches), and the result built in a local and copied
        ///       out (vanilla's store order and frame size).
        void modify(hkbRotateCharacterModifier& mod, const hkQsTransform& characterWorldFromModel, const hkVector4& modelUp,
                    hkQsTransform& worldFromModel)
        {
            const hkQsTransform worldFromCharacter = characterWorldFromModel;
            hkQsTransform characterFromModel;
            characterFromModel.setMulInverseMul(worldFromCharacter, worldFromModel);

            hkQuaternion rotation;
            if (mod.m_axisOfRotation.equals3(g_vectorConstantZero, 1e-3f)) rotation.setAxisAngle(modelUp, mod.m_angle);
            else                                                           rotation.setAxisAngle(mod.m_axisOfRotation, mod.m_angle);

            hkQsTransform rotatedWorldFromCharacter;
            rotatedWorldFromCharacter.m_translation = worldFromCharacter.m_translation;
            rotatedWorldFromCharacter.m_rotation.setMul(worldFromCharacter.m_rotation, rotation);
            rotatedWorldFromCharacter.m_scale = worldFromCharacter.m_scale;

            hkQsTransform newWorldFromModel;
            newWorldFromModel.setMul(rotatedWorldFromCharacter, characterFromModel);
            newWorldFromModel.m_rotation.normalize();
            worldFromModel = newWorldFromModel;
            mod.m_angle = 0.0f;
        }
    }
} }
