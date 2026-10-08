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

}  // namespace CB::core::hooks
