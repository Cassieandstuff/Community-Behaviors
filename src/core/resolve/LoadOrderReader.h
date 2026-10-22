#pragma once

#include <decompile/BehaviorDecompile.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>

/// @file
/// @brief The read-only load-order seam runtime features subscribe to: the resolved load order's merged graphs and
///        bundle-authored animations, without reaching into the Resolver itself.
/// @note  Core owns the seam (Plugin.cpp arms it once the Resolver's Init is ready); a feature only subscribes.

namespace CB::core::resolve {

    /// @brief What a feature can read of the resolved load order.
    struct LoadOrderReader {
        /// @brief A served graph merged across the load order (Resolver::MergedGraph); false when not served.
        std::function<bool(const std::string& key, CB::engine::decompile::ParsedHky& out)> mergedGraph;
        /// @brief A bundle-authored animation's YAML by meshes-relative path (Resolver::NativeAnimationText).
        std::function<std::optional<std::string>(std::string_view path)> nativeAnimation;
    };

    /// @brief Arm the seam and run every subscriber with `reader`, in subscription order, on the calling thread.
    /// @pre   The Resolver's Init is ready and did not fail. Called once.
    void ArmLoadOrderReader(LoadOrderReader reader);

    /// @brief Run `fn` once with the reader when the seam is armed: at once when it already is, else on the arming
    ///        thread. Never runs when the load order could not be read.
    /// @note  `fn` runs on CB's arm thread, ahead of the graph compile: anything slow belongs on its own thread.
    void OnLoadOrderReady(std::function<void(const LoadOrderReader&)> fn);

}  // namespace CB::core::resolve
