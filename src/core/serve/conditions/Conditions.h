#pragma once

/// @file
/// @brief The conditional setdata: OAR-parity gates evaluated by Animation Relay (<platform/relay/Relay.h>) at the
///        setdata matcher and fed to the engine's native setdata as {gate name -> int}.

#include <platform/relay/Relay.h>

#include <cstdint>

namespace CB::core::conditions {

    namespace relay = CB::platform::relay;

    /// @brief A live gate: while enabled, its expression's result is fed as {gateName -> int} into the matcher's
    ///        assignment array, so the engine's native setdata evaluates the gate.
    /// @note  Registration is what config compose produces; with none registered the hook is inert.
    struct ConditionInstance {
        RE::BSFixedString   gateName;            ///< interned: matched by pointer against the set's condition name
        relay::CompiledExpr expr;
        const bool*         enabled  = nullptr;  ///< the per-config toggle (toggle store); null = on
        std::uint32_t       configId = 0;        ///< identifies the source config; the pass's relay seed
    };
    void RegisterConditionInstance(ConditionInstance inst);
    void ClearConditionInstances();

    /// @brief Subscribe the producer actor-stash + the matcher-append to the setdata gate (Override on its matcher and
    ///        weapon producer, <platform/game/SetData.h>). Idempotent.
    /// @pre   Called during SKSEPluginLoad, before CB::platform::hooks::InstallAll().
    void InstallConditionHooks();

}  // namespace CB::core::conditions
