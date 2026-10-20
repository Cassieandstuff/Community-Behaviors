/// @file
/// @brief hkbGenerator: a node that produces a pose.
#pragma once

#include <Behavior/Node/hkbNode.h>

namespace CB { namespace havok
{
    extern const hkClass hkbGeneratorClass;
    struct hkbGeneratorOutput;
    struct hkbNodeInfo;

    class hkbGenerator : public hkbNode
    {
    public:
        HK_FORCE_INLINE hkbGenerator() {}
        HK_FORCE_INLINE hkbGenerator(hkFinishLoadedObjectFlag flag) : hkbNode(flag) {}
        /// @note Inline with no vtable store of its own: vanilla's clone copies call hkbNode's directly.
        HK_FORCE_INLINE hkbGenerator(const hkbGenerator& gen) : hkbNode(gen) {}

        /// @note hkbGenerator's returns true (hkbNode's false): slot 21 tells generators from other nodes.
        virtual bool vfn21();                                                          ///< slot 21

        virtual void generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output,
                              hkReal timeOffset) = 0;                                  ///< slot 23
        /// @note hkbGenerator's returns false.
        virtual bool canRecycleOutput() const;                                         ///< slot 24
        /// @note hkbGenerator's reports no sync points: base frequency and local time 0, playback speed 1. The sync info is
        ///       the head of the node info (hkbNodeInfo::m_syncInfo, +0).
        virtual void updateSync(const hkbContext& context, hkbNodeInfo& info);                ///< slot 25
        /// @note Name provisional (see hkbClipGenerator::setElapsedTime).
        virtual void setElapsedTime(hkReal elapsed);                                   ///< slot 26
        /// @note Name provisional (see hkbClipGenerator::startEcho).
        virtual void startEcho(hkReal echoDuration);                                   ///< slot 27
    };
} }
