/// @file
/// @brief Matching device for hkaSkeleton's destructors: vanilla's linked copies of ~hkaSkeleton and its scalar deleting
///        destructor are COMDATs from a Bethesda unit built with C++ exceptions on (each call is an unwind state).
/// @note Not a Havok file: compiled with /EHsc so the same COMDATs come out; newSkeleton only makes the unit emit them.
#include <Animation/Rig/hkaSkeleton.h>

namespace CB { namespace havok
{
    /// @note Matching device only (see the file note).
    hkaSkeleton* hkaSkeletonEhUnit_newSkeleton()
    {
        return new hkaSkeleton();
    }
} }
