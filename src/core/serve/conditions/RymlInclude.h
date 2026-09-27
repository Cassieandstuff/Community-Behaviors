#pragma once
// RymlInclude.h — the ONLY way this module should pull in rapidyaml (per-module copy of the repo's
// standard workaround; see src/havok-schema/external/RymlInclude.h for the full rationale).
//
// rapidyaml's bundled c4core defines function-like C4_LIKELY/C4_UNLIKELY that expand to an illegal
// attribute position under C++20+ (this plugin compiles at C++23 / /std:c++latest). Include
// c4/language.hpp FIRST and force those macros back to the plain form before the real ryml headers
// (whose include guard then skips the buggy definition).
#include <c4/language.hpp>

#if defined(C4_UNLIKELY_IS_ATTR_)
#   undef C4_LIKELY
#   undef C4_UNLIKELY
#   define C4_LIKELY(x)   (x)
#   define C4_UNLIKELY(x) (x)
#endif

#include <c4/yml/yml.hpp>
#include <c4/yml/std/std.hpp>
