/// @file
/// @brief bhkCollisionBox, reimplemented against vanilla (AE 1.6.1170).
/// @note Bethesda's engine code: built with C++ exceptions on and /Ob2, which the manifest sets on this unit. Vanilla
///       lays bhkCollisionBox's functions (0x140f03530..0x140f037ae) right after bhkCharControllerShape's
///       (0x140f02e60..0x140f03520).
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCollisionBox.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterController.h>
#include <Physics/Utilities/CharacterControl/Bethesda/NiAVObject.h>

namespace CB { namespace havok
{
    /// @note Statement order follows vanilla's stores: the bound center read between the rotation's columns, each
    ///       position's w stored first.
    void bhkCollisionBox::UpdateFromController()
    {
        if (!s_frozen && m_controller) {
            const NiAVObject* node = bhkGetNiObject(m_controller->GetRigidBody(), 0);
            hkVector4         position;
            float             x, y, z;
            if (node) {
                hkTransform transform;
                const NiMatrix3& rotate = node->m_world.m_rotate;
                transform.m_rotation.m_col0(0) = rotate.m_entry[0][0];
                transform.m_rotation.m_col0(1) = rotate.m_entry[1][0];
                transform.m_rotation.m_col0(2) = rotate.m_entry[2][0];
                transform.m_rotation.m_col0(3) = 0.0f;
                x = node->m_worldBound.m_center.x;
                y = node->m_worldBound.m_center.y;
                z = node->m_worldBound.m_center.z;
                transform.m_rotation.m_col1(0) = rotate.m_entry[0][1];
                transform.m_rotation.m_col1(1) = rotate.m_entry[1][1];
                transform.m_rotation.m_col1(2) = rotate.m_entry[2][1];
                transform.m_rotation.m_col1(3) = 0.0f;
                transform.m_rotation.m_col2(0) = rotate.m_entry[0][2];
                transform.m_rotation.m_col2(1) = rotate.m_entry[1][2];
                transform.m_rotation.m_col2(2) = rotate.m_entry[2][2];
                transform.m_rotation.m_col2(3) = 0.0f;
                position(3) = 0.0f;
                position(0) = node->m_world.m_translate.x * 0.0142875f;
                position(1) = node->m_world.m_translate.y * 0.0142875f;
                position(2) = node->m_world.m_translate.z * 0.0142875f;
                transform.m_translation = position;
                SetTransform(transform);
            } else {
                m_controller->GetPositionImpl(position, false);
                x = position(0) * 69.99125f;
                y = position(1) * 69.99125f;
                z = position(2) * 69.99125f;
            }
            position(3) = 0.0f;
            position(0) = x * 0.0142875f;
            position(1) = y * 0.0142875f;
            position(2) = z * 0.0142875f;
            SetPosition(position);
        }
    }
} }
