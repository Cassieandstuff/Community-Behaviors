#pragma once

#include <cstdint>
#include <string_view>

/// @file
/// @brief CB's own Havok type registries, standing where vanilla's would: the type-info registry (finishes every object
///        a packfile places, by class name), the vtable-class registry and the class-name registry.
/// @note  Why three globals are enough (RE'd, AE 1.6.1170): every consumer reaches the registries through
///        hkBuiltinTypeRegistry's getters, which read g_hkTypeInfoRegistry / g_hkVtableClassRegistry /
///        g_hkClassNameRegistry, or reads those globals directly (three functions), always calling through the object's
///        vtable. Havok's start-up (hkBaseSystem::init, from WinMain) creates a singleton only while its global is null,
///        then registers every built-in type into whatever the globals hold.
/// @note  Every slot forwards to vanilla's code on vanilla's own field layout, and logs. A class named in
///        [HavokRuntime] sOwnTypes (default hkbClipGenerator) is CB's: the registry hands out CB's record for it, whose
///        finish constructor runs vanilla's and then stamps CB's copy of the vtable, and a node's cloneNode slot in that
///        copy stamps it on clones too. A node's activate slot counts the calls that reach it. Every slot still runs
///        vanilla's function.
/// @warning Exe code that tests an object's vtable against vanilla's would see an owned object as another class. None
///          does for hkbClipGenerator or hkbTimerModifier (AE 1.6.1170: their vtables are only stored by constructors
///          and returned by getVtable); check before owning another class.

namespace CB::platform::classes {

    /// @brief Puts CB's registries in Havok's three registry globals ([HavokRuntime] bTypeRegistry).
    /// @pre   Called from SKSEPluginLoad, which SKSE runs before WinMain, so before Havok's start-up creates any.
    /// @note  Leaves vanilla's in charge when a global is already set, or a registry symbol has no pin on this runtime.
    void InstallTypeRegistries();

    /// @return how many times an owned node class's activate has run through CB's vtable (0 for a class CB does not own).
    /// @note  Against a count of every route (the takeover cell's), the rest ran on objects carrying vanilla's vtable:
    ///        ones vanilla's constructors built outside the packfile loader and cloneNode.
    std::uint64_t ActivationsThroughOwnedVtable(std::string_view cls);

    /// @brief Logs what the registries have seen (objects finished per class, since start-up) and whether the globals
    ///        still hold CB's. Called at kDataLoaded and after each new or loaded game.
    void ReportTypeRegistries();

}  // namespace CB::platform::classes
