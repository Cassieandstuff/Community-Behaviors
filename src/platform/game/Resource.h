#pragma once

#include <PCH.h>

#include <platform/hooks/CallSite.h>

#include <cstdint>

/// @file
/// @brief Game functions and call sites of resource loading: CB's serve seams.

namespace CB::platform::game {

    /// @brief BSResourceAssetLoader::Func3's typed-hkx resolver: every project / character / behavior graph /
    ///        skeleton .hkx resolves and reads here, by its canonical Meshes-relative path (original case).
    /// @note Reached only from Func3, so it sees typed-hkx assets only. Each asset is resolved twice — the ".hkt"
    ///       attempt, then ".hkx". Passing a different path to `next` loads those bytes under the caller's
    ///       unchanged descriptor, so the asset keeps its vanilla identity (CB's byte-serve seam).
    std::int64_t BSResourceAssetLoader_ResolveTypedHkx(const char* path, void* entry, std::uint64_t flag, void* ctx);

    /// @brief The CALL inside the animationdata loader that opens AnimationDataSingleFile.txt — the loader's FIRST
    ///        open (its two later opens read per-project listings and are not this seam).
    /// @note open(path, &out, 0, 0) → 0 on success, where `path` is the interned pointer a BSFixedString stores
    ///       (the caller loads it straight from the single-file path global). Passing another BSFixedString's
    ///       stored pointer opens that file instead — CB's adsf serve seam. Same instruction shape on SE and AE.
    using AdsfOpen = hooks::CallSite<hooks::engine::Adsf_OpenCall,
                                     std::int32_t(std::uintptr_t path, std::uintptr_t out, std::uint64_t r8, std::uint64_t r9)>;

    /// @brief The CALL inside the animation-set-data loader that opens AnimationSetDataSingleFile.txt.
    /// @note Same open function and contract as AdsfOpen (CB's asdsf serve seam).
    using AsdsfOpen = hooks::CallSite<hooks::engine::Asdsf_OpenCall,
                                      std::int32_t(std::uintptr_t path, std::uintptr_t out, std::uint64_t r8, std::uint64_t r9)>;

    /// @brief The CALL inside BSSpeedSamplerDBManager's ctor that opens SpeedDataSingleFile.txt (the path loaded from
    ///        SpeedData_SingleFilePath). Runs once, in the routine that built the adsf two calls earlier.
    /// @note Same open function shape and contract as AdsfOpen (CB's speeddata serve seam).
    using SpeedDataOpen = hooks::CallSite<hooks::engine::SpeedData_OpenCall,
                                          std::int32_t(std::uintptr_t path, std::uintptr_t out, std::uint64_t r8, std::uint64_t r9)>;

}  // namespace CB::platform::game
