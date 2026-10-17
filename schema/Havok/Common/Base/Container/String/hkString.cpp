/// @file
/// @brief The hkString utilities, reimplemented against vanilla (AE 1.6.1170).
#include <Common/Base/Container/String/hkString.h>

namespace CB { namespace schema
{
    hkBool hkString::beginsWith(const char* s, const char* prefix)
    {
        for (int i = 0; prefix[i] != 0; ++i) {
            if (s[i] != prefix[i]) return false;
        }
        return true;
    }
} }
