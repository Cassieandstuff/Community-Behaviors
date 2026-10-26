#pragma once
// DebugFlags.h — the CANONICAL registry of Community Behaviors' runtime debug / diagnostic
// toggles. ONE place that enumerates every settings.ini flag and marker-file switch the plugin
// honors, so tooling (the Behavior Converter's Debug tab) renders them as toggles AUTOMATICALLY
// and nothing has to be listed twice.
//
// Dependency-free by design (plain data — no CommonLib, RE, or PCH), so the converter, which
// shares none of the plugin's engine bindings, can include this header directly.
//
// ADDING A FLAG: add ONE row to kFlags below, then wire its BEHAVIOR in the plugin (a flag
// inherently DOES something at runtime — that per-flag logic lives with the code it gates and
// can't be generated). The converter's Debug tab picks up the toggle with no further edits.

#include <array>
#include <string_view>

namespace CB::core::debug {

    enum class FlagKind {
        IniBool,     // a [section] key=true/false in Data/SKSE/Plugins/Community Behaviors/settings.ini
        MarkerFile,  // presence of a file under Data/community_behaviors/ (present == on)
    };

    struct Flag {
        std::string_view id;       // stable identifier (UI ##id + log key)
        FlagKind         kind;
        std::string_view section;  // ini section (IniBool); empty for MarkerFile
        std::string_view key;      // ini key (IniBool) OR marker filename (MarkerFile)
        bool             defOn;    // value when the key/marker is absent
        /// @brief defOn for a DEBUG (!NDEBUG) plugin build — differs from defOn only for a build-dependent default.
        /// @note The converter can't know which plugin build is installed, so it shows both.
        bool             defOnDebug;
        std::string_view label;    // UI label
        std::string_view help;     // one-line tooltip
    };

    // The registry. Array order == display order in the Debug tab.
    /// @note The plugin reads a flag only through FlagOn("<id>") (core/debug/Settings.h), which resolves the row's
    ///       section/key/kind/default — no reader carries its own copy of the strings.
    /// @note force_regenerate: the warm-up recompile is a pure optimization whose output persists on disk, so a
    ///       release build reuses it (fast start) while a debug build regenerates (the cache can't drift while
    ///       iterating on the converter/bundles). The shipped settings.ini leaves the key commented so the build
    ///       default applies until someone opts in.
    /// @note er_gate stays default-off in every build until the gate is verified in-engine (a fault blocks all
    ///       wildcards and T-poses every actor); unrelated to the watermark, which is always on.
    inline constexpr auto kFlags = std::to_array<Flag>({
        { "force_regenerate", FlagKind::IniBool, "Cache", "bForceRegenerate", false, true,
          "Force cache regenerate",
          "Recompile the whole behavior cache at launch instead of reusing the on-disk one. "
          "Build default differs (debug on / release off); an explicit key overrides either way." },
        { "er_gate", FlagKind::IniBool, "ERGate", "bEnable", false, false,
          "Engine Relay wildcard gate",
          "Inject BR_ERWildcardLock and gate every global wildcard on it (for Engine Relay to flip "
          "per-actor). Unproven — a broken gate blocks every wildcard and T-poses every actor." },
        { "conditions", FlagKind::IniBool, "Conditions", "bEnable", false, false,
          "OAR-parity conditions",
          "Install the conditional-setdata producer/matcher "
          "engine hooks. Unproven in a shipped build and inert until conditional replacements exist; "
          "off = no engine hooks installed." },
        { "sequencer_disable", FlagKind::MarkerFile, "", "sequencer.disable", false, false,
          "Serial animation compile",
          "Compile native animations one at a time instead of across a worker pool — an escape hatch for "
          "a machine that shows trouble with the parallel compile (its output is byte-identical)." },
        { "havok_runtime", FlagKind::IniBool, "HavokRuntime", "bEnable", true, true,
          "Havok runtime takeover",
          "Run CB's reimplemented Havok behavior runtime in place of vanilla's (per class: [HavokRuntime] "
          "<Class>=off, or sOnly=<Class>,...). Inert without community_behaviors/runtime/havok-runtime.cbimg." },
        { "havok_type_registry", FlagKind::IniBool, "HavokRuntime", "bTypeRegistry", true, true,
          "Havok type registries",
          "Put CB's type-info, vtable-class and class-name registries in place of Havok's before the game starts it. "
          "They forward every call to vanilla's code, log each registration and count the loaded objects each class "
          "finishes (reported at kDataLoaded and after each game loads). Classes in sOwnTypes (default hkbClipGenerator) "
          "get CB's copy of their vtable on every loaded and cloned object. AE only." },
        { "animation64_readers", FlagKind::IniBool, "Animation64", "bReaders", false, false,
          "Animation 64 readers",
          "Run CB's own readers of the clip's binding index (clip activate/deactivate/isValid/rebind, the offset "
          "generator's sample, the synchronized clip's sync mark and activation) from the 64-bit slot instead of the "
          "16-bit field. Same behavior while every index fits 16 bits." },
        { "animation64_loader", FlagKind::IniBool, "Animation64", "bLoader", false, false,
          "Animation 64 loader + writer",
          "Run the clip loader (AnimationFileManagerSingleton's five slots, its resolver, triple lookup and binder) and "
          "LoadClips' index stamp from the clip's 64-bit slot, and the offset generator's loader calls with it. Same "
          "behavior while every index fits 16 bits. AE only." },
        { "animation64_watch", FlagKind::IniBool, "Animation64", "bReaderWatch", false, false,
          "Animation 64 reader watch",
          "Hardware watchpoints on the 16-bit binding index of the newest activated clips, logging each instruction that "
          "touches it (exe = not yet converted). A debug probe: it overwrites every thread's debug registers." },
        { "havok_activate_counter", FlagKind::IniBool, "Debug", "bHavokActivateCounter", true, true,
          "Clip activate counter",
          "Count hkbClipGenerator::activate calls through the Havok runtime's cell table, logging at every power "
          "of two — the proof that features can override reimplemented Havok functions. Observes only." },
        { "char_contact_probe", FlagKind::IniBool, "Debug", "bCharContactProbe", false, false,
          "Player contact probe",
          "Log the player's character-controller contacts (contact normal, hit triangle face normal, welding, and "
          "Bethesda's constraint decision) each time the contact set changes. Observes only." },
        { "char_support_strict", FlagKind::IniBool, "Character", "bStrictSupport", false, false,
          "Strict ground support",
          "Only a real supporting contact (a walkable one, or a step the controller accepted) marks a proxy character "
          "supported; a rejected step or a wall no longer does (Jolt's ground rule). Unproven in game." },
        { "char_stick_to_floor", FlagKind::IniBool, "Character", "bStickToFloorOnly", false, false,
          "Stick to floor only after walking off",
          "OnGround's unsupported down-cast (4x gravity toward a floor below) runs only right after the character "
          "walked off support (Jolt's StickToFloor); any other unsupported OnGround goes straight to InAir. Unproven "
          "in game." },
        { "char_state_wrapper", FlagKind::IniBool, "Character", "bStateWrapper", false, false,
          "Character state wrappers",
          "Step 1 of the character controller spec: OnGround, Jumping and InAir run through CB's wrapper states, which "
          "forward to vanilla and log that they ran for the player. Behaviour stays vanilla. Unproven in game." },
        { "char_floor_query", FlagKind::IniBool, "Character", "bFloorQueryObserve", false, false,
          "Floor query (observe)",
          "Step 2 of the character controller spec: run the ray-footprint floor query for the player after vanilla's "
          "support check and log its verdict against vanilla's. Observes only. Unproven in game." },
        { "char_ground_states", FlagKind::IniBool, "Character", "bGroundStates", false, false,
          "Ground states in charge",
          "Steps 3, 6 and 7 of the character controller spec: for the player, CB's wrapper states run Grounded / "
          "Airborne from the floor query (landing, stick-to-floor, gravity, jump launch, air control, velocity "
          "springs). Grounded means any ground under the feet: the player lands on it and can always jump from it. How "
          "much it holds them up is its support, easing from 1 at fSlidePullStart degrees (default 5) below the max "
          "slope to 0 at fSlidePullFull degrees (default 10) past it; as it drops, a slide velocity drags the player "
          "downhill and the uphill part of their steering is cut. Rising off the ground, landing and sticking to it are "
          "judged against the slope, not the world. A jump from the ground winds up for fJumpWindup seconds (default "
          "0.033) while the player keeps moving, then launches with the whole ground velocity (the run's slope "
          "momentum kept) plus a full-height push off the ground: straight up running uphill, along the ground's "
          "normal running downhill (as Sonic jumps); walking off meanwhile launches at once. Requires bStateWrapper. "
          "Unproven in game." },
        { "char_planes", FlagKind::IniBool, "Character", "bPlanes", false, false,
          "Constraint planes",
          "Step 4 of the character controller spec: for the player, the proxy's constraint planes follow the spec's "
          "edge rule and plane parameters instead of Bethesda's per-contact step and wall tests. Unproven in game." },
        { "char_step_query", FlagKind::IniBool, "Character", "bStepQuery", false, false,
          "Step query",
          "Step 5 of the character controller spec: for the player, an up-forward-down sweep steps over obstacles "
          "within step height when the slide falls short. Unproven in game." },
        { "char_variable_jump", FlagKind::IniBool, "Character", "bVariableJump", false, false,
          "Variable jump height",
          "For the player under bGroundStates: releasing Jump early cuts the jump short. iJumpMode=1 (default) "
          "rises under fJumpReleaseGravity x gravity once released; iJumpMode=2 picks a short hop "
          "(fShortHopFraction of the full height) when released within fShortHopWindow seconds of the press, else a "
          "full hop. Holding Jump always reaches vanilla's height. Unproven in game." },
        { "camera", FlagKind::IniBool, "Camera", "bEnable", false, false,
          "CB camera",
          "CB drives the third-person camera: it keeps its own world yaw and pitch, so the body never snaps to the "
          "camera and turning the body never swings the camera; it follows a damped anchor (the player plus the "
          "Camera3rd bone's height) with vanilla's shoulder offsets, zoom and pitch zoom, then its own collision. "
          "Off while True Directional Movement or SmoothCam is loaded (they drive the same camera). Mounts, first person, killcams "
          "and furniture stay vanilla. SE and AE. Unproven in game." },
        { "char_movement_model", FlagKind::IniBool, "Character", "bMovementModel", false, false,
          "Movement model (GASP)",
          "The player moves like Unreal's Game Animation Sample character: the velocity accelerates toward the stick "
          "(560 units/s2, easing to 210 while sprinting fast), friction 5 swings it into turns and braking (1400 units/s2, "
          "350 when slowing from a faster gait) stops it, capped at the gait's own speed (every multiplier kept) times the "
          "slope factor: downhill x(1 + fSlopeSpeedDown x sin) up to 1.5 (default 1.0), uphill x(1 - fSlopeSpeedUp x sin) "
          "down to 0.7 (default 0.5), smoothed (it reads the ground states' floor, so needs bGroundStates or bPlanes). "
          "The stick is relative to CB's camera and the body turns toward where it is moving (halflife 0.1 s, at most "
          "200 deg/s in the air); while attacking, aiming, blocking or casting it faces the camera and strafes, its speed "
          "blending to MOVT's side and back speeds by direction. CB also owns sprinting: it stays on while toggled, "
          "pauses while blocking, airborne or (while strafing) moving more than 50 deg from the facing, and ends on "
          "stopping, running out of stamina or overencumbrance; you can jump while sprinting. Off under "
          "bAnimationDriven, bSpeedSynced and bIsSynced. Needs bCamera (in third person); off without it. AE only. "
          "Unproven in game." },
        { "char_sprint_free_out_of_combat", FlagKind::IniBool, "Character", "bSprintNoDrainOutOfCombat", true, true,
          "Free sprinting out of combat",
          "The player's sprint costs no stamina while out of combat; in combat it drains as vanilla's. Followers are "
          "unaffected. AE only. Unproven in game." },
        { "char_debug_hud", FlagKind::IniBool, "Character", "bDebugHud", false, false,
          "Character debug HUD",
          "Draws the player's controller state over the game: the floor-query rays (green walkable, red steep or "
          "wall, grey miss), the output, desired and slide (orange) velocity arrows, the stick's direction (yellow arrowhead) and the "
          "body's facing (white tick) on the ring (needs bMovementModel), the ring and predicted path shaded by support (green "
          "holds, orange slides, blue airborne), falls "
          "and jumps included (needs bPredictorObserve), and a text panel (mode, verdict, timers, the movement model's "
          "speed and cap, the sprint state, iState). Hidden while the game is paused." },
        { "char_predictor", FlagKind::IniBool, "Character", "bPredictorObserve", false, false,
          "Trajectory predictor (observe)",
          "Step 8 of the character controller spec: predict the player's trajectory (1 s ahead) every step and log "
          "its error against where the player actually went. Under bMovementModel it runs the movement model itself "
          "from the player's current velocity and facing, the stick held; walking off a ledge or jumping predicts the fall and where it "
          "lands. Observes only. Unproven in game." },
        { "char_motion_matching", FlagKind::IniBool, "Character", "bMotionMatching", false, false,
          "Motion matching (prototype)",
          "Needs the Motion Matching add-on (Motion Matching.hky, the clips, and Motion Matching Wiring.hky, which switches "
          "locomotion to them) and bPredictorObserve. For the player, CB searches the "
          "add-on's clips every 0.1 s for the frame whose next second best matches the predicted trajectory and the "
          "current pose, and plays it through the add-on's graph; NPCs keep vanilla locomotion. Only the databases the "
          "add-on's chooser picks are searched: each frame CB computes Unreal's Game Animation Sample inputs (movement "
          "mode, rotation mode, movement state, gait, stance, starting, pivoting, landing, turn in place, spin; "
          "iMMDatabaseLOD, default 0, as the LOD) and writes them as MM_* graph variables. Costs carry each database's "
          "additive biases (base, looping, and continuing for the playing frame); a switch happens when a candidate's "
          "biased cost is below the playing frame's, and a change of those inputs that drops the playing database "
          "switches at once. Each clip switch is dead-blended: fMMBlendTime seconds of cross-fade (default 0.2, 0 = "
          "hard cuts) from the old pose coasting at its velocity, slowing with an fMMBlendDecay halflife (default "
          "0.1 s). A pick's stretch of motion stays banned for fMMReselectTime seconds (default 1.0); a sharp change of "
          "the request searches at once. Without the add-on it does nothing. Prototype." },
        { "foot_ik", FlagKind::IniBool, "FootIk", "bEnable", false, false,
          "FABRIK foot IK",
          "CB's foot IK: every character with foot IK data (creatures and people) runs CB's driver. Vanilla still finds "
          "each foot's ground target, rolls the foot to it and sinks the whole body; CB bends the legs and the spine (the "
          "three pieces below, each on unless turned off). Logs once per creature the chains and one frame's ankle "
          "targets. Unproven in game." },
        { "foot_ik_legs", FlagKind::IniBool, "FootIk", "bLegs", true, true,
          "Foot IK: FABRIK legs",
          "With bEnable: each leg bends with FABRIK over its whole chain (a horse's shoulder blade or femur down to the "
          "hoof), every joint hinging the way the animation bends it and never through straight, the knee one way within "
          "the data's range. Off = vanilla's two-joint bend (and no spine bend, which needs these legs)." },
        { "foot_ik_body", FlagKind::IniBool, "FootIk", "bBody", true, true,
          "Foot IK: spine bend",
          "With bEnable and bLegs: a creature with a hind and a fore anchor (a horse's pelvis and ribcage) bends its spine "
          "so the fore end rises or drops by what its legs ask beyond the hind end's; each spine joint folds at most 10 "
          "degrees past the animation. Vanilla's sink keeps the whole body's height. Off = the spine as animated." },
        { "foot_ik_tilt", FlagKind::IniBool, "FootIk", "bTilt", true, true,
          "Foot IK: ground tilt",
          "With bEnable: a creature with a hind and a fore anchor tilts toward the slope between its hind and fore hooves "
          "at the graph's align gain (through vanilla's channel into the controller's pitch). Off = vanilla's tilt." },
        { "runtime_trace", FlagKind::IniBool, "Debug", "bRuntimeTrace", false, false,
          "Runtime graph-variable trace",
          "Log live behavior-graph variable values (the classes/vars a debug/*.yaml probe names via its "
          "watch: list) per-frame ON CHANGE to a greppable file." },
        { "tracy", FlagKind::IniBool, "Debug", "bTracy", false, false,
          "Tracy profiler",
          "Start the Tracy profiler client built into the plugin: zones on every hook subscriber, the warm-up and motion "
          "matching, plots, and a frame mark per present. It records only while a Tracy 0.13.1 viewer on this machine is "
          "connected. Off = no profiler thread and no socket." },
    });

}  // namespace CB::core::debug
