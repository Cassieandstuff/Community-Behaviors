#pragma once

#include <PCH.h>
#include <PluginLogger.h>

#include <Hooks/registry/EngineRegistry.gen.h>
#include <Hooks/registry/Symbol.h>

#include <cstddef>
#include <cstdint>

namespace CB::core::hooks::engine {

/// @return the running runtime's pin for `s`; an unrecognised runtime gets the empty pin (unavailable).
[[nodiscard]] inline const Pin& PinFor(const Symbol& s) noexcept
{
    static constexpr Pin kNone{ 0, 0 };
    switch (REL::Module::GetRuntime()) {
    case REL::Module::Runtime::AE:
        return s.ae;
    case REL::Module::Runtime::SE:
        return s.se;
    case REL::Module::Runtime::VR:
        return s.vr;
    default:
        return kNone;
    }
}

/// @return whether `s` has a verified pin on the running runtime. Never touches the Address Library.
[[nodiscard]] inline bool Available(const Symbol& s) noexcept { return PinFor(s).id != 0; }

/// @return `s`'s address in the running game, or 0 when it has no verified pin on this runtime.
/// @warning An id ABSENT from this game version's Address Library is fatal inside CommonLib (report_and_fail,
///          the standard "plugin incompatible with this version" exit). Resolve lazily — when a hook point
///          installs — never in bulk, so a symbol nothing uses can never take the game down on a new patch.
[[nodiscard]] inline std::uintptr_t Resolve(const Symbol& s)
{
    const Pin& p = PinFor(s);
    return p.id ? REL::ID(p.id).address() + p.offset : 0;
}

/// @brief Log which registry symbols are unavailable on the running runtime (no Address Library access).
inline void LogAvailability()
{
    std::size_t missing = 0;
    for (const Symbol* s : kAll) {
        if (!Available(*s)) {
            ++missing;
            LOG_INFO("engine registry: {} has no verified pin on this runtime — hook points using it stay off", s->name);
        }
    }
    LOG_INFO("engine registry: {}/{} symbols available on this runtime", kCount - missing, kCount);
}

} // namespace CB::core::hooks::engine
