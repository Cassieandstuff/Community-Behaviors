#pragma once

/// @file
/// @brief The runtime SDK's hook library in one include. Three ways to hook, one subscriber chain:
///        Override<&fn> on any declared function (havok/ or platform/game: the takeover's cell when the image runs
///        it, an entry detour otherwise), CallSite<engine::X, Sig> on one call instruction and VirtualSlot<Slot, Sig>
///        on one entry of a class's vtable; Live<&global>() reads a mutable exe global. Calling a declared function needs none of it (the schema link shim binds the call).
/// @note Plugin-only (CommonLib, SKSE, MinHook). Subscribe at static initialization or in SKSEPluginLoad; the plugin
///       attaches everything once with InstallAll().

#include <platform/hooks/CallSite.h>
#include <platform/hooks/Chain.h>
#include <platform/hooks/Live.h>
#include <platform/hooks/Override.h>
#include <platform/hooks/VirtualSlot.h>
#include <platform/hooks/registry/Registry.h>
