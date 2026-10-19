#include "PCH.h"

#include <core/hooks/takeover/Override.h>

#include <Behavior/Generator/Clip/hkbClipGenerator.h>

#include <atomic>
#include <cstdint>

/// @file
/// @brief The cell table's proof feature: counts hkbClipGenerator::activate calls by wrapping its cell, logging at every
///        power of two. It observes only (always calls `next`), so the game runs exactly as without it.

namespace CB::feature {

    namespace {

        using Activate = core::hooks::Override<&CB::schema::hkbClipGenerator::activate>;

        /// @note Atomic: behavior graphs update on several job threads at once.
        std::atomic<std::uint64_t> g_activations{ 0 };

        void OnActivate(Activate::Next next, CB::schema::hkbClipGenerator* clip, const CB::schema::hkbContext& context)
        {
            const std::uint64_t n = g_activations.fetch_add(1, std::memory_order_relaxed) + 1;
            if ((n & (n - 1)) == 0) LOG_INFO("activate counter: hkbClipGenerator::activate has run {} times", n);
            next(clip, context);
        }

        [[maybe_unused]] const bool s_subscribed =
            Activate::Subscribe(&OnActivate, core::hooks::Priority::Normal, "ActivateCounter", "havok_activate_counter");

    }  // namespace

}  // namespace CB::feature
