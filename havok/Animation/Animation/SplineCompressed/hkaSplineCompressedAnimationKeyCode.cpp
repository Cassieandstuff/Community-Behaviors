/// @file
/// @brief hkaSplineCompressedAnimation::computePackedNurbsOffsets, compiled apart from the class's own unit.
/// @note A matching device named for what it holds: vanilla compiled this accessor in another unit (0x140c38450, beside
///       the compression helpers), behind Havok's animation key-code check. That unit's real name is unknown.
#include <Animation/Animation/SplineCompressed/hkaSplineCompressedAnimation.h>

namespace CB { namespace havok
{
    /// @brief Set once Havok's animation-product key code has validated.
    /// @note Name provisional (0x1431c9bd0).
    extern hkBool hkaKeyCode_isValid;
    /// @brief Validates the product key code, setting `isValidOut`.
    /// @note Name provisional (0x140c9f1f0); returns a status code no caller here reads.
    int hkaKeyCode_check(hkBool& isValidOut);

    const hkUint8* hkaSplineCompressedAnimation::computePackedNurbsOffsets(const hkUint8* base, const hkUint32* blockOffsets, hkUint32 block, hkUint32 offset)
    {
        if (!hkaKeyCode_isValid) {
            hkaKeyCode_check(hkaKeyCode_isValid);
            if (!hkaKeyCode_isValid) return HK_NULL;
        }
        return base + blockOffsets[block] + (offset & 0x7fffffff);
    }
} }
