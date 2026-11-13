#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// @file
/// @brief The game-free half of the dynamic generators: CBDynamicClipGenerator's pick and re-pick rules, and the scoped
///        conditions' type table (schema/metadata/conditions.json). The plugin and BK's host run the same rules, so a
///        preview and the game pick the same clip from the same seed.

namespace CB::engine::relay {

    /// @brief DynamicClipFlags (schema/metadata/enums/DynamicClipFlags.yaml).
    enum DynamicClipFlag : std::uint8_t {
        kInterruptible    = 1,
        kRedrawOnLoop     = 2,
        kKeepRandomOnLoop = 4,
    };

    /// @brief One row as the pick sees it: its selection data and whether its condition holds now.
    struct ClipCandidate {
        std::int32_t priority = 0;
        float        weight   = 1.0f;
        std::uint8_t flags    = 0;
        bool         passes   = false;
    };

    /// @return the seed of one draw: a hash of the actor, the generator's activation count and the loops since.
    /// @note  splitmix64 over the three, so neighbouring actors or activations draw unrelated numbers.
    std::uint64_t PickSeed(std::uint32_t actor, std::uint32_t activation, std::uint32_t loop);

    /// @return the highest priority among the passing rows; nullopt when none passes.
    std::optional<std::int32_t> TopPriority(std::span<const ClipCandidate> rows);

    /// @return the picked row: among the passing rows of the top priority, a draw by weight from `seed`; -1 when no row
    ///         passes (the generator plays its fallback).
    /// @note  Rows with weight <= 0 draw only when every candidate's weight is <= 0, and then evenly. Row order breaks
    ///        nothing but the draw's mapping, so the same table and seed always pick the same row.
    int PickClip(std::span<const ClipCandidate> rows, std::uint64_t seed);

    /// @brief When the generator asks whether to pick again.
    enum class Moment { Update, Loop };

    /// @return whether the playing row `current` (-1: the fallback) gives way to a new pick at `moment`.
    /// @note  Update: only an Interruptible row re-picks, and only when it no longer passes or a higher priority now
    ///        passes; a tie keeps the playing clip, so a draw never flickers. The fallback plays out as a vanilla clip
    ///        would, and gives way at its loop once a row passes.
    ///        Loop: a RedrawOnLoop row re-picks, unless KeepRandomOnLoop and it still passes at the top priority.
    bool ShouldRepick(std::span<const ClipCandidate> rows, int current, Moment moment);

    /// @brief A plugin-qualified form reference, `<plugin>|<hex local id>` (OAR's form, e.g. "Skyrim.esm|0x1E711").
    struct FormKey {
        std::string   plugin;   ///< byte-exact
        std::uint32_t id = 0;   ///< the id within the plugin, its load-order byte dropped
    };
    /// @return the parsed key; nullopt when `text` is not of that form.
    std::optional<FormKey> ParseFormKey(std::string_view text);

    /// @brief The scoped conditions' type table, read from schema/metadata/conditions.json.
    struct ConditionTypes {
        struct Type  { std::vector<std::string> is, traits; };
        struct Scope { std::string accepts, produces; std::vector<std::string> arguments; bool many = false; };
        struct Test  { std::string accepts; std::vector<std::string> arguments; bool other = false; };
        std::map<std::string, Type, std::less<>>  types;
        std::map<std::string, Scope, std::less<>> scopes;
        std::map<std::string, Test, std::less<>>  tests;
        int                                       chainLimit = 4;

        /// @return whether type `type` is `want` or derives from it, or carries the trait `want`.
        bool Satisfies(std::string_view type, std::string_view want) const;
        /// @return "" when the chain `kinds` is well-typed and `test` accepts what it produces; else why not.
        /// @note  An empty chain is the character's own actor (scope `self`).
        std::string Check(std::span<const std::string> kinds, std::string_view test) const;
    };
    /// @return the table from conditions.json's text; nullopt with `err` on a malformed table (a type, scope or test
    ///         naming a type that does not exist included).
    std::optional<ConditionTypes> ParseConditionTypes(std::string_view json, std::string& err);

    /// @brief The scope kinds and tests the plugin's evaluator implements, which conditions.json may not exceed.
    std::span<const std::string_view> EvaluatedScopes();
    std::span<const std::string_view> EvaluatedTests();

}  // namespace CB::engine::relay
