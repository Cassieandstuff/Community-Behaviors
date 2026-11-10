#pragma once

#include <PCH.h>

#include <cstdint>

/// @file
/// @brief Game functions on the path from the main menu (New / Continue / Load) to a running game.
/// @note Free functions with the object first: the classes are CommonLib's (RE::), which this does not re-declare.

namespace CB::platform::game {

    /// @brief The save/load manager's per-frame request pump (Main::Update, the main thread; also once as the loading
    ///        screen comes down). Pops the main-thread request queue and runs each request: the GAME LOAD itself (the
    ///        request the save read posts back, whose handler is the load's only caller), saves, autosaves.
    /// @note Every load reaches the game through here, whoever asked for it: the main menu's Continue and Load, the
    ///       console's LoadGame and the startup autoload all go through Load_Impl's asynchronous read first. Its
    ///       requests stay queued while it is not called, so skipping a frame defers them without blocking anything.
    /// @note It already defers itself natively: while the loading screen is up, and for a frame countdown.
    void BGSSaveLoadManager_ProcessEvents(RE::BGSSaveLoadManager* manager);

    /// @brief BGSSaveLoadManager::Load_Impl: starts reading `fileName` asynchronously and returns at once (true); the
    ///        load runs later from BGSSaveLoadManager_ProcessEvents.
    /// @note The main menu's loads call it on the save/load manager's own thread (a queued LoadEntry request); the
    ///       console's LoadGame calls it on the main thread.
    bool BGSSaveLoadManager_Load(RE::BGSSaveLoadManager* manager, const char* fileName, std::int32_t deviceID,
                                 std::uint32_t outputStats, bool checkForMods);

    /// @brief The main menu's "StartNewGame" delegate callback (registered in MainMenu::Accept): starts the opening
    ///        quest (MQ101), sets the player's modded flag, generates the character id and leaves the menu. Main thread.
    /// @note Reads nothing from `args` on SE 1.5.97, AE 1.6.1170 or 1.7.104, so a later call may pass any valid args.
    void MainMenu_StartNewGame(const RE::FxDelegateArgs& args);

}  // namespace CB::platform::game
