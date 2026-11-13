#pragma once

#include <cstdint>

/// @file
/// @brief CB's behavior binding registry, as another SKSE plugin reaches it: a POD message sent through SKSE's messaging
///        interface to "CommunityBehaviors". No CB type crosses the boundary and nothing links against CB.
/// @note  A binding names the behavior a CBDynamicBehaviorReferenceGenerator runs for one actor under one key. The
///        behavior loads and links when the bind message arrives, never when the generator activates, so bind before
///        raising the event that enters the slot. Strings are copied while the message is handled and compared exactly;
///        CB never changes their case.
///
///     const cb::bindings::BindMessage msg{ actor->GetFormID(), "Scene", "Behaviors\\MyMod\\Intro.hkx" };
///     SKSE::GetMessagingInterface()->Dispatch(cb::bindings::kBind, (void*)&msg, sizeof(msg), "CommunityBehaviors");

namespace cb::bindings {

    inline constexpr std::uint32_t kBind   = 0x43424264;   ///< 'CBBd': BindMessage
    inline constexpr std::uint32_t kUnbind = 0x43425562;   ///< 'CBUb': BindMessage, `behaviorPath` unread

    struct BindMessage {
        std::uint32_t actor;          ///< the actor's FormID
        const char*   key;            ///< the slot's key, as the generator's `key` spells it
        const char*   behaviorPath;   ///< project-relative, as hkbBehaviorReferenceGenerator's behaviorName spells it
    };

}  // namespace cb::bindings
