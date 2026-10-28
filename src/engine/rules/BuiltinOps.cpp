#include <rules/Rules.h>

namespace CB::engine::rules {

/// @brief Register every op the engine itself implements; OpRegistry::Instance() calls it once.
/// @note  An op of the engine is listed here, never self-registered: the engine is a static library, and a TU only a
///        static registrar points into is dropped by the linker.
void RegisterBuiltinOps(OpRegistry& reg) {
    (void)reg;
}

}  // namespace CB::engine::rules
