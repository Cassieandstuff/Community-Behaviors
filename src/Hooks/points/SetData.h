#pragma once

#include <Hooks/points/HookPoint.h>

#include <cstdint>

namespace CB::core::hooks {

/// @brief The setdata gate MATCHER: every condition producer funnels its {name, value} assignment array
///        through here before the evaluator picks a replacement set.
/// @note `assignments` is a BSTArray<{BSFixedString name, int32 value}>: data @ +0x08, uint32 count @ +0x18,
///       0x10-byte entries; names compare by interned pointer. The graph handed to the evaluator is NULL on
///       this path, so a condition can only be satisfied from this array (its graph-variable fallback is dead
///       here). Layout verified identical on SE 1.5.97 and AE.
struct SetDataGateMatcher : HookPoint<SetDataGateMatcher,
    std::uint8_t(void* table, void* event, void* project, std::uintptr_t assignments, void* out)> {
    static constexpr const char*           name = "SetDataGateMatcher";
    static constexpr Seam                  seam = Seam::Entry;
    static constexpr const engine::Symbol& symbol = engine::SetDataGate_Matcher;
};

/// @brief The weapon setdata PRODUCER: builds the iLeftHandType / iRightHandType assignments for `actor` on
///        its stack and calls SetDataGateMatcher once with them.
/// @note The only producer that receives the Actor* — the matcher never sees it, so anything that needs the
///       actor during matching must capture it here.
struct SetDataGateProducerWeapon : HookPoint<SetDataGateProducerWeapon,
    std::uint8_t(RE::Actor* actor, void* event, void* key, void* out)> {
    static constexpr const char*           name = "SetDataGateProducerWeapon";
    static constexpr Seam                  seam = Seam::Entry;
    static constexpr const engine::Symbol& symbol = engine::SetDataGate_ProducerWeapon;
};

}  // namespace CB::core::hooks
