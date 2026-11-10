#include "PCH.h"

#include "core/bootstrap/Compat.h"

#include "core/debug/Settings.h"

#include <platform/startup/Startup.h>

#include <string>

/// @file
/// @brief SKSE Menu Framework > Community Behaviors > Compatibility: what the compatibility registry found and did.
/// @note  Reaches SMF through its exports (as core/debug/DebugOverlay.cpp does), so no second translation unit takes the
///        header's per-TU module handle.

namespace CB::core::bootstrap::compat {

    namespace {

        using AddSect_t = void (*)(const char*, void(__stdcall*)());
        using TextU_t   = void (*)(const char*, const char*);

        TextU_t igText = nullptr;

        void Line(const std::string& text)
        {
            igText(text.c_str(), nullptr);
        }

        void __stdcall Render()
        {
            if (!igText) return;
            namespace reg = CB::engine::compat;

            const auto& findings = Findings();
            if (findings.empty()) Line("No mod CB knows it clashes with is installed.");
            for (const reg::Finding& f : findings) {
                const reg::Entry& e = *f.entry;
                Line(std::string(e.name) + "  [" + std::string(reg::SeverityName(e.severity)) + "]");
                for (const reg::Action& a : e.actions) {
                    if (a.kind == reg::Action::Kind::Notice) {
                        Line("    " + std::string(a.text));
                    } else {
                        const bool off = !core::debug::ForcedOffReason(a.text).empty();
                        Line("    " + std::string(a.text) + (off ? " is off" : " could not be switched off"));
                    }
                }
            }
            for (const std::string& line : Drift()) Line("Registry disagrees with what loaded: " + line);
        }

        void Register()
        {
            const HMODULE smf = ::GetModuleHandleW(L"SKSEMenuFramework");
            if (!smf) return;
            igText       = reinterpret_cast<TextU_t>(::GetProcAddress(smf, "igTextUnformatted"));
            auto addSect = reinterpret_cast<AddSect_t>(::GetProcAddress(smf, "AddSectionItem"));
            if (!igText || !addSect) {
                LOG_ERROR("compat: SKSE Menu Framework is missing expected exports - no compatibility page.");
                igText = nullptr;
                return;
            }
            addSect("Community Behaviors/Compatibility", &Render);
        }

        [[maybe_unused]] const bool s_registered = platform::startup::OnDataLoaded(&Register, "compat menu");

    }  // namespace

}  // namespace CB::core::bootstrap::compat
