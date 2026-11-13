#pragma once

#include <hky/layout/CBDistanceMatchModifier.h>
#include <Common/Base/hkBase.h>
#include <Behavior/Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Behavior/Context/hkbContext.h>
#include <Behavior/Behavior/Modifier/hkbModifier.h>

/// @file
/// @brief CBDistanceMatchModifier, CB's own Havok modifier: plays a user-controlled clip by distance instead of the clock
///        (locomotion/DistanceMatch). The layout is schema/modifiers/CBDistanceMatchModifier.yaml's: the platform
///        registers the class from that descriptor (platform/classes) and checks the sizes agree.
/// @note  Why a modifier: Havok's modifiers compute and its bindings route, so it sits beside an ordinary clip generator
///        (bound to the variable it writes) without replacing it, in any graph; a modifier generator wraps it as usual.
///        The pose search node runs the same core itself for its Stops and Pivots databases.

namespace CB::feature::controller {

    class DistanceMatchModifier : public ::hkbModifier {
    public:
        /// @brief What it plays the clip by (schema enum DistanceSource).
        enum Source : ::hkInt8 { kStop = 0, kPivot = 1, kStart = 2, kDistance = 3 };

        ::hkStringPtr m_animationName;   ///< +0x50
        ::hkStringPtr m_timeVariable;    ///< +0x58
        ::hkReal      m_distance;        ///< +0x60
        ::hkInt8      m_source;          ///< +0x64
        ::hkReal      m_minRate;         ///< +0x68
        ::hkReal      m_maxRate;         ///< +0x6c
        float         m_clipTime = 0;    ///< +0x70, runtime: seconds into the clip
        float         m_unused   = 0;    ///< +0x74, runtime

        /// @brief The finish constructor: the vtable on loaded bytes; serialized members stay as loaded.
        explicit DistanceMatchModifier(::hkFinishLoadedObjectFlag flag);
        DistanceMatchModifier(const DistanceMatchModifier& other);

        const ::hkClass* getClassType() const override;
        /// @brief The clip starts over from its first frame.
        void             activate(const ::hkbContext& context) override;
        /// @brief Steps the clip time by the distance (or the clock when no distance applies) and writes its fraction.
        /// @note  Overridden even apart from that: hkbNode's own marks the node update-free, and the graph then skips
        ///        copying bound variables (`distance`) into it.
        void             update(const ::hkbContext& context, ::hkReal timestep) override;
        bool             isValid(::hkStringPtr& errorString) const override;
        ::hkbNode*       cloneNode(::hkbBehaviorGraph& rootBehavior) const override;
    };
    HKY_LAYOUT_CHECK(DistanceMatchModifier, hky::layout::CBDistanceMatchModifier);

}  // namespace CB::feature::controller
