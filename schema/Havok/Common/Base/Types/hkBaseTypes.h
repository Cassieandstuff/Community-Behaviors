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
/// @note Declared with <windows.h>'s own parameter type, so the plugin (which includes it) sees one function.
struct _RTL_CRITICAL_SECTION;
extern "C" __declspec(dllimport) void __stdcall DeleteCriticalSection(_RTL_CRITICAL_SECTION* lpCriticalSection);
extern "C" unsigned __int64 __rdtsc();
#pragma intrinsic(__rdtsc)
extern "C" double __cdecl fabs(double);
#pragma intrinsic(fabs)
extern "C" float __cdecl asinf(float);
extern "C" float __cdecl acosf(float);
extern "C" float __cdecl cosf(float);
extern "C" float __cdecl sqrtf(float);
extern "C" float __cdecl fmodf(float, float);
#pragma intrinsic(sqrtf)
/// @brief HK_BREAKPOINT: the `int3` an HK_ERROR site executes when the error handler asks for a break.
extern "C" void __cdecl __debugbreak(void);
#pragma intrinsic(__debugbreak)

#define HK_NULL 0
#define HK_FORCE_INLINE __forceinline
/// @brief Marks a struct containers may copy as raw bytes though a member has a constructor (see hkIsPodType).
#define HK_DECLARE_POD_TYPE() typedef void hkIsPodTypeTag
#define HK_NEXT_MULTIPLE_OF(ALIGNMENT, VALUE) (((VALUE) + ((ALIGNMENT)-1)) & (~((ALIGNMENT)-1)))
#define HK_REAL_MAX 3.40282e+38f
/// @brief Havok's "very large" real (bits 0x5f7ffff0, 2^64 - 2^44): what an infinite inertia reads back as.
#define HK_REAL_HIGH 1.8446726e+019f
#define HK_REAL_EPSILON 1.192092896e-07f
#define HK_REAL_PI 3.14159265358979f
#define HK_REAL_DEG_TO_RAD (HK_REAL_PI / 180.0f)
#define HK_REAL_RAD_TO_DEG 57.2957795f
#define HK_REAL_EPSILON 1.192092896e-07f

/// @brief Global placement new, as `<new>` declares it.
/// @note MSVC null-checks its result, which vanilla's hkArray::pushBack carries. hkReferencedObject declares its own,
///       `__assume`-ing the target non-null, for the in-place constructions that have no check.
/// @note Guarded by the CRT's own macro: the bare VS2015 build has no `<new>` and defines it here; the plugin (whose
///       function palette includes these headers) already has `<new>`'s.
#ifndef __PLACEMENT_NEW_INLINE
#define __PLACEMENT_NEW_INLINE
HK_FORCE_INLINE void* operator new(size_t, void* p) throw() { return p; }
#endif

namespace CB { namespace schema
{
    typedef unsigned long long hkUlong;
    typedef long long          hkLong;
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
        /// @note Havok's own: assigning a bool through it (not a temporary hkBool) keeps vanilla's store before the next
        ///       member's load (`m_a = !m_b; m_c = !m_d;` in BSIsActiveModifier::activate), and made nine other VARIANTs EXACT.
        HK_FORCE_INLINE hkBool& operator=(bool e) { m_bool = static_cast<char>(e); return *this; }
    };

    /// @brief Havok's enum stored in a narrower integer.
    /// @note A class, not the bare integer: an hkEnum local whose value passes through hkMath::max2 lives in a stack
    ///       slot whose address is taken (hkbGeneratorTransitionEffect's stage), as vanilla's does.
    template <typename ENUM, typename STORAGE>
    class hkEnum
    {
    public:
        HK_FORCE_INLINE hkEnum() {}
        HK_FORCE_INLINE hkEnum(ENUM e) { m_storage = static_cast<STORAGE>(e); }
        HK_FORCE_INLINE operator ENUM() const { return static_cast<ENUM>(m_storage); }
        HK_FORCE_INLINE void operator=(ENUM e) { m_storage = static_cast<STORAGE>(e); }
        HK_FORCE_INLINE bool operator==(ENUM e) const { return m_storage == static_cast<STORAGE>(e); }
        HK_FORCE_INLINE bool operator!=(ENUM e) const { return m_storage != static_cast<STORAGE>(e); }

        STORAGE m_storage;
    };

    /// @brief The tag a packfile load passes to every finish-loaded constructor.
    struct hkFinishLoadedObjectFlag
    {
        int m_finishing;
    };

    /// @brief A simulation time in seconds (hkpWorld::getCurrentTime, hkpMotion::approxTransformAt).
    typedef hkReal hkTime;

    /// @brief A 16-bit float: the upper half of an IEEE single.
    class hkHalf
    {
    public:
        hkInt16 m_value;
    };
} }
