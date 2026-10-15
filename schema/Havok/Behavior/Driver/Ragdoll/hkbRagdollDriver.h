/// @file
/// @brief hkbRagdollDriver: only what the behavior nodes call.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    class hkaRagdollInstance;

    class hkbRagdollDriver : public hkReferencedObject
    {
    public:
        /// @note Out of line in vanilla (returns +0x88).
        hkaRagdollInstance* getRagdoll() const;
    };
} }
