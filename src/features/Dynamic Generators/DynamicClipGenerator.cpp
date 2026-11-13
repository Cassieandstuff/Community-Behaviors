#include "PCH.h"

#include "DynamicClipGenerator.h"

#include "ScopedConditions.h"

#include <hky/locomotion/DeadBlend.h>
#include <platform/classes/ClassRegistry.h>
#include <platform/game/Character.h>
#include <platform/profile/Profile.h>
#include <hky/relay/DynamicClip.h>

#include <Behavior/Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Behavior/Character/hkbCharacter.h>
#include <Behavior/Behavior/Context/hkbContext.h>
#include <Behavior/Behavior/Generator/Clip/hkbClipGenerator.h>
#include <Behavior/Behavior/Generator/hkbGeneratorOutput.h>
#include <Behavior/Behavior/Generator/hkbGeneratorOutputUtils.h>
#include <Common/Base/Reflection/hkClass.h>

#include <chrono>
#include <cstring>
#include <mutex>
#include <new>
#include <unordered_map>
#include <vector>

namespace CB::feature::dynamic {

    namespace {

        namespace rl   = hky::relay;
        namespace loco = hky::locomotion;

        constexpr const char* kClassName = "CBDynamicClipGenerator";
        constexpr int         kFallback  = -1;
        constexpr int         kNothing   = -2;

        void Finish(void* object)
        {
            if (object) new (object) DynamicClipGenerator(::hkFinishLoadedObjectFlag{});
        }
        void Cleanup(void* object) { static_cast<DynamicClipGenerator*>(object)->~DynamicClipGenerator(); }
        /// @note As the exe's own type infos take it: a finish constructor run on a scratch buffer, its vtable read off.
        const void* Vtable()
        {
            alignas(DynamicClipGenerator) unsigned char scratch[sizeof(DynamicClipGenerator)] = {};
            Finish(scratch);
            return *reinterpret_cast<const void* const*>(scratch);
        }
        /// @note Registered whatever the flags: an unregistered class name loads with a null vtable.
        [[maybe_unused]] const bool s_declared = platform::classes::Declare(
            { kClassName, sizeof(DynamicClipGenerator), &Finish, &Cleanup, &Vtable });

        /// @brief One clone's dead blend: kept beside the node, keyed by it, as the pose search's is (CharPoseSearch).
        struct BlendSlot {
            loco::DeadBlend         blend;
            std::vector<loco::Bone> pose;
            bool                    restart = true;
            std::chrono::steady_clock::time_point used;
        };
        struct BlendTable {
            std::mutex                                              lock;
            std::unordered_map<const DynamicClipGenerator*, BlendSlot> slots;
        };
        BlendTable& Blends()
        {
            static BlendTable t;
            return t;
        }

        /// @brief The candidates as the pick sees them: each row's selection data, and whether its condition holds now.
        /// @note  One evaluation pass, so scoped conditions resolve each scope chain once across all the rows.
        std::vector<rl::ClipCandidate> Evaluate(const ::hkArray<DynamicClipRow>& rows, const ::hkbContext& context)
        {
            CB_ZONE(zone, "Dynamic clip evaluate");
            const EvaluationScope pass;
            std::vector<rl::ClipCandidate> out(static_cast<std::size_t>(rows.getSize()));
            for (int i = 0; i < rows.getSize(); ++i) {
                const DynamicClipRow& r = rows[i];
                rl::ClipCandidate&    c = out[static_cast<std::size_t>(i)];
                c.priority              = r.m_priority;
                c.weight                = r.m_weight;
                c.flags                 = static_cast<std::uint8_t>(r.m_flags);
                c.passes                = r.m_clip && (!r.m_condition || r.m_condition->isTrue(context));
            }
            return out;
        }

        std::uint32_t ActorId(const ::hkbContext& context)
        {
            RE::Actor* actor = platform::game::ActorOf(context.m_character);
            return actor ? actor->GetFormID() : 0;
        }

    }  // namespace

    DynamicClipGenerator::DynamicClipGenerator(::hkFinishLoadedObjectFlag flag) :
        hkbGenerator(flag), m_folder(flag), m_fallback(flag), m_rows(flag)
    {
        m_current       = kNothing;
        m_activations   = 0;
        m_loops         = 0;
        m_lastLocalTime = 0.0f;
    }

    DynamicClipGenerator::DynamicClipGenerator(const DynamicClipGenerator& other) :
        hkbGenerator(other),
        m_triggers(other.m_triggers),
        m_cropStartAmountLocalTime(other.m_cropStartAmountLocalTime),
        m_cropEndAmountLocalTime(other.m_cropEndAmountLocalTime),
        m_startTime(other.m_startTime),
        m_playbackSpeed(other.m_playbackSpeed),
        m_enforcedDuration(other.m_enforcedDuration),
        m_userControlledTimeFraction(other.m_userControlledTimeFraction),
        m_mode(other.m_mode),
        m_flags(other.m_flags),
        m_folder(other.m_folder),
        m_fallback(other.m_fallback),
        m_blendDuration(other.m_blendDuration),
        m_fallbackClip(other.m_fallbackClip),
        m_current(kNothing),
        m_activations(0),
        m_loops(0),
        m_lastLocalTime(0.0f)
    {
        m_rows = other.m_rows;
        for (int i = 0; i < m_rows.getSize(); ++i) {
            if (m_rows[i].m_clip) m_rows[i].m_clip->addReference();
            if (m_rows[i].m_condition) m_rows[i].m_condition->addReference();
        }
        if (m_fallbackClip) m_fallbackClip->addReference();
    }

    /// @note Only a clone took references (the copy constructor); a template's rows are the loaded file's.
    DynamicClipGenerator::~DynamicClipGenerator()
    {
        {
            BlendTable&      table = Blends();
            std::scoped_lock lock(table.lock);
            table.slots.erase(this);
        }
        if (m_cloneState != CLONE_STATE_CLONE) return;
        for (int i = 0; i < m_rows.getSize(); ++i) {
            if (m_rows[i].m_clip) m_rows[i].m_clip->removeReference();
            if (m_rows[i].m_condition) m_rows[i].m_condition->removeReference();
        }
        if (m_fallbackClip) m_fallbackClip->removeReference();
    }

    const ::hkClass* DynamicClipGenerator::getClassType() const
    {
        static const auto* const cls = static_cast<const ::hkClass*>(platform::classes::ClassOf(kClassName));
        return cls;
    }

    ::hkbGenerator* DynamicClipGenerator::currentChild() const
    {
        if (m_current >= 0 && m_current < m_rows.getSize()) return m_rows[m_current].m_clip;
        if (m_current == kFallback) return m_fallbackClip;
        return nullptr;
    }

    int DynamicClipGenerator::pick(const ::hkbContext& context)
    {
        const auto candidates = Evaluate(m_rows, context);
        const int  row        = rl::PickClip(candidates, rl::PickSeed(ActorId(context), m_activations, m_loops));
        if (row >= 0) return row;
        return m_fallbackClip ? kFallback : kNothing;
    }

    void DynamicClipGenerator::switchTo(const ::hkbContext& context, int row)
    {
        if (row == m_current) return;
        const ::hkbGenerator* before = currentChild();
        m_current                    = row;
        m_lastLocalTime              = 0.0f;
        if (before != currentChild() && context.m_character && context.m_character->m_behaviorGraph)
            context.m_character->m_behaviorGraph->m_updateActiveNodes = true;
    }

    void DynamicClipGenerator::activate(const ::hkbContext& context)
    {
        ++m_activations;
        m_loops         = 0;
        m_lastLocalTime = 0.0f;
        m_current       = pick(context);
        if (m_current == kNothing)
            LOG_WARN("[dynamic-clip] '{}' on {:08X}: no clip holds and no fallback - it plays nothing",
                     m_name.cString() ? m_name.cString() : "", ActorId(context));
        BlendTable&      table = Blends();
        std::scoped_lock lock(table.lock);
        table.slots[this].restart = true;
    }

    void DynamicClipGenerator::update(const ::hkbContext& context, ::hkReal timestep)
    {
        CB_ZONE(zone, "Dynamic clip update");
        if (timestep > 0.0f) {
            BlendTable&      table = Blends();
            std::scoped_lock lock(table.lock);
            if (const auto it = table.slots.find(this); it != table.slots.end()) it->second.blend.dt = timestep;
        }
        /// @note A loop shows as the playing clip's local time running backwards.
        bool looped = false;
        if (::hkbGenerator* child = currentChild(); child && context.m_character && context.m_character->m_behaviorGraph) {
            if (const ::hkbNode* clone = context.m_character->m_behaviorGraph->getOrCreateNodeClone(child)) {
                const ::hkReal t = static_cast<const ::hkbClipGenerator*>(clone)->m_localTime;
                looped           = t + 1e-4f < m_lastLocalTime;
                m_lastLocalTime  = t;
            }
        }
        const bool interruptible = m_current >= 0 && m_current < m_rows.getSize() && (m_rows[m_current].m_flags & rl::kInterruptible);
        if (!looped && !interruptible) return;
        const auto candidates = Evaluate(m_rows, context);
        if (looped) {
            if (!rl::ShouldRepick(candidates, m_current, rl::Moment::Loop)) return;
            ++m_loops;
        } else if (!rl::ShouldRepick(candidates, m_current, rl::Moment::Update)) {
            return;
        }
        const int row = rl::PickClip(candidates, rl::PickSeed(ActorId(context), m_activations, m_loops));
        switchTo(context, row >= 0 ? row : (m_fallbackClip ? kFallback : kNothing));
    }

    int DynamicClipGenerator::getMaxNumChildren(GetChildrenFlags flags)
    {
        (void)flags;
        return m_rows.getSize() + 1;
    }

    /// @note Active only: the playing clip alone, so a folder of clips never streams in all of its animations. Otherwise
    ///       every row's clip and the fallback, so the graph's walks (clip loading, linking) see them all.
    void DynamicClipGenerator::getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo)
    {
        if (flags & FLAG_ACTIVE_ONLY) {
            ::hkbGenerator* child = currentChild();
            childrenInfo.m_childInfos.setSize(child ? 1 : 0);
            if (child) childrenInfo.m_childInfos[0].m_node = child;
            return;
        }
        int n = 0;
        childrenInfo.m_childInfos.setSize(m_rows.getSize() + 1);
        for (int i = 0; i < m_rows.getSize(); ++i)
            if (m_rows[i].m_clip) childrenInfo.m_childInfos[n++].m_node = m_rows[i].m_clip;
        if (m_fallbackClip) childrenInfo.m_childInfos[n++].m_node = m_fallbackClip;
        childrenInfo.m_childInfos.setSize(n);
    }

    bool DynamicClipGenerator::isValid(::hkStringPtr& errorString) const
    {
        if (m_rows.getSize() == 0 && !m_fallbackClip) {
            errorString = "No clip: the folder holds none and there is no fallback.";
            return false;
        }
        return true;
    }

    ::hkbNode* DynamicClipGenerator::cloneNode(::hkbBehaviorGraph& rootBehavior) const
    {
        (void)rootBehavior;
        return new DynamicClipGenerator(*this);
    }

    void DynamicClipGenerator::generate(const ::hkbContext& context, const ::hkbGeneratorOutput** activeChildrenOutput,
                                        ::hkbGeneratorOutput& output, ::hkReal timeOffset)
    {
        (void)context;
        (void)timeOffset;
        if (!currentChild() || !activeChildrenOutput || !activeChildrenOutput[0]) return;
        if (&output != activeChildrenOutput[0]) ::hkbGeneratorOutputUtils::copyGeneratorOutput(*activeChildrenOutput[0], output);

        using Out = ::hkbGeneratorOutput;
        if (!output.isValid(Out::TRACK_POSE)) return;
        const Out::TrackHeader& header = output.getTrackHeader(Out::TRACK_POSE);
        constexpr int           kPlain = Out::TRACK_FLAG_ADDITIVE_POSE | Out::TRACK_FLAG_PALETTE | Out::TRACK_FLAG_SPARSE;
        const int               bones  = header.m_numData;
        BlendTable&             table  = Blends();
        std::scoped_lock        lock(table.lock);
        BlendSlot&              slot   = table.slots[this];
        slot.used                      = std::chrono::steady_clock::now();
        /// @note An activation starts over: a blend from this node's last pose before it went inactive would snap back.
        if (std::exchange(slot.restart, false) || (header.m_flags & kPlain) || bones <= 0) {
            slot.blend.Reset(0, m_current);
            if ((header.m_flags & kPlain) || bones <= 0) return;
        }
        ::hkQsTransform* out = output.accessPoseLocal();
        slot.pose.resize(static_cast<std::size_t>(bones));
        std::memcpy(slot.pose.data(), out, sizeof(loco::Bone) * static_cast<std::size_t>(bones));
        loco::DeadBlendTuning tuning;
        tuning.blendTime = m_blendDuration > 0.0f ? m_blendDuration : 0.0f;
        slot.blend.Step(slot.pose.data(), bones, m_current, false, tuning);
        std::memcpy(out, slot.pose.data(), sizeof(loco::Bone) * static_cast<std::size_t>(bones));
    }

}  // namespace CB::feature::dynamic
