#pragma once

#include <cstdint>

namespace CB::platform::hooks::engine {

/// @brief What a registry symbol points at.
/// @note `Call` is a CALL rel32 INSIDE a function (pinned as function id + offset); the offset differs per
///       runtime because SE and AE were built by different compilers. `Tls` is a thread_local, pinned as the exe's
///       TLS template (its id) + the variable's offset into every thread's TLS block; it resolves to the template's
///       copy, which holds only the initial value.
enum class Kind : std::uint8_t { Func, Call, Data, Tls };

/// @brief One runtime's pin: an Address Library id plus a byte offset into it.
/// @invariant id == 0 means no verified equivalent on that runtime — the symbol is unavailable there and
///            must not be resolved (an id of 0 is never guessed from another runtime's id).
struct Pin {
    std::uint64_t id;
    std::uint32_t offset;
};

/// @brief An engine address CB uses that CommonLibSSE-NG does not provide, pinned per runtime.
/// @note Defined in the generated <platform/hooks/registry/EngineRegistry.gen.h> from src/platform/hooks/registry/engine.yaml.
struct Symbol {
    const char* name;
    Kind        kind;
    Pin         se;
    Pin         ae;
    Pin         vr;
};

} // namespace CB::platform::hooks::engine
