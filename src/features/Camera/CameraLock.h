#pragma once

#include <features/Advanced Input/Input.h>

#include <cstdint>
#include <vector>

/// @file
/// @brief Combat target locking for CB's camera (bCamera): when it is engaged, the body strafes toward the primary
///        target, the camera sits behind the player looking where the body faces, and the hostiles fighting the player
///        are framed with them as far as the pull-back allows (the rig's Framing stage).
/// @note  Engaged by one of three modes: the POV key (a tap toggles it; held, it enters first person, as True
///        Directional Movement does), a key of the player's choosing (needs bAdvancedInput), or automatically while a
///        weapon is drawn and the player is not sprinting. Holding the look stick away for the break time ends it; it
///        stays off until the player hits a combatant or presses the key again (automatic: draws again).

namespace CB::feature::camera::lock {

    enum class Mode : std::uint8_t { Off, PovKey, CustomKey, Automatic };

    /// @brief settings.ini [Camera], edited live from the menu.
    struct Settings {
        Mode       mode            = Mode::PovKey;   ///< iLockMode: 0 off, 1 POV key, 2 custom key, 3 automatic
        int        maxTargets      = 3;              ///< iMaxTargetsToBlend: combatants framed on top of the player
        float      breakTime       = 1.0f;           ///< fLockBreakTime: seconds the look stick is held away to end the lock
        float      firstPersonHold = 0.5f;           ///< fFirstPersonHoldTime: POV key mode, seconds held to enter first person
        float      maxDistance     = 900.0f;         ///< fFramingMaxDistance: the farthest the framing pulls the boom
        float      range           = 2500.0f;        ///< fLockRange: the farthest a combatant may be to be framed
        float      fullWeightAt    = 500.0f;         ///< fFramingFullWeight: a combatant this close frames at full weight
        float      zeroWeightAt    = 1500.0f;        ///< fFramingZeroWeight: and this far at none (the primary always full)
        input::Key keyboardKey{};                    ///< iLockKey: scan code, or 256 + mouse button
        input::Key gamepadKey{ input::Device::Gamepad, 0 };   ///< iLockGamepadKey
    };

    /// @brief The settings now. Thread-safe; read from settings.ini once.
    Settings Current();
    /// @brief Use `s` from the next frame on, without saving it.
    void Set(const Settings& s);
    /// @brief Writes the settings to settings.ini [Camera]; false when the file could not be written.
    bool Save();

    /// @brief A combatant to frame: where its body is, how big, how close (for ranking).
    struct Candidate {
        RE::ActorHandle actor;
        float           x = 0.0f, y = 0.0f, z = 0.0f;
        float           radius = 50.0f;
    };

    struct Frame {
        bool                   engaged = false;
        std::vector<Candidate> candidates;   ///< the primary first, then the best-ranked; at most maxTargets
        RE::ActorHandle        primary;      ///< the target the body faces and attacks go at; empty without one
    };

    /// @brief Once per camera frame (main thread): the engagement from the mode, the lock key, the look stick
    ///        (`lookX`, `lookY`: this frame's look input) and hits, and while engaged the hostiles to frame.
    /// @note  The primary target sticks: it changes when it dies or leaves combat or range (then the best-ranked: nearest,
    ///        an actor off the player's facing counting as farther), or on a flick of the look stick (out and back within a
    ///        quarter second), which picks the nearest combatant on that side of it as seen from the camera
    ///        (`cameraYaw`). A hit only re-arms a lock the stick ended.
    Frame Step(RE::PlayerCharacter& player, float dt, float lookX, float lookY, float cameraYaw);

    /// @brief The camera's half of Primary: the framed target the body should face, or none.
    void PublishPrimary(bool have, float x, float y);

    /// @brief The world position the body strafes toward while locked; false when nothing is locked (or the camera has
    ///        not published within a quarter second). Thread-safe (the movement frame reads it on a job thread).
    bool Primary(float& x, float& y);

    /// @brief Whether the lock is engaged now (as of the last Step). Thread-safe.
    bool Engaged();

}  // namespace CB::feature::camera::lock
