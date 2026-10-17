/// @file
/// @brief bhkCharacterController functions still being matched (see tools/cbmatch/parked.yaml).
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterController.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkWorld.h>

namespace CB { namespace schema
{
    float bhkCharacterController::GetFallDistance()
    {
        if (m_fallStartHeight != 0.0f) {
            hkVector4 position;
            GetPositionImpl(position, true);
            const float height = position(2) * bhkWorld::s_worldScaleInverse;
            return hkMath::max2(m_fallStartHeight - height, 0.0f);
        }
        return 0.0f;
    }
} }
