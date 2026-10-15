/// @file
/// @brief Havok's scalar types and macros, plus the few platform declarations the runtime needs.
/// @note The schema runtime compiles against the real `<emmintrin.h>` and nothing else from the SDK or the CRT;
///       every engine symbol it touches is declared in this tree. `_INC_MALLOC` keeps the intrinsics' `<malloc.h>` (UCRT)
///       out, so a bare VS2015 `cl` with only `VC\include` builds it.
/// @note The tree opens `namespace CB { namespace schema {` rather than C++17's `namespace CB::schema`: it is compiled
///       with VS2015, which predates nested namespace definitions.
#pragma once

#define _INC_MALLOC
#include <emmintrin.h>

typedef unsigned long DWORD;
extern "C" __declspec(dllimport) void* __stdcall TlsGetValue(DWORD dwTlsIndex);
extern "C" unsigned __int64 __rdtsc();
#pragma intrinsic(__rdtsc)
extern "C" double __cdecl fabs(double);
#pragma intrinsic(fabs)

#define HK_NULL 0
#define HK_FORCE_INLINE __forceinline
#define HK_NEXT_MULTIPLE_OF(ALIGNMENT, VALUE) (((VALUE) + ((ALIGNMENT)-1)) & (~((ALIGNMENT)-1)))
#define HK_REAL_MAX 3.40282e+38f

/// @brief Global placement new, as `<new>` declares it.
/// @note MSVC null-checks its result, which vanilla's hkArray::pushBack carries. hkReferencedObject declares its own,
///       `__assume`-ing the target non-null, for the in-place constructions that have no check.
HK_FORCE_INLINE void* operator new(size_t, void* p) throw() { return p; }

namespace CB { namespace schema
{
    typedef unsigned long long hkUlong;
    typedef unsigned short     hkUint16;
    typedef unsigned int       hkUint32;
    typedef short              hkInt16;
    typedef signed char        hkInt8;
    typedef unsigned char      hkUint8;
    typedef int                hkInt32;
    typedef float              hkReal;

    /// @brief Havok's one-byte boolean.
    /// @note A struct, not `bool`: plain `bool` members reorder stores against vanilla.
    struct hkBool
    {
        char m_bool;
        HK_FORCE_INLINE hkBool() {}
        HK_FORCE_INLINE hkBool(bool b) { m_bool = static_cast<char>(b); }
        HK_FORCE_INLINE operator bool() const { return m_bool != 0; }
    };

    /// @brief The tag a packfile load passes to every finish-loaded constructor.
    struct hkFinishLoadedObjectFlag
    {
        int m_finishing;
    };
} }
