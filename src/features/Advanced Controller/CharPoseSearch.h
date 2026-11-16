#pragma once

#include <hky/layout/CBPoseSearchChild.h>
#include <hky/layout/CBPoseSearchChooser.h>
#include <hky/layout/CBPoseSearchChooserRow.h>
#include <hky/layout/CBPoseSearchDatabase.h>
#include <hky/layout/CBPoseSearchGenerator.h>
#include <Common/Base/hkBase.h>
#include <Behavior/Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Behavior/Condition/hkbCondition.h>
#include <Behavior/Behavior/Context/hkbContext.h>
#include <Behavior/Behavior/Generator/hkbGenerator.h>
#include <Common/Base/Container/String/hkString.h>

#include "CharPoseSearchSchema.h"

#include <hky/locomotion/PoseSearch.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

/// @file
/// @brief CBPoseSearchGenerator, CB's own Havok generator: motion matching as a node, laid out like a blend space. Each
///        update it searches its database for the frame the character's query asks for (motion::RunMatcher), plays it on
///        its child's clone itself (PlayPick), keeps every other child active (getChildren), dead-blends each switch
///        (generate) and publishes the pick (m_selectedIndex, m_selectedTime, ...). With no query or no database the
///        pick plays on.
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
        ::hkReal        m_entryStart;           ///< +0x10
        ::hkReal        m_entryEnd;             ///< +0x14
        ::hkBool        m_disableReselection;   ///< +0x18
    };
    HKY_LAYOUT_CHECK(PoseSearchChild, hky::layout::CBPoseSearchChild);

    /// @brief CBPoseSearchDatabase: a dance card, named by its children, with its profile, Unreal's per-database cost
    ///        biases (condition expressions) and its tags.
    /// @note  Declared POD to hkArray: the elements live in the loaded file, so an array never constructs or destroys
    ///        them (a clone shares them through m_children's copy, and its destructor must not tear down the template's data).
    struct PoseSearchDatabase {
        HK_DECLARE_POD_TYPE();

        ::hkStringPtr          m_name;                     ///< +0x00
        ::hkStringPtr          m_profile;                  ///< +0x08
        ::hkStringPtr          m_blendProfile;             ///< +0x10
        ::hkReal               m_profileBlend;             ///< +0x18
        ::hkStringPtr          m_baseCostBias;             ///< +0x20
        ::hkStringPtr          m_continuingPoseCostBias;   ///< +0x28
        ::hkStringPtr          m_loopingCostBias;          ///< +0x30
        ::hkArray<::hkStringPtr> m_tags;                   ///< +0x38
        ::hkStringPtr          m_standingCandidate;        ///< +0x48
        ::hkStringPtr          m_movingCandidate;          ///< +0x50
    };
    HKY_LAYOUT_CHECK(PoseSearchDatabase, hky::layout::CBPoseSearchDatabase);

    /// @brief CBPoseSearchChooserRow: a condition and the databases it selects while it holds.
    struct PoseSearchChooserRow {
        HK_DECLARE_POD_TYPE();

        ::hkbCondition*          m_condition;   ///< +0x00, the loaded file's; null always holds
        ::hkArray<::hkStringPtr> m_databases;   ///< +0x08
    };
    HKY_LAYOUT_CHECK(PoseSearchChooserRow, hky::layout::CBPoseSearchChooserRow);

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
    HKY_LAYOUT_CHECK(PoseSearchChooser, hky::layout::CBPoseSearchChooser);

    /// @brief A trajectory sample's query bound to a variable: channel `channel`'s sample `sample`, its pose or its motion.
    struct SampleBind {
        int         channel = 0;
        int         sample  = 0;
        bool        pose    = true;
        std::string variable;   ///< byte-exact
    };

    /// @brief What a node's channels, profiles and databases bind, by variable name, and where its outputs go.
    /// @note  Read by name for each character: the channel and profile objects serve every clone of the node.
    struct NodeBinds {
        std::vector<SampleBind>               samples;
        std::vector<std::vector<std::string>> weights;   ///< [profile][weight]: the variable its value comes from, or empty
        std::vector<std::string>              blends;    ///< [database]: the variable its profileBlend comes from, or empty
        std::string selectedIndex, selectedTime, selectedCost, blendRemaining;   ///< output-bound variables, or empty
    };

    /// @brief What a node loaded, for its database, its query and its chooser, as one character's graph runs it.
    struct NodeDescription {
        hky::locomotion::DatabaseSpec  spec;      ///< bone sets named through the character's properties and skeleton
        anim::Rig                      rig;       ///< the character's animation skeleton, as its graph runs
        NodeBinds                      binds;
        const PoseSearchChooser*       chooser = nullptr;   ///< the character's bound one, else the node's inline one
        bool                           speedBound = false;  ///< m_speed is bound: the node trims its own rate
        bool                           traversal  = false;  ///< a database is tagged "Traversal"
    };

    class PoseSearchGenerator : public ::hkbGenerator {
    public:
        /// @note Each row's child holds a reference (raw pointers: the constructors and the destructor count them by hand).
        /// @note  Each character runs its own clone (cloneNode). Per-character state lives in a table keyed by character
        ///        (generate) rather than here: while the dead blend kept owned state in the node, random actors vanished
        ///        (suspected cause, not proven). A pointer the copy constructor copies is shared by every clone.
        ::hkArray<PoseSearchChild>          m_children;       ///< +0x48
        ::hkReferencedObject*               m_rootBone;       ///< +0x58 hkbBoneIndexArray, the loaded file's
        /// @note The arrays and the chooser are the loaded file's: a clone shares them (never referenced or freed here).
        ::hkArray<PoseSearchChannel*>       m_channels;       ///< +0x60
        ::hkArray<PoseSearchProfile*>       m_profiles;       ///< +0x70
        ::hkArray<PoseSearchDatabase>       m_databases;      ///< +0x80
        PoseSearchChooser*                  m_chooser;        ///< +0x90
        ::hkBool                            m_haveQuery;      ///< +0x98
        ::hkInt32                           m_ignoreSurrounding;   ///< +0x9c, frames
        ::hkVector4                         m_desired;        ///< +0xa0, world space, game units/s
        /// @brief Bound to the character's speed (game units/s): the playback rate is this over the playing frame's own clip
        ///        speed. Unbound, the rate comes from MM_PlayRate.
        ::hkReal                            m_speed;          ///< +0xb0
        ::hkReal                            m_ledgeDistance;  ///< +0xb4, game units; <= 0 = none
        ::hkReal                            m_searchInterval; ///< +0xb8
        ::hkReal                            m_minDwell;       ///< +0xbc
        ::hkReal                            m_forcedDwell;    ///< +0xc0
        ::hkReal                            m_switchFloor;    ///< +0xc4
        ::hkReal                            m_switchMargin;   ///< +0xc8
        ::hkReal                            m_reselectTime;   ///< +0xcc
        ::hkReal                            m_blendTimeGround;   ///< +0xd0
        ::hkReal                            m_blendTimeLand;     ///< +0xd4
        ::hkReal                            m_blendTimeJump;     ///< +0xd8
        ::hkReal                            m_blendTimeAir;      ///< +0xdc
        ::hkReal                            m_blendDecay;        ///< +0xe0
        ::hkInt32                           m_selectedIndex;  ///< +0xe4, output: the child playing
        ::hkInt32                           m_currentIndex;   ///< +0xe8, the child playing, clamped into range
        ::hkReal                            m_selectedTime;   ///< +0xec, output: seconds into it
        ::hkReal                            m_selectedCost;   ///< +0xf0, output
        ::hkReal                            m_blendRemaining; ///< +0xf4, output

        /// @brief The finish constructor: the vtable on loaded bytes; serialized members stay as loaded.
        explicit PoseSearchGenerator(::hkFinishLoadedObjectFlag flag);
        /// @brief The cloneNode copy: shares the children (each gains a reference), the current index restarted.
        PoseSearchGenerator(const PoseSearchGenerator& other);
        ~PoseSearchGenerator() override;

        const ::hkClass*          getClassType() const override;
        void                 activate(const ::hkbContext& context) override;
        /// @note Overridden only to trace it (Tracy): hkbNode's is empty, and is still called.
        void                 deactivate(const ::hkbContext& context) override;
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

        /// @brief The matcher switched: the switch the output shows next blends over `blendTime` seconds (SituationBlendTime),
        ///        and with `jump` (the playing child moved to another time) it blends though the child stays.
        void BlendNext(float blendTime, bool jump);
        /// @brief Play child `index` at `fraction` of its cropped length, on this character's clone of it, from this frame's
        ///        output on.
        /// @note  The children update before their parent, so this sets the clone's local time too: its generate, after
        ///        this update, samples the pick at once. Its previous fraction follows, so its next update steps nothing
        ///        across the jump (no triggers, no extracted motion).
        void PlayPick(const ::hkbContext& context, int index, ::hkReal fraction);

        /// @return what this node loaded, for its database, as `context`'s character runs it; nothing when its template
        ///         cannot be found
        std::optional<NodeDescription> Describe(const ::hkbContext& context) const;
        /// @brief Write the output members (m_selectedIndex, ...) and each output-bound variable `binds` names.
        void Publish(const ::hkbContext& context, const NodeBinds& binds, int clip, float time, float cost);

    private:
        /// @brief m_selectedIndex clamped into [0, last child].
        int computeCurrentIndex() const;
        /// @brief Cross the output pose from the switched-from child's extrapolated motion into the live child's.
        void deadBlend(const ::hkbContext& context, ::hkbGeneratorOutput& output);
    };
    HKY_LAYOUT_CHECK(PoseSearchGenerator, hky::layout::CBPoseSearchGenerator);

    /// @brief GASP's Get_MMBlendTime: the seconds a switch made now blends over, from `node`'s blend times (bound to
    ///        MM_BlendTimeGround, MM_BlendTimeLand just landed, MM_BlendTimeJump airborne and rising past 70 units/s,
    ///        MM_BlendTimeAir otherwise in the air), each clamped to [0, 1].
    /// @param vz the character's vertical speed, game units/s
    float SituationBlendTime(const PoseSearchGenerator& node, bool airborne, bool justLanded, float vz);

    /// @brief Seconds left in the newest dead blend (0 when none runs), for the HUD.
    /// @note  One value for every instance: motion matching plays the node for the player alone.
    float BlendRemaining();

}  // namespace CB::feature::controller::motion
