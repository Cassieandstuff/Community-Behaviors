#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

/// @file
/// @brief The runtime takeover: every vanilla Havok function CB reimplements gets a jump at its entry to CB's code, so
///        every route into it — vtable, direct call, another plugin's call — runs CB's implementation.
/// @note Which code runs is chosen per class from a code source. `image` is the VS2015 build tools/cbmatch verified
///       against the exe (codec/image/RuntimeImage.h), relocated next to the exe at load; a plugin-compiled source
///       slots in beside it. settings.ini `[HavokRuntime]`: `bEnable` (the havok_runtime flag), `sOnly` (a comma list
///       of classes, empty = every class), `<Class>=image|off` (absent = image). Class names are compared exactly.

namespace CB::core::hooks {

    /// @brief One vanilla function a code source replaces, with its replacement ready to run.
    struct Replacement {
        std::string_view name;
        std::uint32_t    vanillaRva = 0;
        std::uint32_t    vanillaLength = 0;
        /// @brief FNV-1a 64 of vanilla's bytes as shipped: a loaded function that hashes otherwise was patched by
        ///        someone else (or is another build) and keeps vanilla.
        std::uint64_t    vanillaHash = 0;
        std::uintptr_t   entry = 0;
    };

    /// @brief Where a class's replacement code comes from.
    class ICodeSource {
    public:
        virtual ~ICodeSource() = default;
        [[nodiscard]] virtual const char* Name() const noexcept = 0;
        [[nodiscard]] virtual std::vector<std::string> Classes() const = 0;
        /// @return how many functions the source can provide, over all its classes.
        [[nodiscard]] virtual std::size_t Count() const noexcept = 0;
        /// @return `cls`'s replacements, placed and ready to run.
        /// @post The source owns the code, for the life of the process.
        [[nodiscard]] virtual std::vector<Replacement> Provide(std::string_view cls) = 0;
    };

    /// @brief Jumps every enabled class's vanilla functions to their replacements.
    /// @pre Called once, from SKSEPluginLoad, BEFORE hooks::InstallAll: CB's own entry detours then wrap the jump
    ///      (MinHook relocates it), and any plugin loading later layers on top the same way.
    void InstallTakeover();

    /// @brief Restores vanilla for any replaced function whose body another plugin patched after InstallTakeover
    ///        (a call-site hook inside it would otherwise sit in code that no longer runs). Logs the summary.
    /// @pre Called at kDataLoaded, once every plugin has installed its hooks.
    void RecheckTakeover();

}  // namespace CB::core::hooks
