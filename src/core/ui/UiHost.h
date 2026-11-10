#pragma once

/// @file
/// @brief The UI host: CB's one ImGui context, drawn over the game's finished frame from the game's own present call.
///        Everything CB draws in game (the compile progress bar, the debug HUD, later prompts and windows) is a client
///        of this host and never touches the present hook or the ImGui backends itself.
/// @note  Interactive only while an active overlay says it wants input (Overlay::wantsInput): then the game is given
///        no input events (core/ui/UiInput.h), the context takes them instead, and ImGui draws a cursor. Otherwise the
///        context takes no input and shows no cursor.

namespace CB::core::ui {

    /// @brief One thing drawn on the host's context each present.
    struct Overlay {
        const char* who    = "?";
        bool (*active)()   = nullptr;   ///< render thread, every present; `draw` runs only when this returns true
        void (*draw)()     = nullptr;   ///< between ImGui::NewFrame and ImGui::Render: any ImGui call, windows and draw lists
        int         order  = 0;         ///< lower draws first (underneath); equal orders keep registration order
        bool (*wantsInput)() = nullptr; ///< null: never. True takes the input from the game. This and `active` of such an
                                        ///< overlay are asked on the main thread (the input dispatch) as well: keep both thread-safe
    };

    /// @return Whether an active overlay wants input right now. Any thread.
    bool WantsInput();

    /// @brief Adds an overlay.
    /// @pre   Static initialization or SKSEPluginLoad: the list is read without a lock once the present hook draws.
    void RegisterOverlay(const Overlay& overlay);

    /// @brief Builds ImGui from the swapchain's own device and window, so the present-call hook (attached at plugin load)
    ///        starts drawing.
    /// @return false while the swapchain is not up yet (retry later) or the present call is not hooked; true once
    ///         installed (idempotent).
    bool Install();

}  // namespace CB::core::ui
