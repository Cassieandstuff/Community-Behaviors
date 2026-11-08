#include "PCH.h"

#include "CharDistanceMatchModifier.h"
#include "CharMotionMatching.h"

#include <locomotion/DistanceMatch.h>
#include <platform/classes/ClassRegistry.h>
#include <platform/game/Character.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <new>

namespace CB::feature::controller {

    namespace {

        namespace loco = CB::engine::locomotion;

        constexpr const char* kClassName = "CBDistanceMatchModifier";
        constexpr double      kDefaultMinRate = 0.6, kDefaultMaxRate = 5.0;   ///< Lyra's start and pivot clamps

        void Finish(void* object)
        {
            if (object) new (object) DistanceMatchModifier(::hkFinishLoadedObjectFlag{});
        }
        void Cleanup(void* object) { static_cast<DistanceMatchModifier*>(object)->~DistanceMatchModifier(); }
        /// @note As the exe's own type infos take it: a finish constructor run on a scratch buffer, its vtable read off.
        const void* Vtable()
        {
            alignas(DistanceMatchModifier) unsigned char scratch[sizeof(DistanceMatchModifier)] = {};
            Finish(scratch);
            return *reinterpret_cast<const void* const*>(scratch);
        }
        /// @note Registered whatever the flags: a behavior holding this class must load, and an unregistered class name
        ///       loads with a null vtable.
        [[maybe_unused]] const bool s_declared =
            platform::classes::Declare({ kClassName, sizeof(DistanceMatchModifier), &Finish, &Cleanup, &Vtable });

        using platform::game::ActorOf;

        loco::DistanceMarker MarkerOf(::hkInt8 source)
        {
            switch (source) {
            case DistanceMatchModifier::kPivot: return loco::DistanceMarker::Pivot;
            case DistanceMatchModifier::kStart: return loco::DistanceMarker::Start;
            default: return loco::DistanceMarker::Stop;
            }
        }

    }  // namespace

    DistanceMatchModifier::DistanceMatchModifier(::hkFinishLoadedObjectFlag flag)
    :   hkbModifier(flag),
        m_animationName(flag),
        m_timeVariable(flag)
    {
        m_userData |= 2;
    }

    DistanceMatchModifier::DistanceMatchModifier(const DistanceMatchModifier& other)
    :   hkbModifier(other),
        m_animationName(other.m_animationName),
        m_timeVariable(other.m_timeVariable),
        m_distance(other.m_distance),
        m_source(other.m_source),
        m_minRate(other.m_minRate),
        m_maxRate(other.m_maxRate)
    {
        m_userData |= 2;
    }

    const ::hkClass* DistanceMatchModifier::getClassType() const
    {
        static const auto* const cls = static_cast<const ::hkClass*>(platform::classes::ClassOf(kClassName));
        return cls;
    }

    void DistanceMatchModifier::activate(const ::hkbContext& context) { m_clipTime = 0.0f; }

    void DistanceMatchModifier::update(const ::hkbContext& context, ::hkReal timestep)
    {
        const char* name = m_animationName.cString();
        const char* var  = m_timeVariable.cString();
        RE::Actor*  actor = ActorOf(context.m_character);
        if (!name || !*name || !var || !*var || !actor || timestep <= 0.0f) return;
        const auto curve = motion::DistanceCurveOf(name, MarkerOf(m_source));
        if (!curve) return;

        const double minRate = m_minRate > 0.0f || m_maxRate > 0.0f ? m_minRate : kDefaultMinRate;
        const double maxRate = m_minRate > 0.0f || m_maxRate > 0.0f ? m_maxRate : kDefaultMaxRate;
        motion::Distances d;
        const bool        haveDistances = motion::DistancesOf(actor->GetFormID(), d);
        const double      was = m_clipTime;
        double            t   = was + timestep;   // the clock, whenever no distance applies
        if (m_source == kStart) {
            if (haveDistances) t = loco::AdvanceByDistance(*curve, was, d.speed * timestep, timestep, minRate, maxRate);
        } else {
            const float left = m_source == kDistance ? m_distance : !haveDistances ? -1.0f : m_source == kPivot ? d.toPivot : d.toStop;
            if (left >= 0.0f) {
                if (const auto at = loco::TimeAtDistance(*curve, -left); at && *at >= was) t = *at;
            } else if (m_source == kPivot && haveDistances && was >= curve->markerTime) {
                t = loco::AdvanceByDistance(*curve, was, d.speed * timestep, timestep, minRate, maxRate);
            }
        }
        const double duration = curve->Duration();
        m_clipTime            = static_cast<float>(std::clamp(t, 0.0, duration));
        actor->SetGraphVariableFloat(RE::BSFixedString(var), duration > 0.0 ? static_cast<float>(m_clipTime / duration) : 0.0f);
    }

    bool DistanceMatchModifier::isValid(::hkStringPtr& errorString) const { return true; }

    ::hkbNode* DistanceMatchModifier::cloneNode(::hkbBehaviorGraph& rootBehavior) const { return new DistanceMatchModifier(*this); }

}  // namespace CB::feature::controller
