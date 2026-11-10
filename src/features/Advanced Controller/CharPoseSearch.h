#pragma once

#include <Common/Base/hkBase.h>
#include <Behavior/Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Behavior/Context/hkbContext.h>
#include <Behavior/Behavior/Generator/hkbGenerator.h>
#include <Common/Base/Container/String/hkString.h>

#include "CharPoseSearchSchema.h"

#include <locomotion/PoseSearch.h>

#include <cstddef>
#include <optional>

/// @file
/// @brief CBPoseSearchGenerator, CB's own Havok generator: motion matching as a node. Each update it searches its
///        database for the frame the character's query asks for (motion::RunMatcher); it plays the one child
///        `m_selectedIndex` names, with 32-bit indices, keeps every other child active (getChildren), and dead-blends
///        each switch (generate). With no query or no database it just follows `m_selectedIndex`.
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

        ::hkbGenerator* m_generator;            ///< +0x00
        const char*     m_database;             ///< +0x08, the file's string (never assigned or freed here)
        ::hkReal        m_rangeStart;           ///< +0x10
        ::hkReal        m_rangeEnd;             ///< +0x14
        ::hkBool        m_disableReselection;   ///< +0x18
        ::hkBool        m_looping;              ///< +0x19
    };
    static_assert(sizeof(PoseSearchChild) == 32, "PoseSearchChild size (schema: 32)");

    /// @brief CBPoseSearchDatabase: a dance card, named by its children, with Unreal's per-database cost biases
    ///        (condition expressions) and its tags.
    /// @note  Declared POD to hkArray: the elements live in the loaded file, so an array never constructs or destroys
    ///        them (a clone shares them through m_children's copy, and its destructor must not tear down the template's data).
    struct PoseSearchDatabase {
        HK_DECLARE_POD_TYPE();

        ::hkStringPtr          m_name;                     ///< +0x00
        ::hkStringPtr          m_baseCostBias;             ///< +0x08
        ::hkStringPtr          m_continuingPoseCostBias;   ///< +0x10
        ::hkStringPtr          m_loopingCostBias;          ///< +0x18
        ::hkArray<::hkStringPtr> m_tags;          ///< +0x20
        ::hkStringPtr          m_standingCandidate;        ///< +0x30
        ::hkStringPtr          m_movingCandidate;          ///< +0x38
        ::hkReferencedObject*  m_schema;                   ///< +0x40 CBPoseSearchSchema; none = the classic layout
    };
    static_assert(sizeof(PoseSearchDatabase) == 72, "PoseSearchDatabase size (schema: 72)");

    /// @brief CBPoseSearchChooserRow: a condition expression and the databases it selects.
    struct PoseSearchChooserRow {
        HK_DECLARE_POD_TYPE();

        ::hkStringPtr                     m_when;        ///< +0x00
        ::hkArray<::hkStringPtr> m_databases;   ///< +0x08
    };
    static_assert(sizeof(PoseSearchChooserRow) == 24, "PoseSearchChooserRow size (schema: 24)");

    /// @brief CBPoseSearchChooser: the generator's chooser table, which it evaluates through its database.
    /// @note  Only ever loaded (the finish constructor).
    class PoseSearchChooser : public ::hkReferencedObject {
    public:
        ::hkArray<PoseSearchChooserRow>   m_rows;       ///< +0x10
        ::hkArray<::hkStringPtr> m_fallback;   ///< +0x20

        explicit PoseSearchChooser(::hkFinishLoadedObjectFlag flag);
        ~PoseSearchChooser() override = default;
        const ::hkClass* getClassType() const override;
    };
    static_assert(sizeof(PoseSearchChooser) == 48, "PoseSearchChooser size (schema: 48)");

    /// @brief What a node loaded, for its database and its query.
    struct NodeDescription {
        CB::engine::locomotion::DatabaseSpec  spec;
        anim::Rig                             rig;        ///< the character's animation skeleton, as its graph runs
        std::vector<std::vector<BoundInput>>  bound;      ///< per spec.schemas entry: the query values its features bind
        bool                                  speedBound = false;   ///< m_speed is bound: the node trims its own rate
    };

    class PoseSearchGenerator : public ::hkbGenerator {
    public:
        /// @note Each row's child holds a reference (raw pointers: the constructors and the destructor count them by hand).
        /// @note  Each character runs its own clone (cloneNode). Per-character state lives in a table keyed by character
        ///        (generate) rather than here: while the dead blend kept owned state in the node, random actors vanished
        ///        (suspected cause, not proven). A pointer the copy constructor copies is shared by every clone.
        ::hkArray<PoseSearchChild>          m_children;       ///< +0x48
        ::hkInt32                           m_selectedIndex;  ///< +0x58, bound to the add-on's MM_Clip
        ::hkInt32                           m_currentIndex;   ///< +0x5c, the child playing, clamped into range
        /// @note A clone leaves the databases and chooser empty, and nothing here owns memory: Describe reads them, and
        ///       the rows' other columns, from the template the clone came from.
        ::hkArray<PoseSearchDatabase>       m_databases;      ///< +0x60
        PoseSearchChooser*                           m_chooser;        ///< +0x70, the loaded file's (never referenced or freed here)
        /// @brief Bound to the character's speed (game units/s): the playback rate is this over the playing frame's own clip
        ///        speed. Unbound, the rate comes from MM_Rate.
        ::hkReal                            m_speed;          ///< +0x78

        /// @brief The finish constructor: the vtable on loaded bytes; serialized members stay as loaded.
        explicit PoseSearchGenerator(::hkFinishLoadedObjectFlag flag);
        /// @brief The cloneNode copy: shares the children (each gains a reference), the current index restarted.
        PoseSearchGenerator(const PoseSearchGenerator& other);
        ~PoseSearchGenerator() override;

        const ::hkClass*          getClassType() const override;
        void                 activate(const ::hkbContext& context) override;
        /// @brief Follows the selected index; a switch to another child asks the root graph to recompute its active nodes.
        void                 update(const ::hkbContext& context, ::hkReal timestep) override;
        int                  getMaxNumChildren(GetChildrenFlags flags) override;
        void                 getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo) override;
        bool                 isValid(::hkStringPtr& errorString) const override;
        ::hkbNode*  cloneNode(::hkbBehaviorGraph& rootBehavior) const override;
        void                 generate(const ::hkbContext& context, const ::hkbGeneratorOutput** activeChildrenOutput,
                                      ::hkbGeneratorOutput& output, ::hkReal timeOffset) override;
        bool                 canRecycleOutput() const override;
        void                 updateSync(const ::hkbContext& context, ::hkbNodeInfo& info) override;

        /// @brief Play child `index` at `fraction` of its length, on this character's clone of it.
        /// @note  For a node with no bindings. Two nodes alive through a state's transition would fight over MM_Clip and MM_Time,
        ///        which hold one child of one database. Its effect lands when a binding's would: the children updated before this.
        /// @brief The next output blends: the matcher moved the playing child to another time.
        void BlendNextJump();
        void PlayUnbound(const ::hkbContext& context, int index, ::hkReal fraction);

        /// @return what this node loaded, for its database; nothing when its template cannot be found
        std::optional<NodeDescription> Describe(const ::hkbContext& context) const;

    private:
        /// @brief m_selectedIndex clamped into [0, last child].
        int computeCurrentIndex() const;
        /// @brief Cross the output pose from the switched-from child's extrapolated motion into the live child's.
        void deadBlend(const ::hkbContext& context, ::hkbGeneratorOutput& output);
    };
    static_assert(sizeof(PoseSearchGenerator) == 0x80, "PoseSearchGenerator size (schema: 128)");

    /// @brief Seconds left in the newest dead blend (0 when none runs), for the HUD.
    /// @note  One value for every instance: motion matching plays the node for the player alone.
    float BlendRemaining();

}  // namespace CB::feature::controller::motion
