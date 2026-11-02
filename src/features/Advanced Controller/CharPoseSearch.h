#pragma once

#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Context/hkbContext.h>
#include <Behavior/Generator/hkbGenerator.h>
#include <Common/Base/Container/String/hkString.h>

#include <cstddef>

/// @file
/// @brief CBPoseSearchGenerator, CB's own Havok generator (motion matching, stage A): it plays the one child
///        `m_selectedIndex` names, with 32-bit indices, keeps every other child active (getChildren), and dead-blends
///        each switch (generate).
/// @note  Why not the stock selector: hkbManualSelectorGenerator's indices are int8, and it clamps the selection to
///        (int8)(children - 1); with the add-on's 252 clips that is -5, so every selection read past its children
///        (the first in-game run crashed there). The layout is schema/generators/CBPoseSearchGenerator.yaml's: the
///        platform registers the class from that descriptor (platform/classes) and checks the sizes agree.

namespace CB::feature::controller::motion {

    /// @brief CBPoseSearchChild: one row of the generator, the clip and how the search treats it.
    /// @note  Declared POD to hkArray: the elements live in the loaded file (a clone copies the row bytes, and its
    ///        strings stay the file's).
    struct PoseSearchChild {
        HK_DECLARE_POD_TYPE();

        CB::havok::hkbGenerator* m_generator;            ///< +0x00
        CB::havok::hkStringPtr   m_database;             ///< +0x08
        CB::havok::hkReal        m_rangeStart;           ///< +0x10
        CB::havok::hkReal        m_rangeEnd;             ///< +0x14
        CB::havok::hkBool        m_disableReselection;   ///< +0x18
        CB::havok::hkBool        m_looping;              ///< +0x19
    };
    static_assert(sizeof(PoseSearchChild) == 32, "PoseSearchChild size (schema: 32)");

    /// @brief CBPoseSearchDatabase: a dance card, named by its children, with Unreal's per-database cost biases
    ///        (condition expressions, evaluated by CB's controller) and its tags.
    /// @note  Declared POD to hkArray: the elements live in the loaded file, so an array never constructs or destroys
    ///        them (a block-copied node's destructor must not tear down the template's data).
    struct PoseSearchDatabase {
        HK_DECLARE_POD_TYPE();

        CB::havok::hkStringPtr          m_name;                     ///< +0x00
        CB::havok::hkStringPtr          m_baseCostBias;             ///< +0x08
        CB::havok::hkStringPtr          m_continuingPoseCostBias;   ///< +0x10
        CB::havok::hkStringPtr          m_loopingCostBias;          ///< +0x18
        CB::havok::hkArray<CB::havok::hkStringPtr> m_tags;          ///< +0x20
        CB::havok::hkStringPtr          m_standingCandidate;        ///< +0x30
        CB::havok::hkStringPtr          m_movingCandidate;          ///< +0x38
    };
    static_assert(sizeof(PoseSearchDatabase) == 64, "PoseSearchDatabase size (schema: 64)");

    /// @brief CBPoseSearchChooserRow: a condition expression and the databases it selects.
    struct PoseSearchChooserRow {
        HK_DECLARE_POD_TYPE();

        CB::havok::hkStringPtr                     m_when;        ///< +0x00
        CB::havok::hkArray<CB::havok::hkStringPtr> m_databases;   ///< +0x08
    };
    static_assert(sizeof(PoseSearchChooserRow) == 24, "PoseSearchChooserRow size (schema: 24)");

    /// @brief CBPoseSearchChooser: the generator's chooser table, a data object CB's controller evaluates.
    /// @note  Only ever loaded (the finish constructor): CB reads it through the merged graph, the live object only has
    ///        to exist for the loader.
    class PoseSearchChooser : public CB::havok::hkReferencedObject {
    public:
        CB::havok::hkArray<PoseSearchChooserRow>   m_rows;       ///< +0x10
        CB::havok::hkArray<CB::havok::hkStringPtr> m_fallback;   ///< +0x20

        explicit PoseSearchChooser(CB::havok::hkFinishLoadedObjectFlag flag);
        ~PoseSearchChooser() override = default;
        const void* getClassType() const override;
    };
    static_assert(sizeof(PoseSearchChooser) == 48, "PoseSearchChooser size (schema: 48)");

    class PoseSearchGenerator : public CB::havok::hkbGenerator {
    public:
        /// @note Each row's child holds a reference (raw pointers: the constructors and the destructor count them by hand).
        /// @warning No member may own memory: Skyrim instances a character's graph by block-copying it (only clip
        ///          controls and a few reference counts are fixed up), so a node-owned pointer is shared by every copy
        ///          and freed by the first one destroyed. While the dead blend kept its owned state here, random actors
        ///          vanished (suspected cause, not proven); it now lives in a table keyed by character (generate).
        CB::havok::hkArray<PoseSearchChild>          m_children;       ///< +0x48
        CB::havok::hkInt32                           m_selectedIndex;  ///< +0x58, bound to the add-on's MM_Clip
        CB::havok::hkInt32                           m_currentIndex;   ///< +0x5c, the child playing, clamped into range
        /// @note The databases, the rows' other columns and the chooser are read by CB's controller from the merged graph,
        ///       never from a live node: a clone leaves the databases and chooser empty, and nothing here owns memory.
        CB::havok::hkArray<PoseSearchDatabase>       m_databases;      ///< +0x60
        PoseSearchChooser*                           m_chooser;        ///< +0x70, the loaded file's (never referenced or freed here)

        /// @brief The finish constructor: the vtable on loaded bytes; serialized members stay as loaded.
        explicit PoseSearchGenerator(CB::havok::hkFinishLoadedObjectFlag flag);
        /// @brief The cloneNode copy: shares the children (each gains a reference), the current index restarted.
        PoseSearchGenerator(const PoseSearchGenerator& other);
        ~PoseSearchGenerator() override;

        const void*          getClassType() const override;
        void                 activate(const CB::havok::hkbContext& context) override;
        /// @brief Follows the selected index; a switch to another child asks the root graph to recompute its active nodes.
        void                 update(const CB::havok::hkbContext& context, CB::havok::hkReal timestep) override;
        int                  getMaxNumChildren(GetChildrenFlags flags) override;
        void                 getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo) override;
        bool                 isValid(CB::havok::hkStringPtr& errorString) const override;
        CB::havok::hkbNode*  cloneNode(CB::havok::hkbBehaviorGraph& rootBehavior) const override;
        void                 generate(const CB::havok::hkbContext& context, const CB::havok::hkbGeneratorOutput** activeChildrenOutput,
                                      CB::havok::hkbGeneratorOutput& output, CB::havok::hkReal timeOffset) override;
        bool                 canRecycleOutput() const override;
        void                 updateSync(const CB::havok::hkbContext& context, CB::havok::hkbNodeInfo& info) override;

    private:
        /// @brief m_selectedIndex clamped into [0, last child].
        int computeCurrentIndex() const;
        /// @brief Cross the output pose from the switched-from child's extrapolated motion into the live child's.
        void deadBlend(const CB::havok::hkbContext& context, CB::havok::hkbGeneratorOutput& output);
    };
    static_assert(sizeof(PoseSearchGenerator) == 0x78, "PoseSearchGenerator size (schema: 120)");

    /// @brief Seconds left in the newest dead blend (0 when none runs), for the HUD.
    /// @note  One value for every instance: motion matching plays the node for the player alone.
    float BlendRemaining();

}  // namespace CB::feature::controller::motion
