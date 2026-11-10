#pragma once

/// @file
/// @brief Animation Relay in the game: the engine's condition language (<relay/Relay.h>) under this namespace, the
///        game-state roster over it (Builtins.cpp), and the casts from the core's opaque pointers back to RE types.

#include <PCH.h>

#include <relay/Relay.h>

namespace CB::platform::relay {

    using namespace CB::engine::relay;

    inline RE::Actor*      ActorOf(const EvalContext& c) { return static_cast<RE::Actor*>(c.actor); }
    inline RE::TESForm*    FormOf(const Value& v) { return static_cast<RE::TESForm*>(v.form); }
    inline RE::BGSKeyword* KeywordOf(const Value& v) { return static_cast<RE::BGSKeyword*>(v.kw); }

    /// @note Why these take the base types: a Value holds a void*, so a derived pointer must convert to its TESForm* /
    ///       BGSKeyword* HERE, before the type is forgotten, or FormOf would cast back an unadjusted pointer.
    inline Value FormValue(RE::TESForm* form) { return Value::Form(form); }
    inline Value KeywordValue(RE::BGSKeyword* keyword) { return Value::Keyword(keyword); }

    /// @brief Point the core's warnings at the log and register the built-in primitive/function roster (Builtins.cpp).
    /// @note  Idempotent; installs no hook. Called at SKSEPluginLoad, before anything compiles an expression.
    void RegisterBuiltins();

}  // namespace CB::platform::relay
