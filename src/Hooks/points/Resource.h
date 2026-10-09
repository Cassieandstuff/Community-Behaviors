#pragma once

#include <Hooks/points/HookPoint.h>

#include <cstdint>

namespace CB::core::hooks {

/// @brief BSResourceAssetLoader::Func3's typed-hkx resolver: every project / character / behavior graph /
///        skeleton .hkx resolves and reads here, by its canonical Meshes-relative path (original case).
/// @note Reached only from Func3, so this seam sees typed-hkx assets only. Each asset is resolved twice — the
///       ".hkt" attempt, then ".hkx". Passing a different path to `next` loads those bytes under the caller's
///       unchanged descriptor, so the asset keeps its vanilla identity (CB's byte-serve seam).
struct ResolveTypedHkx : HookPoint<ResolveTypedHkx, std::int64_t(const char* path, void* entry, std::uint64_t flag, void* ctx)> {
    static constexpr const char*             name = "ResolveTypedHkx";
    static constexpr Seam                    seam = Seam::Entry;
    static constexpr const engine::Symbol&   symbol = engine::BSResourceAssetLoader_ResolveTypedHkx;
};

/// @brief The CALL inside the animationdata loader that opens AnimationDataSingleFile.txt — the loader's FIRST
///        open (its two later opens read per-project listings and are not this seam).
/// @note open(path, &out, 0, 0) → 0 on success, where `path` is the interned pointer a BSFixedString stores
///       (the caller loads it straight from the single-file path global). Passing another BSFixedString's
///       stored pointer opens that file instead — CB's adsf serve seam. Same instruction shape on SE and AE.
struct AdsfOpen : HookPoint<AdsfOpen, std::int32_t(std::uintptr_t path, std::uintptr_t out, std::uint64_t r8, std::uint64_t r9)> {
    static constexpr const char*             name = "AdsfOpen";
    static constexpr Seam                    seam = Seam::Call;
    static constexpr const engine::Symbol&   symbol = engine::Adsf_OpenCall;
};

/// @brief The CALL inside the animation-set-data loader that opens AnimationSetDataSingleFile.txt.
/// @note Same open function and contract as AdsfOpen (CB's asdsf serve seam).
struct AsdsfOpen : HookPoint<AsdsfOpen, std::int32_t(std::uintptr_t path, std::uintptr_t out, std::uint64_t r8, std::uint64_t r9)> {
    static constexpr const char*             name = "AsdsfOpen";
    static constexpr Seam                    seam = Seam::Call;
    static constexpr const engine::Symbol&   symbol = engine::Asdsf_OpenCall;
};

}  // namespace CB::core::hooks
