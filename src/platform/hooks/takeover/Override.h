#pragma once

#include <platform/hooks/registry/SchemaIdentity.gen.h>
#include <platform/hooks/takeover/Cell.h>

#include <cstdint>

/// @file
/// @brief Override<&fn>: subscribe to a schema function the takeover runs, named by its own declaration.
/// @note The signature comes from the declaration (decltype of the member pointer), so it cannot drift from the
///       header; the identity (decorated name, the image's `this`) comes from engine.yaml through the generated
///       Identity<&fn>. A function engine.yaml has no `mangled` for has no Identity: the Override fails to compile.
/// @note Calling the function needs nothing from here: call it as declared (`world->linearCast(...)`). The schema
///       shim binds the call to vanilla's entry, which is the takeover chain when the image owns the function.

namespace CB::platform::hooks {

    namespace detail {

        /// @brief A schema function's type as a cell signature: `this` first, typed `This` (the image's view).
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

    /// @brief The takeover cell of schema function `F`: Subscribe(fn, priority, who, flag) as any cell.
    /// @note `This` is the base subobject the image passes when the function is a virtual a non-primary base
    ///       introduced (engine.yaml `this:`), so a subscriber's first parameter is what it really receives.
    template <auto F>
    struct Override : Cell<Override<F>, typename detail::CellSignature<decltype(F), typename Identity<F>::This>::type> {
        static constexpr const char*   mangled = Identity<F>::mangled;
        static constexpr const char*   name    = Identity<F>::name;
        static constexpr std::uint64_t id      = 0;
    };

}  // namespace CB::platform::hooks
