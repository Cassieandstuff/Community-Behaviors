/// @file
/// @brief hkbHandIkDriver: only what the reimplemented code calls.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    class hkbHandIkDriver : public hkReferencedObject
    {
    public:
        char   m_pad10[0x20 - 0x10];
        /// @note Name provisional: cleared by reset so the driver sets its hands up again.
        hkBool m_isSetUp;   ///< +0x20

        /// @note Out of line in vanilla (0x140ae0830).
        void reset();
    };
} }
