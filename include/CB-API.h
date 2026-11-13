#pragma once

/// @file
/// @brief Community Behaviors' public facade under its original names: HKY-SDK's <hky/HKY.h>, where it now lives, with
///        cb:: = hky::api:: (cb::resolve, cb::anim, cb::mm, …). Link cb::CB-API.
/// @note  CB::engine stays an alias of hky for code written before the rename (BK); new code uses <hky/HKY.h> and hky::.
/// @note  The Skyrim.hky master regen is deliberately not here (first-party-only): tools consume the master, never
///        mint it.

#include <hky/HKY.h>

/// @note A namespace with a using-directive, not an alias: <CB-Bindings.h> opens cb::bindings, and a namespace alias
///       could not be reopened beside it.
namespace cb {
using namespace ::hky::api;
}

namespace CB {
namespace engine = ::hky;
}
