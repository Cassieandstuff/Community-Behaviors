/// @file
/// @brief hkaSkeletonMapper: maps poses and extracted motion between two skeletons.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    struct hkaSkeletonMapper : hkReferencedObject
    {
        /// @brief Applies the mapping's extracted-motion transform.
        void mapExtractedMotion(const hkQsTransform& motionIn, hkQsTransform& motionOut) const;
    };
} }
