#pragma once

#include <relay/DynamicClip.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// @file
/// @brief The scoped conditions' text form: a method chain, `equipped.right.HasKeyword("Skyrim.esm|0x0001E711")`, with
///        `!`, `&&`, `||` and parentheses. It is the same tree as the structured classes (CBScopedCondition,
///        CBConditionAnd/Or/Not), so BK shows either form and each converts to the other.
/// @note  Grammar. A chain is dot-separated steps, each `name` or `name(arguments)`; every step but the last is a scope,
///        the last is the test. Names are byte-exact, as conditions.json spells them (`equipped.right`, `HasKeyword`);
///        a dotted scope name is matched longest-first against the table. A leading `all.` or `any.` (when no scope has
///        that name) sets the quantifier. Arguments are "quoted strings" (`\"` and `\\` escape) or bare tokens
///        (numbers, `==`), kept as written; a test that compares two forms (`IsSame`) takes a chain as its one argument:
///        `IsSame(alias("MyScene.esp|0x000D62", 3))`. An empty scope chain is the character's own actor.

namespace CB::engine::relay {

    /// @brief One scope step: CBConditionScope.
    struct ConditionStep {
        std::string              kind;
        std::vector<std::string> arguments;
    };

    /// @brief A condition tree node: a CBScopedCondition, or a CBConditionAnd / Or / Not over `children`.
    struct ConditionNode {
        enum class Kind { Scoped, And, Or, Not };
        Kind                       kind = Kind::Scoped;
        std::vector<ConditionStep> scope, other;
        std::string                test;
        std::vector<std::string>   arguments;
        bool                       all = false;   ///< ConditionQuantifier: QUANTIFIER_ALL
        std::vector<ConditionNode> children;      ///< Not: exactly one
    };

    /// @return the tree `text` states, type-checked against `types`; nullopt with `err` (and its offset) when it does
    ///         not parse or does not check.
    std::optional<ConditionNode> ParseConditionText(std::string_view text, const ConditionTypes& types, std::string& err);

    /// @return `node` in the text form; ParseConditionText of it gives `node` back.
    /// @note  Every argument is written quoted, so a bare `3` reads back as the same string "3".
    std::string FormatConditionText(const ConditionNode& node);

}  // namespace CB::engine::relay
