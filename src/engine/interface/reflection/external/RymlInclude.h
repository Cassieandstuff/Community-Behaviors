#pragma once
// ============================================================================
// RymlInclude.h — the ONLY way SCT plugin code should pull in rapidyaml.
//
// Why this exists
// ---------------
// rapidyaml 0.15.2 bundles c4core v0.5.0, whose c4/language.hpp contains a bug
// that bites any C++20-or-later translation unit:
//
//     #if (__has_cpp_attribute(unlikely) >= 201803L) && (C4_CPP >= 20)
//     #   define C4_UNLIKELY_IS_ATTR_
//     #endif
//     #ifdef C4_UNLIKELY_IS_ATTR_
//     #   define C4_LIKELY(x)   (x) [[likely]]
//     #   define C4_UNLIKELY(x) (x) [[unlikely]]
//     ...
//
// c4core then USES those function-like macros inside `if(...)` conditions, e.g.
// c4/yml/error.hpp:  `if(C4_UNLIKELY(len > buf.len))`. Under C++20 that expands
// to `if((len > buf.len) [[unlikely]])` — an attribute in an illegal position —
// producing "missing ')' before attribute specifier" in error.hpp / node.hpp /
// emit.def.hpp. (The ryml *library* is built as C++17, so it never trips the
// bug; only downstream C++20+ consumers do. The editor is unaffected because it
// links an older ryml whose c4core lacks this path.)
//
// Every CommonLibSSE / SKSE plugin here compiles at C++23 (`/std:c++latest`), so
// every plugin TU that touches ryml MUST go through this header instead of
// including <c4/yml/...> directly.
//
// The fix: include c4/language.hpp FIRST, then force C4_LIKELY/C4_UNLIKELY back
// to the plain `(x)` form. language.hpp has an include guard (C4_LANGUAGE_HPP_),
// so when the real ryml headers re-include it the guard skips it and our benign
// definitions remain in force for the rest of the TU.
// ============================================================================

#include <c4/language.hpp>

#if defined(C4_UNLIKELY_IS_ATTR_)
#   undef C4_LIKELY
#   undef C4_UNLIKELY
#   define C4_LIKELY(x)   (x)
#   define C4_UNLIKELY(x) (x)
#endif

#include <c4/yml/yml.hpp>
#include <c4/yml/std/std.hpp>

#include <cstddef>
#include <stdexcept>
#include <string>

namespace CB::engine::common::ryml_errors {

[[noreturn]] inline void ThrowBasic(c4::csubstr msg, c4::yml::ErrorDataBasic const&, void*) {
    throw std::runtime_error(std::string(msg.str, msg.len));
}
[[noreturn]] inline void ThrowParse(c4::csubstr msg, c4::yml::ErrorDataParse const&, void*) {
    throw std::runtime_error(std::string(msg.str, msg.len));
}
[[noreturn]] inline void ThrowVisit(c4::csubstr msg, c4::yml::ErrorDataVisit const&, void*) {
    throw std::runtime_error(std::string(msg.str, msg.len));
}
[[noreturn]] inline void ThrowC4(const char* msg, std::size_t sz) { throw std::runtime_error(std::string(msg, sz)); }

}  // namespace CB::engine::common::ryml_errors

namespace CB::engine::common {

/// @brief Make every ryml (and c4core) error THROW std::runtime_error from here on, process-wide, once.
/// @note  ryml's default handler ABORTS: a malformed file would kill the process instead of failing its parse, which
///        no per-file try/catch can isolate. c4core's own checks take a separate path, routed here as well.
inline void EnsureRymlThrows() {
    static const bool once = [] {
        c4::yml::Callbacks cb = c4::yml::get_callbacks();
        cb.m_error_basic = &ryml_errors::ThrowBasic;
        cb.m_error_parse = &ryml_errors::ThrowParse;
        cb.m_error_visit = &ryml_errors::ThrowVisit;
        c4::yml::set_callbacks(cb);
        c4::set_error_flags(c4::ON_ERROR_CALLBACK);
        c4::set_error_callback(&ryml_errors::ThrowC4);
        return true;
    }();
    (void)once;
}

}  // namespace CB::engine::common
