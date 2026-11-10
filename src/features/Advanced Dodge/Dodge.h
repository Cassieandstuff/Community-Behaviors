#pragma once

#include <features/Advanced Input/Input.h>

#include <locomotion/Dodge.h>

#include <string>

/// @file
/// @brief Advanced Dodge (bAdvancedDodge): the player's dodge input. Each input dispatch it reads Advanced Input's
///        buffer and the move input, decides whether the player asked to dodge (engine locomotion/Dodge.h), checks the
///        actor can, charges the stamina and raises the dodge in the graph. Playing it is the graph's job.
/// @note  The graph contract, names exact: CB sets `AD_Direction` (int: locomotion::DodgeDirection) and `AD_Angle`
///        (float, degrees clockwise from the facing), then raises `AD_Dodge`; while the graph's `AD_IsDodging` (bool)
///        is true CB raises nothing. Stamina is charged only when a graph answers the event.
/// @note  Settings live in settings.ini [Dodge], edited live from SKSE Menu Framework (features/Advanced Controller
///        CharConfigMenu, the one translation unit that includes the framework).

namespace CB::feature::dodge {

    struct Settings {
        CB::engine::locomotion::DodgeTrigger trigger         = CB::engine::locomotion::DodgeTrigger::SprintTap;
        float                                tapMaxHold      = 0.25f;   ///< fTapMaxHold: seconds a Sprint press may last and still dodge
        float                                doubleTapWindow = 0.25f;   ///< fDoubleTapWindow: seconds between the two pushes
        /// @brief iKeyboardKey: DirectInput scan code (keyboard) or 256 + button (mouse); 0 = none.
        input::Key                           keyboardKey{ input::Device::Keyboard, 0 };
        /// @brief iGamepadKey: XInput button mask; 0 = none.
        input::Key                           gamepadKey{ input::Device::Gamepad, 0 };
        CB::engine::locomotion::DodgeStamina stamina;
    };

    /// @brief The settings in use: settings.ini [Dodge] as read at first use, then as Set. Thread-safe.
    Settings Current();
    /// @brief Use `s` from the next input dispatch on; nothing is written until Save. Thread-safe.
    void Set(const Settings& s);
    /// @brief Write the settings in use to settings.ini [Dodge]. Thread-safe.
    bool Save();

    /// @brief Whether Sprint is the dodge's button (iTrigger 0 while the dodge input runs); then `held` says whether it
    ///        has been held past the tap time, which is the player's sprint intent in place of vanilla's toggle.
    /// @note  For CB's sprint rules (CharSprint). Thread-safe.
    bool SprintHoldIntent(bool& held);

    /// @brief One line about the latest dodge or refusal, for the config page. Thread-safe.
    std::string Status();

}  // namespace CB::feature::dodge
