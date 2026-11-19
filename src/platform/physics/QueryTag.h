#pragma once

#include <platform/physics/QueryStats.h>
#include <platform/profile/Profile.h>

#include <source_location>
#include <type_traits>
#include <utility>

/// @file
/// @brief A physics query's caller, as the game backend (GameWorld) takes it: its counters (QueryStats) and its Tracy
///        zone, both named by the tag.

namespace CB::platform::physics {

/// @brief The usual budget: a millisecond of one caller's queries in a frame is worth a log line at 60 fps.
inline constexpr QueryBudget kQueryBudget{ .msPerFrame = 1.0 };

/// @brief A caller of physics queries, named `<owner>.<what>` (`char.floor`, `camera.boom`).
/// @pre   `name` is a string literal; the tag has static storage (namespace scope or a function-local static), since
///        Tracy keeps its zone's address.
class QueryTag {
public:
    /// @param at where the tag is defined: its Tracy zone's source location.
    explicit QueryTag(const char* name, QueryBudget budget = kQueryBudget, std::source_location at = std::source_location::current())
        : m_stats(name, budget), m_zone{ name, name, at.file_name(), at.line(), 0 }
    {}
    QueryTag(const QueryTag&)            = delete;
    QueryTag& operator=(const QueryTag&) = delete;

    [[nodiscard]] QueryStats&                      Stats() { return m_stats; }
    [[nodiscard]] const ::tracy::SourceLocationData* Zone() const { return &m_zone; }

private:
    QueryStats                 m_stats;
    ::tracy::SourceLocationData m_zone;
};

/// @brief Runs `query`, a game function that queries the world inside itself (vanilla's camera caster), counted, timed
///        and zoned under `tag` like a GameWorld query; a result that converts to bool counts as its hit.
/// @note  The game takes its own lock inside: the wait is in the time, not apart from it.
template <class Fn>
auto VanillaQuery(QueryTag& tag, Fn&& query)
{
    CB_ZONE_AT(zone, tag.Zone());
    QueryTimer timer(tag.Stats());
    auto       result = std::forward<Fn>(query)();
    if constexpr (std::is_convertible_v<decltype(result), bool>) timer.Hit(static_cast<bool>(result));
    return result;
}

}  // namespace CB::platform::physics
