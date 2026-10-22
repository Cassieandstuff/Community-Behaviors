#pragma once

#include <platform/hooks/Chain.h>
#include <platform/hooks/registry/Registry.h>

#include <string>

/// @file
/// @brief CallSite<engine::X, Sig>: subscribe to ONE call instruction, so only that caller is hooked — the seams
///        where every other caller must stay untouched (a loader's first file open, the render loop's present).
/// @note There is no declaration to name, so the identity is the registry's `call` row (function id + offset per
///       runtime) and the signature is the callee's as that caller uses it. Attached with the SKSE trampoline, so
///       SKSE::AllocTrampoline must have run.

namespace CB::platform::hooks {

    template <const engine::Symbol& S, class Sig>
    struct CallSite : Chain<CallSite<S, Sig>, Sig> {
        static_assert(S.kind == engine::Kind::Call, "a CallSite needs a `call` registry row");
        static constexpr const char* name = S.name;

        static bool Attach(void* detour, void** original, std::string& why, const char*& how)
        {
            if (!engine::Available(S)) {
                why = "no pin on this runtime";
                return false;
            }
            how = "a call detour";
            return detail::AttachCall(engine::Resolve(S), detour, original, why);
        }
    };

}  // namespace CB::platform::hooks
