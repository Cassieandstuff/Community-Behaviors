/// @file
/// @brief hkbGenerator: a node that produces a pose.
#pragma once

#include <Behavior/Node/hkbNode.h>

namespace CB { namespace schema
{
    extern const hkClass hkbGeneratorClass;
    struct hkbGeneratorOutput;

    class hkbGenerator : public hkbNode
    {
    public:
        HK_FORCE_INLINE hkbGenerator(hkFinishLoadedObjectFlag flag) : hkbNode(flag) {}
        /// @note Inline with no vtable store of its own: vanilla's clone copies call hkbNode's directly.
        HK_FORCE_INLINE hkbGenerator(const hkbGenerator& gen) : hkbNode(gen) {}

        virtual void generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output,
                              hkReal timeOffset);   ///< slot 23
        /// @note Slots 24 and 25 are placeholders: declared so a subclass's own slots land where vanilla's do.
        virtual void v24();
        virtual void v25();
    };
} }
