#include "PCH.h"

#include "core/debug/Settings.h"

#include "core/debug/DebugFlags.h"

#include "SimpleIni.h"

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace CB::core::debug {

    namespace {

        constexpr const char* kIniPath = "Data/SKSE/Plugins/Community Behaviors/settings.ini";

        /// @brief settings.ini, parsed once (thread-safe static init).
        /// @note UTF-8 store, so a leading BOM is consumed rather than glued onto the first section name.
        ///       `loaded` false (missing/unreadable file) → every lookup yields its default, until SetValue writes one.
        struct LoadedIni {
            std::mutex  lock;   ///< every read and write: the config page writes while frame threads read
            CSimpleIniA ini{ true };
            bool        loaded = ini.LoadFile(kIniPath) >= 0;
        };

        LoadedIni& Ini()
        {
            static LoadedIni s;
            return s;
        }

        /// @brief Flags ForceOff switched off, with why. Short (a handful of rows), so a vector scanned under a lock.
        struct Forced {
            std::mutex                                       lock;
            std::vector<std::pair<std::string, std::string>> rows;   ///< (flag id, reason)
        };

        Forced& ForcedRows()
        {
            static Forced s;
            return s;
        }

        bool Write(LoadedIni& s)
        {
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::path(kIniPath).parent_path(), ec);
            if (s.ini.SaveFile(kIniPath) < 0) {
                LOG_ERROR("Community Behaviors: could not write {}.", kIniPath);
                return false;
            }
            s.loaded = true;
            return true;
        }

    }  // namespace

    bool FlagOn(std::string_view id)
    {
        const auto it = std::ranges::find(kFlags, id, &Flag::id);
        if (it == kFlags.end()) {
            LOG_ERROR("Community Behaviors: no debug flag '{}' in kFlags — reading it as off.", id);
            return false;
        }
        if (!ForcedOffReason(id).empty()) return false;
        if (it->kind == FlagKind::MarkerFile) {
            std::error_code ec;
            return std::filesystem::exists(std::filesystem::path("Data/community_behaviors") / it->key, ec);
        }
#ifdef NDEBUG
        const bool def = it->defOn;
#else
        const bool def = it->defOnDebug;
#endif
        LoadedIni&       s = Ini();
        std::scoped_lock guard(s.lock);
        if (!s.loaded) return def;
        return s.ini.GetBoolValue(std::string(it->section).c_str(), std::string(it->key).c_str(), def);
    }

    void ForceOff(std::string_view id, std::string_view reason)
    {
        if (std::ranges::find(kFlags, id, &Flag::id) == kFlags.end()) {
            LOG_ERROR("Community Behaviors: cannot force off '{}': no such debug flag in kFlags.", id);
            return;
        }
        Forced&          f = ForcedRows();
        std::scoped_lock guard(f.lock);
        const auto       it = std::ranges::find(f.rows, id, &std::pair<std::string, std::string>::first);
        if (it == f.rows.end()) f.rows.emplace_back(std::string(id), std::string(reason));
        else if (it->second.find(reason) == std::string::npos) it->second += "; " + std::string(reason);
    }

    std::string ForcedOffReason(std::string_view id)
    {
        Forced&          f = ForcedRows();
        std::scoped_lock guard(f.lock);
        const auto       it = std::ranges::find(f.rows, id, &std::pair<std::string, std::string>::first);
        return it == f.rows.end() ? std::string() : it->second;
    }

    std::string Value(std::string_view section, std::string_view key, std::string_view def)
    {
        LoadedIni&       s = Ini();
        std::scoped_lock guard(s.lock);
        if (!s.loaded) return std::string(def);
        const char* v = s.ini.GetValue(std::string(section).c_str(), std::string(key).c_str(), nullptr);
        return v ? std::string(v) : std::string(def);
    }

    bool SetValue(std::string_view section, std::string_view key, std::string_view value)
    {
        LoadedIni&       s = Ini();
        std::scoped_lock guard(s.lock);
        s.ini.SetValue(std::string(section).c_str(), std::string(key).c_str(), std::string(value).c_str());
        return Write(s);
    }

    bool RemoveValue(std::string_view section, std::string_view key)
    {
        LoadedIni&       s = Ini();
        std::scoped_lock guard(s.lock);
        if (!s.loaded) return true;
        const std::string sec(section);
        if (!s.ini.Delete(sec.c_str(), std::string(key).c_str(), true)) return true;
        return Write(s);
    }

}  // namespace CB::core::debug
