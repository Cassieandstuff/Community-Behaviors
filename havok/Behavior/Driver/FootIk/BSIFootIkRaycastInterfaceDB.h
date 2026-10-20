/// @file
/// @brief BSIFootIkRaycastInterfaceDB: Bethesda's hook into Havok's foot IK — the per-character raycast interface and
///        the length scale foot IK works in.
/// @note Bethesda's (the RTTI names BSIFootIkRaycastInterfaceDB and its one implementation,
///       BGSFootIkRaycastInterfaceDB); declared only, nothing of it is reimplemented. The file sits beside the foot IK
///       driver because both the driver and hkbFootIkModifier read it.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    class hkaRaycastInterface;
    struct hkbCharacter;

    class BSIFootIkRaycastInterfaceDB
    {
    public:
        /// @brief The game's instance (BGSFootIkRaycastInterfaceDB's singleton); null until the game creates it.
        /// @note Name provisional (0x1431b2818).
        static BSIFootIkRaycastInterfaceDB* s_instance;

        virtual ~BSIFootIkRaycastInterfaceDB();                                      ///< slot 0
        /// @brief The raycast interface for `character`'s foot IK (BGS's creates one per character on first use).
        /// @note Name provisional.
        virtual hkaRaycastInterface* GetRaycastInterface(hkbCharacter* character) = 0;   ///< slot 1
        /// @brief The factor foot IK lengths (heights, ray distances, foot ends) are scaled by.
        /// @note Name provisional; BGS's returns 1.
        virtual hkReal GetScale() = 0;                                                ///< slot 2
    };
} }
