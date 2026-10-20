#include "PCH.h"

#include <platform/hooks/Chain.h>

#include <MinHook.h>

#include <format>

namespace CB::platform::hooks {

    namespace detail {

        bool AttachEntry(std::uintptr_t at, void* detour, void** original, std::string& why)
        {
            if (MH_CreateHook(reinterpret_cast<void*>(at), detour, original) != MH_OK) {
                why = std::format("MinHook refused the entry at 0x{:X}", at);
                return false;
            }
            if (MH_EnableHook(reinterpret_cast<void*>(at)) != MH_OK) {
                why = std::format("MinHook could not enable the entry at 0x{:X}", at);
                return false;
            }
            return true;
        }

        bool AttachCall(std::uintptr_t at, void* detour, void** original, std::string& why)
        {
            const std::uint8_t op = *reinterpret_cast<const std::uint8_t*>(at);
            if (op != 0xE8) {
                why = std::format("0x{:X} holds 0x{:02X}, not a CALL", at, op);
                return false;
            }
            *original = reinterpret_cast<void*>(SKSE::GetTrampoline().write_call<5>(at, detour));
            return *original != nullptr;
        }

    }  // namespace detail

    void InstallAll()
    {
        if (detail::Closed()) return;
        detail::Closed() = true;

        const MH_STATUS mh = MH_Initialize();
        if (mh != MH_OK && mh != MH_ERROR_ALREADY_INITIALIZED)
            LOG_WARN("hooks: MH_Initialize failed ({}) — entry detours will not attach", static_cast<int>(mh));

        std::size_t live = 0;
        for (detail::ChainBase* c : detail::Wanted())
            if (c->Install()) ++live;
        LOG_INFO("hooks: {}/{} subscribed hooks attached", live, detail::Wanted().size());
    }

}  // namespace CB::platform::hooks
