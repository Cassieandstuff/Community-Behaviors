#include "PCH.h"

#include "core/debug/Settings.h"

#include "core/debug/DebugFlags.h"

#include "SimpleIni.h"

#include <algorithm>
#include <filesystem>

namespace CB::core::debug {

    namespace {

        /// @brief settings.ini, parsed once (thread-safe static init).
        /// @note UTF-8 store, so a leading BOM is consumed rather than glued onto the first section name.
        ///       `loaded` false (missing/unreadable file) → every lookup yields its default.
        struct LoadedIni {
            CSimpleIniA ini{ true };
            bool        loaded = ini.LoadFile("Data/SKSE/Plugins/Community Behaviors/settings.ini") >= 0;
        };

        const LoadedIni& Ini()
        {
            static const LoadedIni s;
            return s;
        }

    }  // namespace

    bool FlagOn(std::string_view id)
    {
        const auto it = std::ranges::find(kFlags, id, &Flag::id);
        if (it == kFlags.end()) {
            LOG_ERROR("Community Behaviors: no debug flag '{}' in kFlags — reading it as off.", id);
            return false;
        }
        if (it->kind == FlagKind::MarkerFile) {
            std::error_code ec;
            return std::filesystem::exists(std::filesystem::path("Data/community_behaviors") / it->key, ec);
        }
#ifdef NDEBUG
        const bool def = it->defOn;
#else
        const bool def = it->defOnDebug;
#endif
        const LoadedIni& s = Ini();
        if (!s.loaded) return def;
        return s.ini.GetBoolValue(std::string(it->section).c_str(), std::string(it->key).c_str(), def);
    }

    std::string Value(std::string_view section, std::string_view key, std::string_view def)
    {
        const LoadedIni& s = Ini();
        if (!s.loaded) return std::string(def);
        const char* v = s.ini.GetValue(std::string(section).c_str(), std::string(key).c_str(), nullptr);
        return v ? std::string(v) : std::string(def);
    }

}  // namespace CB::core::debug
