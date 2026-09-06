#pragma once
// Hooks/factory/Install.h — the install MECHANISM for the repo's RE hook toolkit.
//
// Header-only. Wraps CommonLib's trampoline / vtable writes with a call-opcode
// sanity check and uniform logging, so every RE hook in the repo installs the same
// way. Hook BODIES stay in their plugin (they carry project logic); this is the
// shared plumbing they call. See <Hooks/hookslib.h> for the umbrella.
//
// Plugin-world only (needs RE / SKSE / REL).

#include <PCH.h>
#include <PluginLogger.h>

#include <cstddef>
#include <cstdint>

namespace hooks {

// Swap virtual function `index` in the vtable identified by `vtblId` (e.g.
// RE::VTABLE_LookHandler[0], or a REL id), returning the previous entry's ADDRESS.
// `label` is logged on install. Store the result in a REL::Relocation<Fn> directly,
// or reinterpret_cast it to a raw function pointer — same contract as write_vfunc.
template <class VtblID, class F>
[[nodiscard]] std::uintptr_t InstallVFunc(VtblID vtblId, std::size_t index, F detour,
                                          const char* label = nullptr) {
    REL::Relocation<std::uintptr_t> vtbl{ vtblId };
    std::uintptr_t original = vtbl.write_vfunc(index, detour);
    if (label) LOG_INFO("hook: vfunc[{}] swapped ({})", index, label);
    return original;
}

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

} // namespace hooks
