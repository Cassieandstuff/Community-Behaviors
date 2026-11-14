#include "PCH.h"

#include "CameraZoom.h"

#include <core/debug/Settings.h>

#include <atomic>
#include <charconv>
#include <format>
#include <mutex>
#include <string>

namespace CB::feature::camera::zoom {

    namespace {

        constexpr std::string_view kSection = "Camera";

        struct Store {
            std::mutex lock;
            Settings   settings;
            bool       loaded = false;
        };
        Store& S()
        {
            static Store s;
            return s;
        }

        std::atomic<float> g_shown{ 0.0f };

        float ReadFloat(std::string_view key, float def)
        {
            const std::string text = core::debug::Value(kSection, key);
            float             v    = 0.0f;
            const auto [end, ec]   = std::from_chars(text.data(), text.data() + text.size(), v);
            return text.empty() || ec != std::errc{} ? def : v;
        }

        bool ReadBool(std::string_view key, bool def)
        {
            const std::string text = core::debug::Value(kSection, key);
            if (text == "true" || text == "1") return true;
            if (text == "false" || text == "0") return false;
            return def;
        }

        /// @pre `s.lock` held.
        void LoadOnce(Store& s)
        {
            if (s.loaded) return;
            s.loaded              = true;
            const Settings d;
            s.settings.preferred   = (std::max)(0.0f, ReadFloat("fPreferredDistance", d.preferred));
            s.settings.manual      = ReadBool("bManualZoom", d.manual);
            s.settings.firstPerson = ReadBool("bFirstPerson", d.firstPerson);
            s.settings.halflife    = (std::max)(0.0f, ReadFloat("fZoomHalflife", d.halflife));
            LOG_INFO("camera zoom: preferred {:.0f}, manual zoom {}, first person {}, halflife {:.2f} s (settings.ini [Camera])",
                     s.settings.preferred, s.settings.manual, s.settings.firstPerson, s.settings.halflife);
        }

    }  // namespace

    Settings Current()
    {
        Store&           s = S();
        std::scoped_lock guard(s.lock);
        LoadOnce(s);
        return s.settings;
    }

    void Set(const Settings& settings)
    {
        Store&           s = S();
        std::scoped_lock guard(s.lock);
        s.loaded   = true;
        s.settings = settings;
    }

    bool Save()
    {
        const Settings now = Current();
        bool           ok  = core::debug::SetValue(kSection, "fPreferredDistance", std::format("{:g}", now.preferred));
        ok = core::debug::SetValue(kSection, "bManualZoom", now.manual ? "true" : "false") && ok;
        ok = core::debug::SetValue(kSection, "bFirstPerson", now.firstPerson ? "true" : "false") && ok;
        ok = core::debug::SetValue(kSection, "fZoomHalflife", std::format("{:g}", now.halflife)) && ok;
        LOG_INFO("camera zoom: saved to settings.ini [Camera]{}", ok ? "" : " (with errors)");
        return ok;
    }

    float Shown() { return g_shown.load(std::memory_order_relaxed); }

    void PublishShown(float distance) { g_shown.store(distance, std::memory_order_relaxed); }

}  // namespace CB::feature::camera::zoom
