#pragma once

#include <cstddef>

// Community Behaviors's startup progress overlay, drawn through SKSE Menu Framework (SMF).
//
// SMF bundles ImGui and owns the D3D present hook, exporting the raw `ig*` C functions;
// we resolve them ourselves at kDataLoaded (NOT via SMF's header static, which caches a
// null module handle because BR loads before SMF alphabetically) and register a HUD draw
// callback. If SMF isn't installed, this is a silent no-op (progress still logs). No
// present hook, no ImGui in our build — the framework owns all of that.
namespace CB::ProgressOverlay {

    // Resolve SMF's exports and register the draw callback. Call once at kDataLoaded
    // (SMF is loaded by then). No-op if SMF is absent or missing exports.
    void Install();

    // Update the bar (called from the background precompile thread). running=false hides it.
    void SetProgress(std::size_t done, std::size_t total, bool running);

}  // namespace CB::ProgressOverlay
