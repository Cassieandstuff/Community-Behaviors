#include "PCH.h"

#include "core/bootstrap/Compat.h"

#include "core/debug/Settings.h"

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <string>

namespace CB::core::bootstrap::compat {

    namespace {

        namespace reg = CB::engine::compat;

        constexpr const char* kPluginsDir = "Data/SKSE/Plugins";

        std::vector<reg::Finding> g_findings;

        std::mutex               g_driftLock;
        std::vector<std::string> g_drift;

        /// @note The filesystem decides what counts as the same name (a case-insensitive volume, MO2's virtual one): the
        ///       registry's spelling goes in exactly as written, nothing is folded here.
        bool ScanPresent(std::string_view dll)
        {
            std::error_code ec;
            return std::filesystem::exists(std::filesystem::path(kPluginsDir) / std::filesystem::path(std::string(dll)), ec);
        }

        bool Loaded(std::string_view dll)
        {
            return ::GetModuleHandleW(std::wstring(dll.begin(), dll.end()).c_str()) != nullptr;
        }

        bool Found(std::string_view dll)
        {
            return std::ranges::any_of(g_findings, [&](const reg::Finding& f) {
                return std::ranges::find(f.dlls, dll) != f.dlls.end();
            });
        }

    }  // namespace

    void Preflight()
    {
        g_findings = reg::Match(&ScanPresent);
        if (g_findings.empty()) {
            LOG_INFO("compat: none of the {} mods CB knows it clashes with are in {}.", reg::Entries().size(), kPluginsDir);
            return;
        }
        for (const reg::Finding& f : g_findings) {
            const reg::Entry& e = *f.entry;
            const std::string who = std::string(e.name) + " (" + std::string(f.dlls.front()) + ")";
            if (e.severity == reg::Severity::Note) LOG_INFO("compat: {} found [{}]", who, reg::SeverityName(e.severity));
            else LOG_WARN("compat: {} found [{}]", who, reg::SeverityName(e.severity));
            for (const reg::Action& a : e.actions) {
                if (a.kind == reg::Action::Kind::Notice) {
                    LOG_WARN("compat:   {}", a.text);
                } else {
                    core::debug::ForceOff(a.text, std::string(e.name) + " is installed");
                    LOG_WARN("compat:   {} is off", a.text);
                }
            }
        }
    }

    void Postflight()
    {
        std::vector<std::string> drift;
        for (const reg::Entry& e : reg::Entries()) {
            for (const std::string_view dll : e.dlls) {
                const bool found  = Found(dll);
                const bool loaded = Loaded(dll);
                if (found && !loaded)
                    drift.push_back(std::string(dll) + " is in " + kPluginsDir + " but did not load (disabled, or it failed); CB acted on it anyway.");
                else if (!found && loaded)
                    drift.push_back(std::string(dll) + " loaded but the scan of " + kPluginsDir + " did not find it; CB did NOT act on " + std::string(e.name) + ".");
            }
        }
        for (const std::string& line : drift) LOG_ERROR("compat: {}", line);
        if (drift.empty()) LOG_INFO("compat: the plugin scan and the loaded modules agree ({} finding(s)).", g_findings.size());
        std::scoped_lock lock(g_driftLock);
        g_drift = std::move(drift);
    }

    const std::vector<reg::Finding>& Findings()
    {
        return g_findings;
    }

    std::vector<std::string> Drift()
    {
        std::scoped_lock lock(g_driftLock);
        return g_drift;
    }

}  // namespace CB::core::bootstrap::compat
