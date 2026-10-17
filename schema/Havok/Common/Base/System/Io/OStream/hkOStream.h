/// @file
/// @brief hkOstream and hkErrStream: the text stream HK_WARN-style warnings are formatted into.
#pragma once

#include <Common/Base/Object/hkReferencedObject.h>

namespace CB { namespace schema
{
    /// @note Only what reimplemented callers use is declared; every body is vanilla's (out of line).
    class hkOstream : public hkReferencedObject
    {
    public:
        virtual ~hkOstream();   ///< 0x140a94b00

        hkOstream& operator<<(const char* s);   ///< 0x140a94c30

        void* m_writer;   ///< +0x10, the stream writer (a reference-counted hkStreamWriter)
    };

    /// @brief An hkOstream writing into a caller-owned character buffer, zero-terminated.
    /// @note The destructor is implicit: vanilla's warning sites call ~hkOstream directly.
    class hkErrStream : public hkOstream
    {
    public:
        hkErrStream(void* buf, int bufSize);   ///< 0x140a8cac0
    };
} }
