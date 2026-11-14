#include "PCH.h"

#include <platform/classes/TypeRegistries.h>
#include <platform/game/Render.h>
#include <platform/hooks/Override.h>
#include <platform/profile/Profile.h>

#include <Common/Base/hkBase.h>
#include <Behavior/Behavior/Generator/Clip/hkbClipGenerator.h>

#include <atomic>
#include <cstdint>

/// @file
/// @brief The cell table's proof feature: counts hkbClipGenerator::activate calls by wrapping its cell, logging at every
///        power of two. It observes only (always calls `next`), so the game runs exactly as without it.
/// @note  With [Debug] bTracy, also every clip's activate and deactivate per frame, and the clip loader calls they make,
///        as Tracy plots; each call is the zone of its subscriber here.

namespace CB::feature {

    namespace {

        using Activate   = platform::hooks::Override<&::hkbClipGenerator::activate>;
        using Deactivate = platform::hooks::Override<&::hkbClipGenerator::deactivate>;

        /// @note Atomic: behavior graphs update on several job threads at once.
        std::atomic<std::uint64_t> g_activations{ 0 };

        void OnActivate(Activate::Next next, ::hkbClipGenerator* clip, const ::hkbContext& context)
        {
            const std::uint64_t n = g_activations.fetch_add(1, std::memory_order_relaxed) + 1;
            if ((n & (n - 1)) == 0)
                LOG_INFO("activate counter: hkbClipGenerator::activate has run {} times, {} through CB's vtable", n,
                         platform::classes::ActivationsThroughOwnedVtable("hkbClipGenerator"));
            next(clip, context);
        }

        [[maybe_unused]] const bool s_subscribed =
            Activate::Subscribe(&OnActivate, platform::hooks::Priority::Normal, "ActivateCounter", "havok_activate_counter");

        /// @brief Clip activates and deactivates since the last present, and the clip loader calls among them.
        struct Churn {
            std::atomic<std::uint64_t> activates{ 0 }, deactivates{ 0 }, queues{ 0 }, unloads{ 0 };
        };
        Churn g_churn;

        /// @note m_userData is the clip's load state as its activate and deactivate test it (vanilla's, and Animation 64's
        ///       copies in Readers.cpp): 0 at activate queues its animation with the clip loader, nonzero at deactivate
        ///       unloads it.
        /// @note First: the outermost subscriber, so its zone spans the whole call, the clip loader included.
        void OnChurnActivate(Activate::Next next, ::hkbClipGenerator* clip, const ::hkbContext& context)
        {
            g_churn.activates.fetch_add(1, std::memory_order_relaxed);
            if (clip->m_userData == 0) g_churn.queues.fetch_add(1, std::memory_order_relaxed);
            next(clip, context);
        }

        void OnChurnDeactivate(Deactivate::Next next, ::hkbClipGenerator* clip, const ::hkbContext& context)
        {
            g_churn.deactivates.fetch_add(1, std::memory_order_relaxed);
            if (clip->m_userData != 0) g_churn.unloads.fetch_add(1, std::memory_order_relaxed);
            next(clip, context);
        }

        void OnChurnPresent(platform::game::PresentCall::Next next, std::uint32_t arg)
        {
            next(arg);
            CB_PLOT("Clip activates / frame", g_churn.activates.exchange(0, std::memory_order_relaxed));
            CB_PLOT("Clip deactivates / frame", g_churn.deactivates.exchange(0, std::memory_order_relaxed));
            CB_PLOT("Clip loader queues / frame", g_churn.queues.exchange(0, std::memory_order_relaxed));
            CB_PLOT("Clip loader unloads / frame", g_churn.unloads.exchange(0, std::memory_order_relaxed));
        }

        /// @note Under the "tracy" flag: with the profiler off these hooks are not attached at all.
        [[maybe_unused]] const bool s_churnSubscribed =
            (Activate::Subscribe(&OnChurnActivate, platform::hooks::Priority::First, "Clip activate", "tracy"),
             Deactivate::Subscribe(&OnChurnDeactivate, platform::hooks::Priority::First, "Clip deactivate", "tracy"),
             platform::game::PresentCall::Subscribe(&OnChurnPresent, platform::hooks::Priority::Normal, "Clip churn plots", "tracy"), true);

    }  // namespace

}  // namespace CB::feature
