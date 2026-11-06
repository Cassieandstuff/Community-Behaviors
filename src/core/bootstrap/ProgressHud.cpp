#include "PCH.h"

#include "core/bootstrap/ProgressOverlay.h"
#include "core/ui/UiHost.h"

#include <cstdio>

#include <imgui.h>

/// @file
/// @brief The cold-compile progress bar, an overlay of the UI host (core/ui): drawn passively over the game's own frames
///        while the compile runs, so the game must be presenting meanwhile. That holds because the heavy behavior compile
///        runs on a background thread (CompileGate): the menu comes up and presents, the bar draws, and ByteServe's
///        backstop waits on the compile only when a graph is actually needed. It reads ProgressOverlay; once the compile
///        finishes (running false) it draws nothing.

namespace CB::core::bootstrap {

    namespace {

        bool Active()
        {
            std::size_t done = 0, total = 0;
            return ReadProgress(done, total);
        }

        void Draw()
        {
            std::size_t done = 0, total = 0;
            if (!ReadProgress(done, total)) return;

            /// @note A yellow fill inside a white-outlined track, in a clear panel with a white border.
            const ImVec4 kWhite { 1.00f, 1.00f, 1.00f, 1.00f };
            const ImVec4 kYellow{ 1.00f, 0.82f, 0.00f, 1.00f };
            const ImVec4 kClear { 0.00f, 0.00f, 0.00f, 0.00f };   // fully transparent panel
            const ImVec4 kTrack { 0.00f, 0.00f, 0.00f, 0.30f };   // faint dark inside the white outline

            ImGui::PushStyleColor(ImGuiCol_WindowBg,      kClear);
            ImGui::PushStyleColor(ImGuiCol_Border,        kWhite);   // window + frame outline
            ImGui::PushStyleColor(ImGuiCol_Text,          kWhite);
            ImGui::PushStyleColor(ImGuiCol_FrameBg,       kTrack);   // unfilled part of the bar
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kYellow);  // the fill
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,  2.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,    0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(18.0f, 16.0f));

            const ImGuiIO& io = ImGui::GetIO();
            ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.80f),
                                    ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::Begin("##CommunityBehaviorsPrecompile", nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs |
                         ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
            ImGui::Text("Community Behaviors  -  %s", ReadPhase());
            ImGui::Spacing();
            if (total == 0) {
                /// @note Indeterminate: a phase with no count (Init, the adsf, the features' warm-up). A negative
                ///       fraction makes ImGui animate a sweeping bar.
                ImGui::ProgressBar(-1.0f * static_cast<float>(ImGui::GetTime()),
                                   ImVec2(460.0f, 24.0f), "working\xE2\x80\xA6");
            } else {
                const float frac = static_cast<float>(done) / static_cast<float>(total);
                char label[64];
                std::snprintf(label, sizeof label, "%zu / %zu", done, total);
                ImGui::ProgressBar(frac, ImVec2(460.0f, 24.0f), label);
            }
            ImGui::End();

            ImGui::PopStyleVar(5);
            ImGui::PopStyleColor(5);
        }

        /// @note Order -1: the bar sits underneath every other overlay, as it did when it was drawn before them.
        [[maybe_unused]] const bool s_registered = [] {
            ui::RegisterOverlay({ "ProgressBar", &Active, &Draw, -1 });
            return true;
        }();

    }  // namespace

}  // namespace CB::core::bootstrap
