#pragma once
// platform/hooks/factory/Install.h — the install MECHANISM for the repo's RE hook toolkit.
//
// Header-only. Wraps CommonLib's trampoline writes with a call-opcode
// sanity check and uniform logging, so every RE hook in the repo installs the same
// way. Hook BODIES stay in their plugin (they carry project logic); this is the
// shared plumbing they call. See <platform/hooks/hookslib.h> for the umbrella.
//
// Plugin-world only (needs RE / SKSE / REL).

#include <PCH.h>
#include <PluginLogger.h>

#include <cstddef>
#include <cstdint>

namespace CB::platform::hooks {

// write_call<N> detour on a call site, guarded by a 0xE8 (CALL rel32) opcode check.
// Returns the original call target's ADDRESS, or 0 if `site` is not a CALL (hook not
// installed — the caller falls back). N is the instruction length (5 for a near CALL).
template <std::size_t N, class F>
[[nodiscard]] std::uintptr_t InstallCallDetour(std::uintptr_t site, F detour,
                                               const char* label = nullptr) {
    const std::uint8_t op = *reinterpret_cast<const std::uint8_t*>(site);
    if (op != 0xE8) {
        LOG_WARN("hook: call site 0x{:X} is 0x{:02X}, not a CALL — {} not installed",
                 site, op, label ? label : "detour");
        return 0;
    }
    std::uintptr_t original = SKSE::GetTrampoline().write_call<N>(site, detour);
    if (label) LOG_INFO("hook: call detour installed at 0x{:X} ({})", site, label);
    return original;
}

} // namespace CB::platform::hooks
