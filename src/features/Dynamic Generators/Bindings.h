#pragma once

#include <Common/Base/hkBase.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

class hkbGenerator;
class hkbCharacter;

namespace SKSE { class MessagingInterface; }

/// @file
/// @brief The binding registry: which behavior a CBDynamicBehaviorReferenceGenerator runs for an actor under a key.
/// @note  Keys and paths are byte-exact and compared exactly. A binding loads its behavior at Bind, so activation never
///        loads; an unbound behavior is released once no active generator holds it.

namespace CB::feature::dynamic {

    /// @brief Bind `actor` under `key` to `behaviorPath` (project-relative), replacing any binding it had there.
    /// @return false when the key or path is empty.
    bool Bind(std::uint32_t actor, std::string_view key, std::string_view behaviorPath);
    /// @return false when `actor` had nothing bound under `key`.
    bool Unbind(std::uint32_t actor, std::string_view key);
    /// @return the path `actor` is bound to under `key`, if any.
    std::optional<std::string> BoundPath(std::uint32_t actor, std::string_view key);

    /// @brief What a generator holds while it runs a binding: the binding's generator and a ticket to release it by.
    struct Held {
        ::hkbGenerator* generator = nullptr;   ///< the bound behavior's root graph, linked into the character; null: none
        std::uint64_t   ticket    = 0;         ///< 0: nothing held
        std::string     path;                  ///< what was bound, for the log
    };
    /// @brief Hold `actor`'s binding under `key` for `character` until Release, loading it there if it is not yet.
    Held Acquire(std::uint32_t actor, std::string_view key, ::hkbCharacter* character);
    void Release(std::uint64_t ticket);

    /// @brief Take bindings from other plugins: cb::bindings messages (<CB-Bindings.h>) from any sender.
    void ListenForBindings(const SKSE::MessagingInterface& messaging);

}  // namespace CB::feature::dynamic
