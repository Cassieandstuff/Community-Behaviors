#pragma once

// Community Behaviors — locomotion debug overlay, drawn through SKSE Menu Framework (SMF).
//
// A live on-screen readout of the player's behavior-graph locomotion variables — chiefly
// `SpeedSampled`, the blendParameter that drives the parametric forward-locomotion blend
// (MT_ForwardBlend). A HUD is the only way to watch it change WHILE walking (an SMF menu pauses
// the game).
//
// Toggle: SMF menu > "Community Behaviors" > "Locomotion Debug" carries a checkbox that shows/hides
// the HUD (default OFF). The HUD is an always-registered SMF HudElement that no-ops until
// enabled. SMF owns ImGui + the present hook; we resolve its `ig*` C exports ourselves at
// kDataLoaded (CB loads before SMF alphabetically, so the header's cached module handle is
// null). Silent no-op if SMF isn't installed. Independent of the compile progress bar (ProgressHud),
// which carries its own ImGui and present-call hook.
namespace CB::core::debug {

    // Resolve SMF's exports, register the HUD element + the toggle menu section. Call once at
    // kDataLoaded. No-op if SMF is absent or missing exports.
    void Install();

}  // namespace CB::core::debug
