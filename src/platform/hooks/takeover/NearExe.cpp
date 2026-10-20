#include "PCH.h"

#include "platform/hooks/takeover/NearExe.h"

namespace CB::platform::hooks {

    std::byte* AllocateNearExe(std::size_t size)
    {
        const std::uintptr_t exe   = REL::Module::get().base();
        const auto*          dos   = reinterpret_cast<const IMAGE_DOS_HEADER*>(exe);
        const auto*          nt    = reinterpret_cast<const IMAGE_NT_HEADERS64*>(exe + dos->e_lfanew);
        const std::uintptr_t end   = exe + nt->OptionalHeader.SizeOfImage;
        SYSTEM_INFO          si{};
        GetSystemInfo(&si);
        const std::uintptr_t gran  = si.dwAllocationGranularity;
        const std::uintptr_t reach = 0x7FFF0000ull;
        const auto           fits  = [&](std::uintptr_t a) { return a + size - exe <= reach && a + size > a; };
        const auto           tryAt = [&](std::uintptr_t a) -> std::byte* {
            MEMORY_BASIC_INFORMATION mbi{};
            if (!VirtualQuery(reinterpret_cast<void*>(a), &mbi, sizeof mbi) || mbi.State != MEM_FREE) return nullptr;
            if (reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize < a + size) return nullptr;
            return static_cast<std::byte*>(VirtualAlloc(reinterpret_cast<void*>(a), size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
        };
        for (std::uintptr_t a = (end + gran - 1) & ~(gran - 1); fits(a); a += gran)
            if (auto* p = tryAt(a)) return p;
        for (std::uintptr_t a = (exe - size) & ~(gran - 1); a > gran && end - a <= reach; a -= gran)
            if (auto* p = tryAt(a)) return p;
        return nullptr;
    }

}  // namespace CB::platform::hooks
