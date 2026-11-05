#include "PCH.h"

#include "CharPoseSearch.h"

#include "CharGround.h"
#include "CharMotionMatching.h"
#include "CharSkeletonWatch.h"

#include <locomotion/DeadBlend.h>
#include <platform/classes/ClassRegistry.h>
#include <platform/profile/Profile.h>

#include <Common/Base/hkBase.h>
#include <Common/Base/Reflection/hkClass.h>
#include <Behavior/Behavior/BehaviorGraph/hkbNodeInfo.h>
#include <Behavior/Behavior/Character/hkbCharacter.h>
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
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/// @file
/// @brief CBPoseSearchGenerator's behavior, after havok/'s hkbManualSelectorGenerator line for line, with int32 indices.
/// @note  Registered whatever the flags: a behavior holding this class loads with the add-on whether or not motion
///        matching runs, and an unregistered class name loads with a null vtable.

namespace CB::feature::controller::motion {

    namespace detail {

        namespace loco = CB::engine::locomotion;
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
        std::chrono::steady_clock::time_point used{};
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
    constexpr double        kStaleAfter = 0.5;    ///< seconds unused before a slot starts over
    constexpr double        kDropAfter  = 10.0;   ///< seconds unused before a slot is dropped
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
        m_chooser(nullptr)
    {
        m_children = other.m_children;
        for (int i = 0; i < m_children.getSize(); i++) m_children[i].m_generator->addReference();
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

    /// @note A parent activates before its children, and each child copies its bindings as it does: what the first search
    ///       writes here reaches the child that first plays.
    void PoseSearchGenerator::activate(const ::hkbContext& context)
    {
        m_currentIndex = computeCurrentIndex();
        if (const int next = RunMatcher(context, *this, 0.0f, true); next >= 0 && next < m_children.getSize()) m_currentIndex = next;
    }

    void PoseSearchGenerator::update(const ::hkbContext& context, ::hkReal timestep)
    {
        if (timestep > 0.0f) {
            BlendTable& table = Blends();
            std::scoped_lock lock(table.lock);
            if (const auto it = table.slots.find(this); it != table.slots.end()) it->second.blend.dt = timestep;
        }
        const int previous = m_currentIndex;
        m_currentIndex     = computeCurrentIndex();
        if (previous != m_currentIndex && m_children[previous].m_generator != m_children[m_currentIndex].m_generator) {
            ::hkbBehaviorGraph* rootBehavior = context.m_character->m_behaviorGraph;
            rootBehavior->m_updateActiveNodes  = true;
        }
        RunMatcher(context, *this, timestep, false);
    }

    void PoseSearchGenerator::PlayUnbound(const ::hkbContext& context, int index, ::hkReal fraction)
    {
        m_selectedIndex = index;
        ::hkbGenerator* child = m_children[index].m_generator;
        if (!child || child->getClassType() != &::hkbClipGenerator::staticClass()) return;
        ::hkbNode* clone = context.m_character->m_behaviorGraph->getOrCreateNodeClone(child);
        static_cast<::hkbClipGenerator*>(clone)->m_userControlledTimeFraction = fraction;
    }

    std::optional<CB::engine::locomotion::DatabaseSpec> PoseSearchGenerator::Describe(const ::hkbContext& context) const
    {
        const PoseSearchGenerator* from = this;
        if (m_cloneState == CLONE_STATE_CLONE) {
            from = static_cast<const PoseSearchGenerator*>(context.m_character->m_behaviorGraph->getNodeTemplate(this));
            if (!from) return std::nullopt;
        }
        const auto text = [](const char* s) { return s ? std::string(s) : std::string(); };
        const auto name = [&](const ::hkStringPtr& s) { return text(s.cString()); };
        loco::DatabaseSpec spec;
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
                if (name(d.m_tags[t]) == "Pivots") out.pivots = true;
            for (int w = 0; w < d.m_weights.getSize(); ++w) out.weights.emplace_back(name(d.m_weights[w].m_group), d.m_weights[w].m_weight);
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
        return spec;
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
        /// @note A slot unused for a while is stale (a reused character address, or a graph that stopped playing the
        ///       node): it starts over instead of blending from another life's pose.
        if (std::chrono::duration<double>(now - slot.used).count() > kStaleAfter) slot.blend.Reset(0, m_currentIndex);
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
        if (!b.Step(pose.data(), bones, m_currentIndex, s, &fault)) return;
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
        if (IsPlayerCharacter(context.m_character).value_or(false)) g_blendRemaining.store(b.Remaining(s), std::memory_order_relaxed);
        CB_PLOT("MM blend left", b.Remaining(s));
    }

    float BlendRemaining()
    {
        return g_blendRemaining.load(std::memory_order_relaxed);
    }

    bool PoseSearchGenerator::canRecycleOutput() const
    {
        return true;
    }

    void PoseSearchGenerator::updateSync(const ::hkbContext& context, ::hkbNodeInfo& info)
    {
        ::hkbBehaviorGraph* rootBehavior = context.m_character->m_behaviorGraph;
        const ::hkbNode*    child        = m_children[m_currentIndex].m_generator;
        if (child->m_cloneState == CLONE_STATE_CLONE) child = rootBehavior->getNodeTemplate(child);
        const int index   = rootBehavior->m_activeNodeTemplateToIndexMap->getWithDefault(child, 0);
        info.m_isProxy    = true;
        info.m_proxyIndex = (*rootBehavior->m_activeNodes)[index].m_isProxy ? (*rootBehavior->m_activeNodes)[index].m_proxyIndex : index;
    }

    int PoseSearchGenerator::computeCurrentIndex() const
    {
        const int last = m_children.getSize() - 1;
        if (m_selectedIndex < 0) return 0;
        return m_selectedIndex > last ? last : m_selectedIndex;
    }

}  // namespace CB::feature::controller::motion
