/// @file
/// @brief hkpContactListener: told about an entity's contact points.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @note 8 bytes (the vtable, 0x1417ec570). Opaque so far: BSRagdollContactListenerModifier embeds one at +0x50 and
    ///       registers it with hkpEntity::addContactListener; its slots are not identified.
    class hkpContactListener
    {
    public:
        virtual ~hkpContactListener() {}
    };
} }
