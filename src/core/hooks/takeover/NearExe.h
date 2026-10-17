#pragma once

#include <cstddef>

namespace CB::core::hooks {

    /// @return `size` bytes of executable memory as close above the exe's image as is free (below it if nothing above
    ///         is), or nullptr when none lies where every byte of the image stays within rel32 reach of every byte of it.
    /// @note Not an SKSE::Trampoline: that only promises ±2 GB of the end of .text, and a block near that window's edge
    ///       leaves the exe's later sections (.data, TLS slots) out of rel32 reach — 856 of 1307 image functions on the
    ///       takeover's first launch.
    /// @post Never freed: the code and cells placed there live for the process.
    [[nodiscard]] std::byte* AllocateNearExe(std::size_t size);

}  // namespace CB::core::hooks
