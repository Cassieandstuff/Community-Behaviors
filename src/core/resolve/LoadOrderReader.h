#pragma once

#include <hky/decompile/BehaviorDecompile.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// @file
/// @brief The read-only load-order seam runtime features subscribe to: the resolved load order's merged graphs and
///        bundle-authored animations, without reaching into the Resolver itself.
/// @note  Core owns the seam (Plugin.cpp arms it once the Resolver's Init is ready); a feature only subscribes.

namespace CB::core::resolve {

    /// @brief What a feature can read of the resolved load order.
    struct LoadOrderReader {
        /// @brief A served graph merged across the load order (Resolver::MergedGraph); false when not served.
        std::function<bool(const std::string& key, hky::decompile::ParsedHky& out)> mergedGraph;
        /// @brief A bundle-authored animation's YAML by meshes-relative path (Resolver::NativeAnimationText).
        std::function<std::optional<std::string>(std::string_view path)> nativeAnimation;
        /// @brief The behavior graphs mods add outright (Resolver::AddedBehaviorUnitKeys).
        std::function<std::vector<std::string>()> addedBehaviors;
        /// @brief Every rig unit the characters running a graph bind through (Resolver::RigUnitsOf); mergedGraph reads each.
        std::function<std::vector<std::string>(const std::string& key)> rigUnitsOf;
    };

    /// @brief Runs a batch of jobs and returns once all are done: the plugin's BELOW_NORMAL pool, or none (serial).
    using WarmUpExecutor = std::function<void(std::vector<std::function<void()>>)>;

    /// @brief Run `fn` during the startup warm-up, after the graph compile and before the warm-up finishes: the game's
    ///        first graph loads wait for it (ByteServe waits on WarmUpFinished), so what it builds is there when they do.
    /// @param fn gets the reader and the pool to run its jobs on (null: run them serially)
    /// @pre   Called before the warm-up runs (a feature's static initializer).
    void OnWarmUp(std::function<void(const LoadOrderReader&, const WarmUpExecutor*)> fn);
    /// @brief Run every OnWarmUp callback, in registration order, on the calling thread.
    /// @pre   The seam is armed (ArmLoadOrderReader). Called once, by the warm-up.
    void RunWarmUp(const WarmUpExecutor* exec);

    /// @brief Arm the seam and run every subscriber with `reader`, in subscription order, on the calling thread.
    /// @pre   The Resolver's Init is ready and did not fail. Called once.
    void ArmLoadOrderReader(LoadOrderReader reader);

    /// @brief Run `fn` once with the reader when the seam is armed: at once when it already is, else on the arming
    ///        thread. Never runs when the load order could not be read.
    /// @note  `fn` runs on CB's arm thread, ahead of the graph compile: anything slow belongs on its own thread.
    void OnLoadOrderReady(std::function<void(const LoadOrderReader&)> fn);

}  // namespace CB::core::resolve
