#pragma once

#include <hky/layout/CBDynamicClipGenerator.h>
#include <hky/layout/CBDynamicClipRow.h>
#include <Common/Base/hkBase.h>
#include <Behavior/Behavior/Condition/hkbCondition.h>
#include <Behavior/Behavior/Generator/hkbGenerator.h>

#include <cstddef>

/// @file
/// @brief CBDynamicClipGenerator: plays one clip of a folder, picked by each clip's selection data. The layout is
///        schema/generators/CBDynamicClipGenerator.yaml's; the rows are filled by the compile (compile/DynamicClips.h).
/// @note  The rows' clips are real hkbClipGenerators, so the character's clip loading binds them as any other; only the
///        playing one is active, so only it streams its animation in.

namespace CB::feature::dynamic {

    /// @brief CBDynamicClipRow.
    struct DynamicClipRow {
        ::hkbGenerator* m_clip;        ///< +0x00, an hkbClipGenerator
        ::hkbCondition* m_condition;   ///< +0x08, null: always holds
        ::hkInt32       m_priority;    ///< +0x10
        ::hkReal        m_weight;      ///< +0x14
        ::hkInt8        m_flags;       ///< +0x18, DynamicClipFlags
    };
    HKY_LAYOUT_CHECK(DynamicClipRow, hky::layout::CBDynamicClipRow);

    class DynamicClipGenerator : public ::hkbGenerator {
    public:
        ::hkReferencedObject*     m_triggers;                     ///< +0x48, hkbClipTriggerArray
        ::hkReal                  m_cropStartAmountLocalTime;     ///< +0x50
        ::hkReal                  m_cropEndAmountLocalTime;       ///< +0x54
        ::hkReal                  m_startTime;                    ///< +0x58
        ::hkReal                  m_playbackSpeed;                ///< +0x5c
        ::hkReal                  m_enforcedDuration;             ///< +0x60
        ::hkReal                  m_userControlledTimeFraction;   ///< +0x64
        ::hkInt8                  m_mode;                         ///< +0x68
        ::hkInt8                  m_flags;                        ///< +0x69
        ::hkStringPtr             m_folder;                       ///< +0x70
        ::hkStringPtr             m_fallback;                     ///< +0x78
        ::hkReal                  m_blendDuration;                ///< +0x80
        /// @note A clone copies the rows and takes a reference on each row's clip and condition; the objects stay the loaded
        ///       file's. A condition is never cloned, so it keeps no per-character state.
        ::hkArray<DynamicClipRow> m_rows;                         ///< +0x88
        ::hkbGenerator*           m_fallbackClip;                 ///< +0x98
        /// @brief +0xa0, runtime: the row playing; -1 the fallback clip; -2 nothing.
        ::hkInt32                 m_current;
        ::hkUint32                m_activations;                  ///< +0xa4, this clone's activations, a seed input
        ::hkUint32                m_loops;                        ///< +0xa8, loops since the activation, a seed input
        ::hkReal                  m_lastLocalTime;                ///< +0xac, the playing clip's local time last update

        explicit DynamicClipGenerator(::hkFinishLoadedObjectFlag flag);
        /// @brief The cloneNode copy: shares the rows, the pick restarted.
        DynamicClipGenerator(const DynamicClipGenerator& other);
        ~DynamicClipGenerator() override;

        const ::hkClass* getClassType() const override;
        /// @brief The pick: evaluate every row, take the top priority, draw by weight from (actor, activation).
        void             activate(const ::hkbContext& context) override;
        /// @brief Interruptible rows re-evaluate every update; a loop re-picks per RedrawOnLoop / KeepRandomOnLoop.
        void             update(const ::hkbContext& context, ::hkReal timestep) override;
        int              getMaxNumChildren(GetChildrenFlags flags) override;
        void             getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo) override;
        bool             isValid(::hkStringPtr& errorString) const override;
        ::hkbNode*       cloneNode(::hkbBehaviorGraph& rootBehavior) const override;
        /// @brief The playing clip's output, dead-blended over m_blendDuration after a switch.
        void             generate(const ::hkbContext& context, const ::hkbGeneratorOutput** activeChildrenOutput,
                                  ::hkbGeneratorOutput& output, ::hkReal timeOffset) override;

    private:
        ::hkbGenerator* currentChild() const;
        /// @return the pick for this activation and loop count (-1: the fallback clip; -2: nothing to play).
        int  pick(const ::hkbContext& context);
        void switchTo(const ::hkbContext& context, int row);
    };
    HKY_LAYOUT_CHECK(DynamicClipGenerator, hky::layout::CBDynamicClipGenerator);
    static_assert(offsetof(DynamicClipGenerator, m_folder) == 0x70);
    static_assert(offsetof(DynamicClipGenerator, m_rows) == 0x88);
    static_assert(offsetof(DynamicClipGenerator, m_current) == 0xa0);

}  // namespace CB::feature::dynamic
