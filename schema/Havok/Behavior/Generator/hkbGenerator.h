/// @file
/// @brief hkbGenerator: a node that produces a pose.
#pragma once

#include <Behavior/Node/hkbNode.h>

namespace CB { namespace schema
{
    class hkbGenerator : public hkbNode
    {
    public:
        HK_FORCE_INLINE hkbGenerator(hkFinishLoadedObjectFlag flag) : hkbNode(flag) {}
        /// @note Inline with no vtable store of its own: vanilla's clone copies call hkbNode's directly.
        HK_FORCE_INLINE hkbGenerator(const hkbGenerator& gen) : hkbNode(gen) {}
    };
} }
