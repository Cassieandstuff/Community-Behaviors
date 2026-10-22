#pragma once

#include <PCH.h>

#include <cstdint>

/// @file
/// @brief Game functions of the setdata gate (which replacement animation set an actor gets).

namespace CB::platform::game {

    /// @brief The setdata gate MATCHER: every condition producer funnels its {name, value} assignment array through
    ///        here before the evaluator picks a replacement set.
    /// @note `assignments` is a BSTArray<{BSFixedString name, int32 value}>: data @ +0x08, uint32 count @ +0x18, 0x10-byte
    ///       entries; names compare by interned pointer. The graph handed to the evaluator is NULL on this path, so a
    ///       condition can only be satisfied from this array (its graph-variable fallback is dead here). Layout
    ///       verified identical on SE 1.5.97 and AE.
    std::uint8_t SetDataGate_Matcher(void* table, void* event, void* project, std::uintptr_t assignments, void* out);

    /// @brief The weapon setdata PRODUCER: builds the iLeftHandType / iRightHandType assignments for `actor` on its
    ///        stack and calls SetDataGate_Matcher once with them.
    /// @note The only producer that receives the Actor* — the matcher never sees it, so anything that needs the actor
    ///       during matching must capture it here.
    std::uint8_t SetDataGate_ProducerWeapon(RE::Actor* actor, void* event, void* key, void* out);

}  // namespace CB::platform::game
