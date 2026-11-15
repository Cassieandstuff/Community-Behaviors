#include "PCH.h"

#include "CharPoseSearch.h"

#include "CharGround.h"
#include "CharMotionMatching.h"
#include "CharSkeletonWatch.h"

#include <hky/locomotion/DeadBlend.h>
#include <platform/classes/ClassRegistry.h>
#include <platform/profile/Profile.h>

#include <Common/Base/hkBase.h>
#include <Common/Base/Reflection/hkClass.h>
#include <Behavior/Behavior/BehaviorGraph/hkbNodeInfo.h>
#include <Behavior/Behavior/Character/hkbCharacter.h>
#include <Behavior/Behavior/Character/hkbCharacterSetup.h>
#include <Behavior/Behavior/Variables/hkbVariableBindingSet.h>
#include <Animation/Animation/Rig/hkaSkeleton.h>
#include <Behavior/Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Behavior/Generator/Clip/hkbClipGenerator.h>
#include <Behavior/Behavior/Generator/hkbGeneratorOutput.h>
#include <Behavior/Behavior/Generator/hkbGeneratorOutputUtils.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <new>
#include <numbers>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/// @file
/// @brief CBPoseSearchGenerator's behavior, after the SDK's hkbManualSelectorGenerator line for line, with int32 indices.
/// @note  Registered whatever the flags: a behavior holding this class loads with the add-on whether or not motion
///        matching runs, and an unregistered class name loads with a null vtable.

namespace CB::feature::controller::motion {

    namespace detail {

        namespace loco = hky::locomotion;
        static_assert(sizeof(loco::Bone) == sizeof(::hkQsTransform), "Bone mirrors hkQsTransform");

        /// @brief The blend's tuning, read once from settings.ini [Character].
        inline const loco::DeadBlendTuning& Tuning()
        {
            static const loco::DeadBlendTuning s{ std::clamp(SettingFloat("fMMBlendTime", 0.2f), 0.0f, 1.0f),
                                                  std::clamp(SettingFloat("fMMBlendDecay", 0.1f), 0.01f, 1.0f) };
            return s;
        }

        inline std::atomic<float> g_blendRemaining{ 0.0f };

        constexpr int kMaxSlotLogs = 64;   ///< "who runs the node" lines per session

        /// @brief The guards' one-shot logs (a broken pose would otherwise log every frame).
        inline std::atomic<bool> g_inputLogged{ false }, g_outputLogged{ false };

    }  // namespace detail

    using namespace detail;
    using loco::DeadBlend;

    /// @brief The dead blend's state per clone (one character's node), never in the node: while the node owned it, random actors
    ///        vanished (suspected cause, not proven).
    struct BlendSlot {
        DeadBlend                             blend;
        std::vector<loco::Bone>               pose;   ///< the output pose, copied out of and back into Havok's
        /// @brief The matcher moved the playing clip to another time (jumpPending, this update); the output that shows it
        ///        blends (jumpNow, the next generate).
        /// @note  The graph updates children before their parent, so the clip shows the new time a frame after the matcher
        ///        wrote it: a blend started in the same frame's generate was already part-way in when the jump showed.
        bool                                  jumpPending = false, jumpNow = false;
        bool                                  activated = false;   ///< the node (re)activated: its last outputs are not the pose shown since
        std::chrono::steady_clock::time_point used{};
        /// @brief The player's arm bones as the last traced output had them, live (the child's) and shown (blended), for
        ///        Tracy's frame-to-frame rotation plots; written only while the profiler runs.
        struct ArmWatch {
            const void* skeleton = nullptr;   ///< the skeleton `bone` was resolved on
            int         bone[2]  = { -1, -1 };
            loco::Quat  live[2]{}, shown[2]{};
            bool        have = false;
        }                                     arm;
    };
    struct BlendTable {
        std::mutex                                     lock;
        std::unordered_map<const void*, BlendSlot>     slots;
        std::uint32_t                                  calls = 0;
        std::unordered_set<const void*>                seen;   ///< every character that ever had a slot (the log's)
        int                                            seenLogs = 0;
    };
    BlendTable& Blends()
    {
        static BlendTable t;
        return t;
    }
    using Clock                       = std::chrono::steady_clock;
    /// @brief Seconds unused before a slot is dropped (its node gone); a node that comes back starts over through activate.
    constexpr double        kDropAfter  = 300.0;
    constexpr std::uint32_t kPruneEvery = 600;    ///< blends between prunes


    namespace {


        constexpr const char* kClassName = "CBPoseSearchGenerator";

        void Finish(void* object)
        {
            if (object) new (object) PoseSearchGenerator(::hkFinishLoadedObjectFlag{});
        }
        void Cleanup(void* object) { static_cast<PoseSearchGenerator*>(object)->~PoseSearchGenerator(); }
        /// @note As the exe's own type infos take it: a finish constructor run on a scratch buffer, its vtable read off.
        const void* Vtable()
        {
            alignas(PoseSearchGenerator) unsigned char scratch[sizeof(PoseSearchGenerator)] = {};
            Finish(scratch);
            return *reinterpret_cast<const void* const*>(scratch);
        }

        [[maybe_unused]] const bool s_declared = platform::classes::Declare(
            { kClassName, sizeof(PoseSearchGenerator), &Finish, &Cleanup, &Vtable });

        constexpr const char* kChooserClassName = "CBPoseSearchChooser";

        void FinishChooser(void* object)
        {
            if (object) new (object) PoseSearchChooser(::hkFinishLoadedObjectFlag{});
        }
        void CleanupChooser(void* object) { static_cast<PoseSearchChooser*>(object)->~PoseSearchChooser(); }
        const void* ChooserVtable()
        {
            alignas(PoseSearchChooser) unsigned char scratch[sizeof(PoseSearchChooser)] = {};
            FinishChooser(scratch);
            return *reinterpret_cast<const void* const*>(scratch);
        }

        [[maybe_unused]] const bool s_chooserDeclared = platform::classes::Declare(
            { kChooserClassName, sizeof(PoseSearchChooser), &FinishChooser, &CleanupChooser, &ChooserVtable });

    }  // namespace

    /// @note Only the runtime members reset: the children and the selected index are the loaded bytes.
    PoseSearchChooser::PoseSearchChooser(::hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag),
        m_rows(flag),
        m_fallback(flag)
    {
    }

    const ::hkClass* PoseSearchChooser::getClassType() const
    {
        static const auto* const cls = static_cast<const ::hkClass*>(platform::classes::ClassOf(kChooserClassName));
        return cls;
    }

    PoseSearchGenerator::PoseSearchGenerator(::hkFinishLoadedObjectFlag flag)
    :   hkbGenerator(flag),
        m_children(flag),
        m_databases(flag)
    {
        m_currentIndex = 0;
    }

    PoseSearchGenerator::PoseSearchGenerator(const PoseSearchGenerator& other)
    :   hkbGenerator(other),
        m_selectedIndex(other.m_selectedIndex),
        m_currentIndex(0),
        m_chooser(nullptr),
        m_speed(other.m_speed)
    {
        m_children = other.m_children;
        for (int i = 0; i < m_children.getSize(); i++)
            if (m_children[i].m_generator) m_children[i].m_generator->addReference();
    }

    PoseSearchGenerator::~PoseSearchGenerator()
    {
        for (int i = 0; i < m_children.getSize(); i++)
            if (m_children[i].m_generator) m_children[i].m_generator->removeReference();
    }

    const ::hkClass* PoseSearchGenerator::getClassType() const
    {
        static const auto* const cls = static_cast<const ::hkClass*>(platform::classes::ClassOf(kClassName));
        return cls;
    }

    namespace {

        /// @brief Every node's activations, deactivations and reactivation resets, for Tracy's counters.
        std::atomic<std::uint64_t> g_activates{ 0 }, g_deactivates{ 0 }, g_blendResets{ 0 };

        const char* Whose(const ::hkbContext& context)
        {
            const std::optional<bool> player = IsPlayerCharacter(context.m_character);
            return !player ? "unknown" : *player ? "player" : "npc";
        }

        const char* NameOf(const ::hkbNode& node) { return node.m_name.cString() ? node.m_name.cString() : ""; }

        double Seconds() { return std::chrono::duration<double>(Clock::now().time_since_epoch()).count(); }

        /// @brief The arm bones traced, by their exact names in the skeleton.
        constexpr const char* kArmBones[2] = { "NPC R UpperArm [RUar]", "NPC R Forearm [RLar]" };
        /// @note Tracy keys a plot by its name's address: one literal per plot.
        constexpr const char* kArmLiveDot[2]   = { "MM RUpperArm live dot", "MM RForearm live dot" };
        constexpr const char* kArmLiveDeg[2]   = { "MM RUpperArm live deg", "MM RForearm live deg" };
        constexpr const char* kArmShownDot[2]  = { "MM RUpperArm shown dot", "MM RForearm shown dot" };
        constexpr const char* kArmShownDeg[2]  = { "MM RUpperArm shown deg", "MM RForearm shown deg" };

        /// @return the 4D dot of `a` and `b`, normalized; negative = the other hemisphere (q vs -q)
        float QuatDot(const loco::Quat& a, const loco::Quat& b)
        {
            const float n = std::sqrt((a.x * a.x + a.y * a.y + a.z * a.z + a.w * a.w) * (b.x * b.x + b.y * b.y + b.z * b.z + b.w * b.w));
            return n > 0.0f ? (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w) / n : 0.0f;
        }

        /// @return the rotation between two unit quaternions of dot `dot`, in degrees, whichever hemisphere each is on
        float QuatDegrees(float dot) { return 2.0f * std::acos((std::min)(1.0f, std::fabs(dot))) * (180.0f / std::numbers::pi_v<float>); }

        /// @brief Finds the arm bones in `context`'s animation skeleton when it is not the one last resolved.
        void ResolveArm(BlendSlot::ArmWatch& arm, const ::hkbContext& context, int bones)
        {
            const ::hkbCharacterSetup* setup    = context.m_character ? context.m_character->m_setup.val() : nullptr;
            const ::hkaSkeleton*       skeleton = setup ? setup->m_animationSkeleton.val() : nullptr;
            if (skeleton == arm.skeleton) return;
            arm          = {};
            arm.skeleton = skeleton;
            if (!skeleton) return;
            for (int b = 0; b < skeleton->m_bones.getSize(); ++b)
                if (const char* name = skeleton->m_bones[b].m_name.cString())
                    for (int k = 0; k < 2; ++k)
                        if (std::strcmp(name, kArmBones[k]) == 0) arm.bone[k] = b;
            CB_MESSAGE("MM arm watch: '{}' bone {}, '{}' bone {} (skeleton {} bones, pose {})", kArmBones[0], arm.bone[0], kArmBones[1],
                       arm.bone[1], skeleton->m_bones.getSize(), bones);
        }

        /// @brief Plots each arm bone's rotation against the last traced output's, live (the child's) and shown (blended).
        void PlotArm(BlendSlot::ArmWatch& arm, const loco::Quat (&live)[2], const loco::Bone* shown, int bones)
        {
            for (int k = 0; k < 2; ++k) {
                const int i = arm.bone[k];
                if (i < 0 || i >= bones) continue;
                if (arm.have) {
                    const float liveDot  = QuatDot(arm.live[k], live[k]);
                    const float shownDot = QuatDot(arm.shown[k], shown[i].q);
                    CB_PLOT(kArmLiveDot[k], liveDot);
                    CB_PLOT(kArmLiveDeg[k], QuatDegrees(liveDot));
                    CB_PLOT(kArmShownDot[k], shownDot);
                    CB_PLOT(kArmShownDeg[k], QuatDegrees(shownDot));
                }
                arm.live[k]  = live[k];
                arm.shown[k] = shown[i].q;
            }
            arm.have = true;
        }

    }  // namespace

    /// @note A parent activates before its children, and each child copies its bindings as it does: what the first search
    ///       writes here reaches the child that first plays.
    void PoseSearchGenerator::activate(const ::hkbContext& context)
    {
        CB_ZONE(zone, "MM activate");
        int lastShown = -1;
        {
            BlendTable&      table = Blends();
            std::scoped_lock lock(table.lock);
            if (const auto it = table.slots.find(this); it != table.slots.end()) {
                it->second.activated = true;
                lastShown            = it->second.blend.child;
            }
        }
        const std::uint64_t activation = g_activates.fetch_add(1, std::memory_order_relaxed) + 1;
        CB_PLOT("MM activate", activation);
        m_currentIndex = computeCurrentIndex();
        const int selected = m_currentIndex;
        /// @note A node with no children (a unit written before the schema keyed its rows `children:`, whose rows the
        ///       reader cannot see) plays nothing and searches nothing: every path below indexes a child.
        if (m_children.isEmpty()) {
            static std::atomic_bool warned{ false };
            if (!warned.exchange(true))
                LOG_ERROR("motion matching: '{}' has no children - it plays nothing (a stale bundle?)",
                          m_name.cString() ? m_name.cString() : "");
            return;
        }
        if (const int next = RunMatcher(context, *this, 0.0f, true); next >= 0 && next < m_children.getSize()) m_currentIndex = next;
        CB_MESSAGE("MM activate '{}' node {} ({}) - {} children, selected {} -> child {} (matcher at dt 0), last output child {}, #{} at {:.3f} s",
                   NameOf(*this), static_cast<const void*>(this), Whose(context), m_children.getSize(), selected, m_currentIndex, lastShown,
                   activation, Seconds());
    }

    void PoseSearchGenerator::deactivate(const ::hkbContext& context)
    {
        CB_ZONE(zone, "MM deactivate");
        const std::uint64_t deactivation = g_deactivates.fetch_add(1, std::memory_order_relaxed) + 1;
        CB_PLOT("MM deactivate", deactivation);
        CB_MESSAGE("MM deactivate '{}' node {} ({}) - {} children, playing child {}, #{} at {:.3f} s", NameOf(*this),
                   static_cast<const void*>(this), Whose(context), m_children.getSize(), m_currentIndex, deactivation, Seconds());
        hkbGenerator::deactivate(context);
    }

    void PoseSearchGenerator::update(const ::hkbContext& context, ::hkReal timestep)
    {
        CB_ZONE(zone, "MM update");
        if (timestep > 0.0f) {
            BlendTable& table = Blends();
            std::scoped_lock lock(table.lock);
            if (const auto it = table.slots.find(this); it != table.slots.end()) it->second.blend.dt = timestep;
        }
        if (m_children.isEmpty()) return;
        const int previous = m_currentIndex;
        m_currentIndex     = computeCurrentIndex();
        if (previous != m_currentIndex && m_children[previous].m_generator != m_children[m_currentIndex].m_generator) {
            ::hkbBehaviorGraph* rootBehavior = context.m_character->m_behaviorGraph;
            rootBehavior->m_updateActiveNodes  = true;
        }
        RunMatcher(context, *this, timestep, false);
    }

    void PoseSearchGenerator::BlendNextJump()
    {
        BlendTable&      table = Blends();
        std::scoped_lock lock(table.lock);
        if (const auto it = table.slots.find(this); it != table.slots.end()) it->second.jumpPending = true;
    }

    void PoseSearchGenerator::PlayUnbound(const ::hkbContext& context, int index, ::hkReal fraction)
    {
        if (index < 0 || index >= m_children.getSize()) return;
        m_selectedIndex = index;
        ::hkbGenerator* child = m_children[index].m_generator;
        if (!child || child->getClassType() != &::hkbClipGenerator::staticClass()) return;
        ::hkbNode* clone = context.m_character->m_behaviorGraph->getOrCreateNodeClone(child);
        static_cast<::hkbClipGenerator*>(clone)->m_userControlledTimeFraction = fraction;
    }

    std::optional<NodeDescription> PoseSearchGenerator::Describe(const ::hkbContext& context) const
    {
        const PoseSearchGenerator* from = this;
        if (m_cloneState == CLONE_STATE_CLONE) {
            from = static_cast<const PoseSearchGenerator*>(context.m_character->m_behaviorGraph->getNodeTemplate(this));
            if (!from) return std::nullopt;
        }
        const auto text = [](const char* s) { return s ? std::string(s) : std::string(); };
        const auto name = [&](const ::hkStringPtr& s) { return text(s.cString()); };
        NodeDescription     desc;
        loco::DatabaseSpec& spec = desc.spec;
        /// @note The rig the clips are measured through: the character's own animation skeleton, as loaded.
        if (const ::hkbCharacterSetup* setup = context.m_character ? context.m_character->m_setup.val() : nullptr)
            if (const ::hkaSkeleton* skeleton = setup->m_animationSkeleton.val())
                for (int b = 0; b < skeleton->m_bones.getSize(); ++b) {
                    anim::Rig::Bone bone;
                    bone.name   = name(skeleton->m_bones[b].m_name);
                    bone.parent = b < skeleton->m_parentIndices.getSize() ? skeleton->m_parentIndices[b] : -1;
                    if (b < skeleton->m_referencePose.getSize()) {
                        const ::hkQsTransform& t = skeleton->m_referencePose[b];
                        bone.translation         = { t.m_translation(0), t.m_translation(1), t.m_translation(2) };
                        bone.rotation            = { t.m_rotation.m_vec(0), t.m_rotation.m_vec(1), t.m_rotation.m_vec(2), t.m_rotation.m_vec(3) };
                    }
                    desc.rig.bones.push_back(std::move(bone));
                }
        if (const ::hkbVariableBindingSet* set = m_variableBindingSet.val())
            for (int b = 0; b < set->m_bindings.getSize(); ++b)
                if (const char* path = set->m_bindings[b].m_memberPath.cString(); path && std::strcmp(path, "speed") == 0) desc.speedBound = true;
        /// @note A schema shared by several databases is read once.
        std::vector<const PoseSearchSchema*> schemas;
        for (int i = 0; i < from->m_databases.getSize(); ++i) {
            const PoseSearchDatabase& d = from->m_databases[i];
            loco::SpecDatabase        out;
            out.name                   = name(d.m_name);
            out.baseCostBias           = name(d.m_baseCostBias);
            out.continuingPoseCostBias = name(d.m_continuingPoseCostBias);
            out.loopingCostBias        = name(d.m_loopingCostBias);
            out.standingCandidate      = name(d.m_standingCandidate);
            out.movingCandidate        = name(d.m_movingCandidate);
            for (int t = 0; t < d.m_tags.getSize(); ++t)
                if (const std::string tag = name(d.m_tags[t]); tag == "Pivots") out.pivots = true;
                else if (tag == "Stops") out.stops = true;
                else if (tag == "TurnInPlace") out.rootTurns = true;
            if (const auto* schema = static_cast<const PoseSearchSchema*>(d.m_schema)) {
                const auto it = std::find(schemas.begin(), schemas.end(), schema);
                out.schema    = static_cast<int>(it - schemas.begin());
                if (it == schemas.end()) {
                    schemas.push_back(schema);
                    desc.bound.emplace_back();
                    spec.schemas.push_back(SpecOfSchema(*schema, context, desc.bound.back()));
                }
            }
            spec.databases.push_back(std::move(out));
        }
        for (int i = 0; i < from->m_children.getSize(); ++i) {
            const PoseSearchChild& row = from->m_children[i];
            loco::SpecChild        out;
            const ::hkClass*       cls = row.m_generator ? row.m_generator->getClassType() : nullptr;
            if (cls && std::strcmp(cls->getName(), "hkbClipGenerator") == 0)
                out.clip = name(static_cast<const ::hkbClipGenerator*>(row.m_generator)->m_animationName);
            out.database   = text(row.m_database);
            out.rangeStart = row.m_rangeStart;
            out.rangeEnd   = row.m_rangeEnd;
            out.flags      = static_cast<std::uint8_t>((row.m_disableReselection ? loco::kFlagDisableReselection : 0) |
                                                       (row.m_looping ? loco::kFlagLooping : 0));
            spec.children.push_back(std::move(out));
        }
        if (const PoseSearchChooser* chooser = from->m_chooser) {
            spec.haveChooser = true;
            for (int r = 0; r < chooser->m_rows.getSize(); ++r) {
                const PoseSearchChooserRow& row = chooser->m_rows[r];
                loco::SpecChooserRow        out;
                out.when = name(row.m_when);
                for (int d = 0; d < row.m_databases.getSize(); ++d) out.databases.push_back(name(row.m_databases[d]));
                spec.rows.push_back(std::move(out));
            }
            for (int f = 0; f < chooser->m_fallback.getSize(); ++f) spec.fallback.push_back(name(chooser->m_fallback[f]));
        }
        return desc;
    }

    int PoseSearchGenerator::getMaxNumChildren(GetChildrenFlags flags)
    {
        return m_children.getSize();
    }

    /// @note Every child stays active, so a switch activates and deactivates nothing: a clip requests its animation on
    ///       activate and releases it on deactivate, and the stock selector's one active child made every switch stream a
    ///       file in (the A-pose until it landed). The playing child is reported first, so it is activeChildrenOutput[0];
    ///       the rest skip generate and events, as hkbGeneratorTransitionEffect keeps its to-generator warm.
    void PoseSearchGenerator::getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo)
    {
        const int n = m_children.getSize();
        childrenInfo.m_childInfos.setSize(n);
        if (n == 0) return;
        if (!(flags & FLAG_ACTIVE_ONLY)) {
            for (int i = 0; i < n; i++) childrenInfo.m_childInfos[i].m_node = m_children[i].m_generator;
            return;
        }
        childrenInfo.m_anyIgnoreEvents = true;
        childrenInfo.m_anySkipGenerate = true;
        int        slot = 0;
        const auto put  = [&](int i, bool playing) {
            ChildInfo& ci     = childrenInfo.m_childInfos[slot++];
            ci.m_node         = m_children[i].m_generator;
            ci.m_ignoreEvents = !playing;
            ci.m_reactivate   = false;
            ci.m_isActive     = false;
            ci.m_skipGenerate = !playing;
        };
        put(m_currentIndex, true);
        for (int i = 0; i < n; i++)
            if (i != m_currentIndex) put(i, false);
    }

    bool PoseSearchGenerator::isValid(::hkStringPtr& errorString) const
    {
        const int n = m_children.getSize();
        if (n == 0) {
            errorString = "You must have at least one child generator.";
            return false;
        }
        for (int i = 0; i < n; i++) {
            if (m_children[i].m_generator == HK_NULL) {
                errorString = "At least one of the child generators is null.";
                return false;
            }
        }
        if (m_selectedIndex < 0 || m_selectedIndex >= n) {
            errorString = "The selected generator is out of range.";
            return false;
        }
        return true;
    }

    ::hkbNode* PoseSearchGenerator::cloneNode(::hkbBehaviorGraph& rootBehavior) const
    {
        return new PoseSearchGenerator(*this);
    }

    void PoseSearchGenerator::generate(const ::hkbContext& context, const ::hkbGeneratorOutput** activeChildrenOutput,
                                       ::hkbGeneratorOutput& output, ::hkReal timeOffset)
    {
        CB_ZONE(zone, "MM generate");
        if (&output != activeChildrenOutput[0]) ::hkbGeneratorOutputUtils::copyGeneratorOutput(*activeChildrenOutput[0], output);
        deadBlend(context, output);
    }

    /// @note The engine's dead blend (locomotion/DeadBlend.h) on the playing child's pose, per character: a switch of
    ///       child coasts the pose then shown into the new child's. The pose is copied out of the output and back
    ///       (Bone mirrors hkQsTransform) so the engine stays Havok-free.
    void PoseSearchGenerator::deadBlend(const ::hkbContext& context, ::hkbGeneratorOutput& output)
    {
        CB_ZONE(zone, "MM dead blend");
        using Out = ::hkbGeneratorOutput;
        if (!output.isValid(Out::TRACK_POSE)) return;
        const Out::TrackHeader& header = output.getTrackHeader(Out::TRACK_POSE);
        const int               bones  = header.m_numData;
        constexpr int           kPlain = Out::TRACK_FLAG_ADDITIVE_POSE | Out::TRACK_FLAG_PALETTE | Out::TRACK_FLAG_SPARSE;
        BlendTable&             table  = Blends();
        std::scoped_lock        lock(table.lock);
        const auto              now    = Clock::now();
        const auto [found, created]    = table.slots.try_emplace(this);
        BlendSlot&              slot   = found->second;
        /// @note Who runs the node: the player, and NPCs under npc_controller; without that flag an NPC here means the
        ///       add-on's switch fired for it.
        if (created && table.seen.insert(context.m_character).second && table.seenLogs++ < kMaxSlotLogs) {
            const auto        player = IsPlayerCharacter(context.m_character);
            const char* const whose  = !player ? "not yet known (the skeleton watch has not run)" : *player ? "the player's" : "NOT the player's";
            LOG_INFO("motion matching: dead blend slot for hkbCharacter {} - {}", static_cast<const void*>(context.m_character), whose);
            CB_MESSAGE("MM blend slot for hkbCharacter {} - {}", static_cast<const void*>(context.m_character), whose);
        }
        /// @note A node that (re)activated since its last output starts over: the graph showed another generator meanwhile,
        ///       and a blend from this node's last pose (however recent) snaps back to it. No clock decides it, since a
        ///       pause or a menu stops the graph too.
        if (std::exchange(slot.activated, false)) {
            if (platform::profile::On()) {
                CB_PLOT("MM blend resets", g_blendResets.fetch_add(1, std::memory_order_relaxed) + 1);
                CB_MESSAGE("MM blend reset (reactivated) - index {}, last output child {}, {:.3f} s of blend dropped ({})", m_currentIndex,
                           slot.blend.child, slot.blend.Remaining(Tuning()), Whose(context));
            }
            slot.blend.Reset(0, m_currentIndex);
        }
        slot.used = now;
        if (++table.calls % kPruneEvery == 0)
            std::erase_if(table.slots, [&](const auto& kv) { return std::chrono::duration<double>(now - kv.second.used).count() > kDropAfter; });
        DeadBlend& b = slot.blend;
        if ((header.m_flags & kPlain) || bones <= 0) {
            b.Reset(0, m_currentIndex);
            return;
        }
        ::hkQsTransform*       out  = output.accessPoseLocal();
        std::vector<loco::Bone>& pose = slot.pose;
        pose.resize(static_cast<std::size_t>(bones));
        std::memcpy(pose.data(), out, sizeof(loco::Bone) * static_cast<std::size_t>(bones));
        const bool player      = IsPlayerCharacter(context.m_character).value_or(false);
        const bool tracePlayer = player && platform::profile::On();
        loco::Quat armLive[2]{};
        if (tracePlayer) {
            ResolveArm(slot.arm, context, bones);
            for (int k = 0; k < 2; ++k)
                if (const int i = slot.arm.bone[k]; i >= 0 && i < bones) armLive[k] = pose[static_cast<std::size_t>(i)].q;
        }
        /// @note The input guard: the live child's pose is not ours to fix, but a broken one must not be blamed on the blend.
        if (!g_inputLogged.load(std::memory_order_relaxed)) {
            for (int i = 0; i < bones; i++) {
                const loco::Bone& in = pose[i];
                if (loco::FiniteBone(in)) continue;
                if (g_inputLogged.exchange(true)) break;
                LOG_WARN("motion matching: input pose non-finite (not the blend's) - bone {} of {}, child {}: t ({}, {}, {}) q ({}, {}, {}, {})",
                         i, bones, m_currentIndex, in.t[0], in.t[1], in.t[2], in.q.x, in.q.y, in.q.z, in.q.w);
                CB_MESSAGE("MM input pose non-finite - bone {}, child {}", i, m_currentIndex);
                break;
            }
        }
        const loco::DeadBlendTuning& s = Tuning();
        loco::BlendFault             fault;
        const bool jumped = std::exchange(slot.jumpNow, std::exchange(slot.jumpPending, false));
        if (!b.Step(pose.data(), bones, m_currentIndex, jumped, s, &fault)) return;
        if (fault.bone >= 0 && !g_outputLogged.exchange(true)) {
            const loco::Bone& live = fault.blended;
            const loco::Bone& in   = fault.live;
            LOG_WARN("motion matching: blended bone {} of {} broken (child {}, {:.3f} s into a {:.2f} s blend, {:.0f} from live) "
                     "- t ({}, {}, {}) q ({}, {}, {}, {}); live t ({}, {}, {}); fell back to live",
                     fault.bone, bones, m_currentIndex, b.t, s.blendTime, fault.offset, live.t[0], live.t[1], live.t[2], live.q.x, live.q.y,
                     live.q.z, live.q.w, in.t[0], in.t[1], in.t[2]);
            CB_MESSAGE("MM blended bone {} broken (child {}, {:.3f} s in) - fell back to live", fault.bone, m_currentIndex, b.t);
        }
        std::memcpy(out, pose.data(), sizeof(loco::Bone) * static_cast<std::size_t>(bones));
        /// @note For the player's HUD and Describe: an NPC's blend isn't the one they show.
        if (player) g_blendRemaining.store(b.Remaining(s), std::memory_order_relaxed);
        CB_PLOT("MM blend left", b.Remaining(s));
        if (tracePlayer) {
            CB_PLOT("MM player blend left", b.Remaining(s));
            PlotArm(slot.arm, armLive, pose.data(), bones);
        }
    }

    float BlendRemaining()
    {
        return g_blendRemaining.load(std::memory_order_relaxed);
    }

    bool PoseSearchGenerator::canRecycleOutput() const
    {
        return true;
    }

    /// @note The node is no sync source and no proxy: the matcher owns its clip's time. As a proxy of the playing clip
    ///       (MODE_USER_CONTROLLED, its time re-derived from the bound fraction every update), a FLAG_SYNC transition
    ///       above it (Sprint/SneakSyncBlendTransition) handed it the effect's phase error modulo the clip's period as
    ///       its timestep: 0.5-3.1 s in one frame on the long sprint transitions, which leapt the clip forward. A base
    ///       frequency of 0 makes the graph pass the frame's timestep straight through (playback speed 1: 0 freezes it).
    void PoseSearchGenerator::updateSync(const ::hkbContext& context, ::hkbNodeInfo& info)
    {
        ::hkbGenerator::updateSync(context, info);
        info.m_isProxy = false;
    }

    int PoseSearchGenerator::computeCurrentIndex() const
    {
        const int last = m_children.getSize() - 1;
        if (m_selectedIndex < 0 || last < 0) return 0;
        return m_selectedIndex > last ? last : m_selectedIndex;
    }

}  // namespace CB::feature::controller::motion
