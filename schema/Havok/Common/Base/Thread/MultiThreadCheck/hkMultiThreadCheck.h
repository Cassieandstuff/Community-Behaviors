/// @file
/// @brief hkMultiThreadCheck: the debug read/write marker every world object carries.
#pragma once

#include <Common/Base/Types/hkBaseTypes.h>

namespace CB { namespace schema
{
    /// @note 12 bytes (schema/physics/hkMultiThreadCheck.yaml). Release builds compile markForRead/markForWrite to nothing,
    ///       so vanilla never touches it.
    class hkMultiThreadCheck
    {
    public:
        hkUint32 m_threadId;       ///< +0x00
        hkInt32  m_stackTraceId;   ///< +0x04
        hkUint16 m_markCount;      ///< +0x08
        hkUint16 m_markBitStack;   ///< +0x0a
    };
} }
