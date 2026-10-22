/// @file
/// @brief bhkCharRigidBodyController functions still being matched (see tools/cbmatch/parked.yaml).
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharRigidBodyController.h>
#include <Physics/Dynamics/Entity/hkpRigidBody.h>

namespace CB { namespace havok
{
    void bhkCharRigidBodyController::characterCallback(hkpWorld* world, hkpCharacterRigidBody* characterRB)
    {
        hkVector4 position;
        GetPositionImpl(position, true);
        const hkQuadReal q = position.m_quad;
        const float x = q.m128_f32[0] * 69.99125f;
        const float y = q.m128_f32[1] * 69.99125f;
        const float z = q.m128_f32[2] * 69.99125f;
        if (!IsFlagSet(17)) {
            bhkCharacterMoveFinishEvent event;
            event.m_position.x = x;
            event.m_position.y = y;
            event.m_position.z = z;
            SendEvent(&event);
        }
        m_flags &= ~0x400u;
        hkpCharacterRigidBodyListener::characterCallback(world, characterRB);
        ProcessCharacterContacts();
    }
} }

