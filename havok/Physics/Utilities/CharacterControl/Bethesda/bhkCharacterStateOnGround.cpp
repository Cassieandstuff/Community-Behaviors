/// @file
/// @brief bhkCharacterStateOnGround, reimplemented against vanilla (AE 1.6.1170).
/// @note Simulate (0x140f02550) is still being matched (tools/cbmatch/workbench, parked.yaml).
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterState.h>

namespace CB { namespace havok
{
    bhkCharacterStateOnGround::bhkCharacterStateOnGround()
    :   m_killVelocityOnLaunch(true)
    {
    }

    hkpCharacterStateType bhkCharacterStateOnGround::getType() const
    {
        return HK_CHARACTER_ON_GROUND;
    }
} }
