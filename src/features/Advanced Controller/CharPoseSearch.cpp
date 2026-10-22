#include "PCH.h"

#include "CharPoseSearch.h"

#include "CharGround.h"
#include "CharSkeletonWatch.h"

#include <platform/classes/ClassRegistry.h>
#include <platform/profile/Profile.h>

#include <Behavior/BehaviorGraph/hkbNodeInfo.h>
#include <Behavior/Character/hkbCharacter.h>
#include <Behavior/Generator/hkbGeneratorOutput.h>
#include <Behavior/Generator/hkbGeneratorOutputUtils.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <new>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/// @file
/// @brief CBPoseSearchGenerator's behavior, after havok/'s hkbManualSelectorGenerator line for line, with int32 indices.
/// @note  Registered whatever the flags: a behavior holding this class loads with the add-on whether or not motion
///        matching runs, and an unregistered class name loads with a null vtable.

namespace CB::feature::controller::motion {

    namespace detail {

        struct Quat {
            float x = 0, y = 0, z = 0, w = 1;
        };
        /// @brief One bone of a local pose (hkQsTransform: translation, rotation, scale; the w lanes kept as they were).
        struct Bone {
            float t[4] = {};
            Quat  q;
            float s[4] = { 1, 1, 1, 0 };
        };
        static_assert(sizeof(Bone) == sizeof(CB::havok::hkQsTransform), "Bone mirrors hkQsTransform");
        /// @brief A bone's velocity: linear (pose units/s) and angular (rad/s, scaled axis, parent space).
        struct Velocity {
            float t[3] = {}, w[3] = {};
        };

        inline Bone Load(const CB::havok::hkQsTransform& x)
        {
            Bone b;
            std::memcpy(&b, &x, sizeof b);
            return b;
        }
        inline void Store(const Bone& b, CB::havok::hkQsTransform& x) { std::memcpy(&x, &b, sizeof b); }

        inline Quat Mul(const Quat& a, const Quat& b)
        {
            return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                     a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
        }
        inline Quat Conj(const Quat& q) { return { -q.x, -q.y, -q.z, q.w }; }
        inline Quat Normalize(const Quat& q)
        {
            const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
            return n > 1.0e-8f ? Quat{ q.x / n, q.y / n, q.z / n, q.w / n } : Quat{};
        }
        /// @brief The rotation by |(x, y, z)| radians about (x, y, z).
        inline Quat FromScaledAxis(float x, float y, float z)
        {
            const float angle = std::sqrt(x * x + y * y + z * z);
            if (angle < 1.0e-6f) return Normalize({ 0.5f * x, 0.5f * y, 0.5f * z, 1.0f });
            const float s = std::sin(0.5f * angle) / angle;
            return { x * s, y * s, z * s, std::cos(0.5f * angle) };
        }
        /// @brief Shortest-path slerp from `a` to `b`.
        inline Quat Slerp(const Quat& a, Quat b, float t)
        {
            float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
            if (d < 0.0f) b = { -b.x, -b.y, -b.z, -b.w }, d = -d;
            if (d > 0.9995f)
                return Normalize({ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t });
            const float th = std::acos(d), s = std::sin(th);
            const float wa = std::sin((1.0f - t) * th) / s, wb = std::sin(t * th) / s;
            return { a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb };
        }
        /// @brief How far a velocity decaying with `halflife` carries over `t`: the integral of e^(-lambda s) on [0, t].
        inline float Coast(float t, float halflife)
        {
            const float lambda = 0.69314718f / halflife;
            return (1.0f - std::exp(-lambda * t)) / lambda;
        }

        /// @brief The blend's tuning, read once from settings.ini [Character].
        struct Settings {
            float blendTime = 0.2f;   ///< fMMBlendTime: seconds the cross-fade takes; 0 switches blending off
            float decay     = 0.1f;   ///< fMMBlendDecay: halflife (s) of the coasting pose's velocity
        };
        inline const Settings& Tuning()
        {
            static const Settings s{ std::clamp(SettingFloat("fMMBlendTime", 0.2f), 0.0f, 1.0f),
                                     std::clamp(SettingFloat("fMMBlendDecay", 0.1f), 0.01f, 1.0f) };
            return s;
        }

        /// @brief Caps on a coasting bone's velocity, so one bad frame cannot fling it.
        constexpr float kMaxAngular = 20.0f;     ///< rad/s
        constexpr float kMaxLinear  = 2000.0f;   ///< pose units/s

        inline std::atomic<float> g_blendRemaining{ 0.0f };

        /// @brief A blended bone further than this from the live child's (pose units) is taken as broken.
        constexpr float kMaxBlendOffset = 500.0f;
        constexpr int   kMaxSlotLogs    = 64;   ///< "who runs the node" lines per session

        /// @brief The guards' one-shot logs (a broken pose would otherwise log every frame).
        inline std::atomic<bool> g_inputLogged{ false }, g_outputLogged{ false };

        inline bool FiniteBone(const Bone& b)
        {
            for (int a = 0; a < 3; a++)
                if (!std::isfinite(b.t[a])) return false;
            return std::isfinite(b.q.x) && std::isfinite(b.q.y) && std::isfinite(b.q.z) && std::isfinite(b.q.w);
        }

    }  // namespace detail

    using namespace detail;

    /// @brief One character's dead-blend state: its last two output poses, and the pose and velocity frozen at a switch.
    struct DeadBlend {
        int                   bones = 0;
        int                   child = -1;     ///< the child the last output played
        int                   history = 0;    ///< outputs recorded since the last reset, up to 2
        float                 dt = 1.0f / 60.0f;   ///< the latest update's timestep
        bool                  blending = false;
        float                 t = 0.0f;       ///< seconds into the blend
        std::vector<Bone>     last, prev, from;
        std::vector<Velocity> vel;

        void Reset(int n, int playing)
        {
            bones = n, child = playing, history = 0, blending = false, t = 0.0f;
            last.assign(static_cast<std::size_t>(n), Bone{});
            prev.assign(static_cast<std::size_t>(n), Bone{});
        }
        void Record(const CB::havok::hkQsTransform* pose)
        {
            prev.swap(last);
            for (int i = 0; i < bones; i++) last[i] = Load(pose[i]);
            history = (std::min)(history + 1, 2);
        }
        /// @brief Freeze the pose last shown and its velocity (from the last two outputs) as the blend's source.
        void Start()
        {
            from = last;
            vel.assign(static_cast<std::size_t>(bones), Velocity{});
            const float inv = dt > 1.0e-5f ? 1.0f / dt : 60.0f;
            for (int i = 0; i < bones; i++) {
                Velocity& v = vel[i];
                for (int a = 0; a < 3; a++) v.t[a] = std::clamp((last[i].t[a] - prev[i].t[a]) * inv, -kMaxLinear, kMaxLinear);
                Quat dq = Mul(last[i].q, Conj(prev[i].q));
                if (dq.w < 0.0f) dq = { -dq.x, -dq.y, -dq.z, -dq.w };
                const float sn    = std::sqrt(dq.x * dq.x + dq.y * dq.y + dq.z * dq.z);
                const float angle = 2.0f * std::atan2(sn, dq.w);
                float       rate  = angle * inv;
                if (rate > kMaxAngular) rate = kMaxAngular;
                const float scale = sn > 1.0e-7f ? rate / sn : 0.0f;
                v.w[0] = dq.x * scale, v.w[1] = dq.y * scale, v.w[2] = dq.z * scale;
            }
            t        = 0.0f;
            blending = true;
        }
    };

    /// @brief The dead blend's state per character (hkbContext::m_character), never in the node: the engine block-copies
    ///        graphs, so node-owned state would be shared across copies and freed by the first destroyed.
    /// @note  Only the player plays the node, so the lock is uncontended.
    struct BlendSlot {
        DeadBlend                             blend;
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

        namespace hk = CB::havok;

        constexpr const char* kClassName = "CBPoseSearchGenerator";

        void Finish(void* object)
        {
            if (object) new (object) PoseSearchGenerator(hk::hkFinishLoadedObjectFlag{});
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
            if (object) new (object) PoseSearchChooser(hk::hkFinishLoadedObjectFlag{});
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
    PoseSearchChooser::PoseSearchChooser(hk::hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag),
        m_rows(flag),
        m_fallback(flag)
    {
    }

    const void* PoseSearchChooser::getClassType() const
    {
        static const void* const cls = platform::classes::ClassOf(kChooserClassName);
        return cls;
    }

    PoseSearchGenerator::PoseSearchGenerator(hk::hkFinishLoadedObjectFlag flag)
    :   hkbGenerator(flag),
        m_generators(flag),
        m_databases(flag),
        m_childFlags(flag)
    {
        m_currentIndex = 0;
    }

    PoseSearchGenerator::PoseSearchGenerator(const PoseSearchGenerator& other)
    :   hkbGenerator(other),
        m_selectedIndex(other.m_selectedIndex),
        m_currentIndex(0),
        m_chooser(nullptr)
    {
        m_generators = other.m_generators;
        for (int i = 0; i < m_generators.getSize(); i++) m_generators[i]->addReference();
    }

    PoseSearchGenerator::~PoseSearchGenerator()
    {
        for (int i = 0; i < m_generators.getSize(); i++)
            if (m_generators[i]) m_generators[i]->removeReference();
    }

    const void* PoseSearchGenerator::getClassType() const
    {
        static const void* const cls = platform::classes::ClassOf(kClassName);
        return cls;
    }

    void PoseSearchGenerator::activate(const hk::hkbContext& context)
    {
        m_currentIndex = computeCurrentIndex();
    }

    void PoseSearchGenerator::update(const hk::hkbContext& context, hk::hkReal timestep)
    {
        if (timestep > 0.0f) {
            BlendTable& table = Blends();
            std::scoped_lock lock(table.lock);
            if (const auto it = table.slots.find(context.m_character); it != table.slots.end()) it->second.blend.dt = timestep;
        }
        const int previous = m_currentIndex;
        m_currentIndex     = computeCurrentIndex();
        if (previous != m_currentIndex && m_generators[previous] != m_generators[m_currentIndex]) {
            hk::hkbBehaviorGraph* rootBehavior = context.m_character->m_behaviorGraph;
            rootBehavior->m_updateActiveNodes  = true;
        }
    }

    int PoseSearchGenerator::getMaxNumChildren(GetChildrenFlags flags)
    {
        return m_generators.getSize();
    }

    /// @note Every child stays active, so a switch activates and deactivates nothing: a clip requests its animation on
    ///       activate and releases it on deactivate, and the stock selector's one active child made every switch stream a
    ///       file in (the A-pose until it landed). The playing child is reported first, so it is activeChildrenOutput[0];
    ///       the rest skip generate and events, as hkbGeneratorTransitionEffect keeps its to-generator warm.
    void PoseSearchGenerator::getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo)
    {
        const int n = m_generators.getSize();
        childrenInfo.m_childInfos.setSize(n);
        if (!(flags & FLAG_ACTIVE_ONLY)) {
            for (int i = 0; i < n; i++) childrenInfo.m_childInfos[i].m_node = m_generators[i];
            return;
        }
        childrenInfo.m_anyIgnoreEvents = true;
        childrenInfo.m_anySkipGenerate = true;
        int        slot = 0;
        const auto put  = [&](int i, bool playing) {
            ChildInfo& ci     = childrenInfo.m_childInfos[slot++];
            ci.m_node         = m_generators[i];
            ci.m_ignoreEvents = !playing;
            ci.m_reactivate   = false;
            ci.m_isActive     = false;
            ci.m_skipGenerate = !playing;
        };
        put(m_currentIndex, true);
        for (int i = 0; i < n; i++)
            if (i != m_currentIndex) put(i, false);
    }

    bool PoseSearchGenerator::isValid(hk::hkStringPtr& errorString) const
    {
        const int n = m_generators.getSize();
        if (n == 0) {
            errorString = "You must have at least one child generator.";
            return false;
        }
        for (int i = 0; i < n; i++) {
            if (m_generators[i] == HK_NULL) {
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

    hk::hkbNode* PoseSearchGenerator::cloneNode(hk::hkbBehaviorGraph& rootBehavior) const
    {
        return new PoseSearchGenerator(*this);
    }

    void PoseSearchGenerator::generate(const hk::hkbContext& context, const hk::hkbGeneratorOutput** activeChildrenOutput,
                                       hk::hkbGeneratorOutput& output, hk::hkReal timeOffset)
    {
        CB_ZONE(zone, "MM generate");
        if (&output != activeChildrenOutput[0]) hk::hkbGeneratorOutputUtils::copyGeneratorOutput(*activeChildrenOutput[0], output);
        deadBlend(context, output);
    }

    /// @note Dead blending (Holden): on a switch the pose being shown keeps moving at its own last velocity, slowing
    ///       with kDecay's halflife, and the output crosses from that coasting pose into the live child's over the blend
    ///       time on a smoothstep. It needs only this node's own past outputs, not the new child's velocity, which the
    ///       node never sees. A switch mid-blend starts from the pose then shown, so switches never pop.
    void PoseSearchGenerator::deadBlend(const hk::hkbContext& context, hk::hkbGeneratorOutput& output)
    {
        CB_ZONE(zone, "MM dead blend");
        using Out = hk::hkbGeneratorOutput;
        if (!output.isValid(Out::TRACK_POSE)) return;
        const Out::TrackHeader& header = output.getTrackHeader(Out::TRACK_POSE);
        const int               bones  = header.m_numData;
        constexpr int           kPlain = Out::TRACK_FLAG_ADDITIVE_POSE | Out::TRACK_FLAG_PALETTE | Out::TRACK_FLAG_SPARSE;
        BlendTable&             table  = Blends();
        std::scoped_lock        lock(table.lock);
        const auto              now    = Clock::now();
        const auto [found, created]    = table.slots.try_emplace(context.m_character);
        BlendSlot&              slot   = found->second;
        /// @note Who runs the node: only the player should (MM_Enable is set on the player's graph alone), so an NPC
        ///       here means the add-on's switch fired for it.
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
        hk::hkQsTransform* pose = output.accessPoseLocal();
        /// @note The input guard: the live child's pose is not ours to fix, but a broken one must not be blamed on the blend.
        if (!g_inputLogged.load(std::memory_order_relaxed)) {
            for (int i = 0; i < bones; i++) {
                const Bone in = Load(pose[i]);
                if (FiniteBone(in)) continue;
                if (g_inputLogged.exchange(true)) break;
                LOG_WARN("motion matching: input pose non-finite (not the blend's) - bone {} of {}, child {}: t ({}, {}, {}) q ({}, {}, {}, {})",
                         i, bones, m_currentIndex, in.t[0], in.t[1], in.t[2], in.q.x, in.q.y, in.q.z, in.q.w);
                CB_MESSAGE("MM input pose non-finite - bone {}, child {}", i, m_currentIndex);
                break;
            }
        }
        if (b.bones != bones) {
            b.Reset(bones, m_currentIndex);
            b.Record(pose);
            return;
        }
        const Settings& s = Tuning();
        if (b.child != m_currentIndex && b.history >= 2 && s.blendTime > 0.0f) b.Start();
        b.child = m_currentIndex;
        if (b.blending) {
            b.t += b.dt;
            if (b.t >= s.blendTime) {
                b.blending = false;
            } else {
                const float u     = b.t / s.blendTime;
                const float alpha = u * u * (3.0f - 2.0f * u);
                const float k     = Coast(b.t, s.decay);
                for (int i = 0; i < bones; i++) {
                    const Bone in   = Load(pose[i]);
                    Bone       live = in;
                    Bone       from = b.from[i];
                    for (int a = 0; a < 3; a++) from.t[a] += b.vel[i].t[a] * k;
                    Quat dq = FromScaledAxis(b.vel[i].w[0] * k, b.vel[i].w[1] * k, b.vel[i].w[2] * k);
                    from.q  = Mul(dq, from.q);
                    for (int a = 0; a < 3; a++) live.t[a] = from.t[a] + (live.t[a] - from.t[a]) * alpha;
                    live.q = Slerp(from.q, live.q, alpha);
                    /// @note The output guard: a non-finite or flung blended bone falls back to the live child's.
                    const float off = std::sqrt((live.t[0] - in.t[0]) * (live.t[0] - in.t[0]) + (live.t[1] - in.t[1]) * (live.t[1] - in.t[1]) +
                                                (live.t[2] - in.t[2]) * (live.t[2] - in.t[2]));
                    if (!FiniteBone(live) || !(off <= kMaxBlendOffset)) {
                        if (FiniteBone(in) && !g_outputLogged.exchange(true)) {
                            LOG_WARN("motion matching: blended bone {} of {} broken (child {}, {:.3f} s into a {:.2f} s blend, {:.0f} from live) "
                                     "- t ({}, {}, {}) q ({}, {}, {}, {}); live t ({}, {}, {}); fell back to live",
                                     i, bones, m_currentIndex, b.t, s.blendTime, off, live.t[0], live.t[1], live.t[2], live.q.x,
                                     live.q.y, live.q.z, live.q.w, in.t[0], in.t[1], in.t[2]);
                            CB_MESSAGE("MM blended bone {} broken (child {}, {:.3f} s in) - fell back to live", i, m_currentIndex, b.t);
                        }
                        live = in;
                    }
                    Store(live, pose[i]);
                }
            }
        }
        g_blendRemaining.store(b.blending ? s.blendTime - b.t : 0.0f, std::memory_order_relaxed);
        CB_PLOT("MM blend left", b.blending ? s.blendTime - b.t : 0.0f);
        b.Record(pose);
    }

    float BlendRemaining()
    {
        return g_blendRemaining.load(std::memory_order_relaxed);
    }

    bool PoseSearchGenerator::canRecycleOutput() const
    {
        return true;
    }

    void PoseSearchGenerator::updateSync(const hk::hkbContext& context, hk::hkbNodeInfo& info)
    {
        hk::hkbBehaviorGraph* rootBehavior = context.m_character->m_behaviorGraph;
        const hk::hkbNode*    child        = m_generators[m_currentIndex];
        if (child->m_cloneState == CLONE_STATE_CLONE) child = rootBehavior->getNodeTemplate(child);
        const int index   = rootBehavior->m_activeNodeTemplateToIndexMap->getWithDefault(child, 0);
        info.m_isProxy    = true;
        info.m_proxyIndex = (*rootBehavior->m_activeNodes)[index].m_isProxy ? (*rootBehavior->m_activeNodes)[index].m_proxyIndex : index;
    }

    int PoseSearchGenerator::computeCurrentIndex() const
    {
        const int last = m_generators.getSize() - 1;
        if (m_selectedIndex < 0) return 0;
        return m_selectedIndex > last ? last : m_selectedIndex;
    }

}  // namespace CB::feature::controller::motion
