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
