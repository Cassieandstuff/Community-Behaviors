#pragma once

#include <platform/hooks/Chain.h>
#include <platform/hooks/registry/Registry.h>
#include <platform/hooks/registry/SchemaIdentity.gen.h>
#include <platform/hooks/takeover/Takeover.h>

#include <cstdint>
#include <string>

/// @file
/// @brief Override<&fn>: subscribe to a declared function, named by its own declaration — a havok/ function or a
///        game function declared in platform/game.
/// @note Where it attaches is decided at install: the takeover's cell when the runtime image runs the function,
///       otherwise an entry detour on vanilla's address (engine.yaml). Subscribers are the same either way, and a
///       havok/ class the takeover leaves to vanilla (settings, another plugin's patch) keeps its overrides.
/// @note The signature comes from the declaration (decltype of the pointer), so it cannot drift from the header;
///       the identity (decorated name, registry pin, the image's `this`) comes from engine.yaml through the
///       generated Identity<&fn>. A function engine.yaml has no `mangled` for has no Identity: it fails to compile.
/// @note Calling the function needs nothing from here: call it as declared. The schema link shim binds the call to
///       vanilla's entry, which runs whatever is attached there.

namespace CB::platform::hooks {

    namespace detail {

        /// @brief A declared function's type as a chain signature: `this` first, typed `This` (the image's view).
        template <class F, class This>
        struct CellSignature;

        template <class R, class C, class... A, class This>
        struct CellSignature<R (C::*)(A...), This> {
            using type = R(This*, A...);
        };

        template <class R, class C, class... A, class This>
        struct CellSignature<R (C::*)(A...) const, This> {
            using type = R(const This*, A...);
        };

        template <class R, class... A, class This>
        struct CellSignature<R (*)(A...), This> {
            using type = R(A...);
        };

    }  // namespace detail

    /// @note `This` is the base subobject the image passes when the function is a virtual a non-primary base
    ///       introduced (engine.yaml `this:`), so a subscriber's first parameter is what it really receives.
    template <auto F>
    struct Override : Chain<Override<F>, typename detail::CellSignature<decltype(F), typename Identity<F>::This>::type> {
        static constexpr const char* name = Identity<F>::name;

        /// @note When the plugin ships its own body of the function, its other shipped bodies call that body directly,
        ///       past vanilla's entry and the cell, so the chain also sits on the body itself: every route runs the
        ///       subscribers once. (bhkCharacterController::UpdatePhysics, shipped, called CB's getState body directly
        ///       and skipped the state wrapper's Override on vanilla's entry.)
        static bool Attach(void* detour, void** original, std::string& why, const char*& how)
        {
            std::string          cellWhy;
            const std::uintptr_t body = PluginBody(Identity<F>::mangled);
            if (std::uintptr_t* cell = FindCell(Identity<F>::mangled, &cellWhy)) {
                if (body) {
                    /// @note The cell then runs the chain, whose original is the body's trampoline; the body's own
                    ///       detour runs the chain for the plugin's direct calls.
                    if (!detail::AttachEntry(body, detour, original, why)) return false;
                    *cell = reinterpret_cast<std::uintptr_t>(detour);
                    how   = "the takeover's cell and CB's own body";
                    return true;
                }
                *original = reinterpret_cast<void*>(*cell);
                *cell     = reinterpret_cast<std::uintptr_t>(detour);
                how       = "the takeover's cell";
                return true;
            }
            const engine::Symbol& s = Identity<F>::symbol;
            if (!engine::Available(s)) {
                why = cellWhy.empty() ? std::string("no pin on this runtime") : cellWhy + ", and no pin on this runtime";
                return false;
            }
            if (!detail::AttachEntry(engine::Resolve(s), detour, original, why)) return false;
            how = "an entry detour";
            if (body) {
                /// @note Vanilla runs the function (the takeover left it), so the original is vanilla's; the plugin's
                ///       direct calls reach the chain through the body's detour and end in vanilla too.
                void* unused = nullptr;
                if (!detail::AttachEntry(body, detour, &unused, why)) return false;
                how = "an entry detour and CB's own body";
            }
            return true;
        }
    };

}  // namespace CB::platform::hooks
