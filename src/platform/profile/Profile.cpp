#include "PCH.h"

#include <platform/profile/Profile.h>

#include <core/debug/Settings.h>
#include <platform/game/Render.h>

namespace CB::platform::profile {

    namespace {

        /// @note First priority is outermost: the mark lands after every other present subscriber has drawn.
        void OnPresent(game::PresentCall::Next next, std::uint32_t arg)
        {
            next(arg);
            CB_FRAME();
        }

        [[maybe_unused]] const bool s_frameSubscribed =
            game::PresentCall::Subscribe(&OnPresent, hooks::Priority::First, "Tracy frame mark", "tracy");

    }  // namespace

    void Start()
    {
        if (!core::debug::FlagOn("tracy")) return;
        tracy::StartupProfiler();
        detail::g_on.store(true, std::memory_order_relaxed);
        LOG_INFO("profile: Tracy 0.13.1 started (localhost only) - a viewer of the same version records while connected");
    }

}  // namespace CB::platform::profile
