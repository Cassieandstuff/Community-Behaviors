#pragma once

#include <tracy/Tracy.hpp>

#include <atomic>
#include <format>
#include <string>
#include <string_view>

/// @file
/// @brief CB's profiling macros over the Tracy client built into the plugin (CMakeLists.txt: tracy_client, v0.13.1).
/// @note  Off unless settings.ini [Debug] bTracy=true. On, Start() runs the profiler and a Tracy 0.13.1 viewer on this
///        machine records while it is connected. Off, each macro costs one relaxed load and a branch.
/// @warning The client is built with TRACY_MANUAL_LIFETIME: a Tracy call before StartupProfiler reaches a profiler that
///          does not exist. Use these macros, which test On() first, never Tracy's own.

namespace CB::platform::profile {

    namespace detail {

        inline std::atomic<bool> g_on{ false };

        inline void Send(const std::string& text)
        {
            TracyMessage(text.data(), text.size());
        }

    }  // namespace detail

    /// @brief Whether the profiler runs.
    [[nodiscard]] inline bool On() noexcept { return detail::g_on.load(std::memory_order_relaxed); }

    /// @brief Starts the profiler when [Debug] bTracy is on; otherwise nothing ever starts.
    /// @pre Called once, in SKSEPluginLoad, before InstallAll attaches the hooks.
    void Start();

    inline void Plot(const char* name, double value)
    {
        if (On()) TracyPlot(name, value);
    }

    inline void Frame()
    {
        if (On()) FrameMark;
    }

}  // namespace CB::platform::profile

/// @brief A zone named `name` (a string literal) from here to the end of the scope, held in `var`.
#define CB_ZONE(var, name) ZoneNamedN(var, name, ::CB::platform::profile::On())
/// @brief Attaches `text` (anything a std::string_view takes) to the zone `var`; copied, so it may be temporary.
#define CB_ZONE_TEXT(var, text)                                       \
    do {                                                              \
        const std::string_view cb_zone_text_(text);                   \
        ZoneTextV(var, cb_zone_text_.data(), cb_zone_text_.size());   \
    } while (0)
/// @brief A zone at the tracy::SourceLocationData `loc`, which must outlive every zone opened at it.
#define CB_ZONE_AT(var, loc) ::tracy::ScopedZone var(loc, TRACY_CALLSTACK, ::CB::platform::profile::On())
/// @brief One point of the plot `name`.
/// @warning Tracy keys a plot by the name's ADDRESS: pass a string literal.
#define CB_PLOT(name, value) ::CB::platform::profile::Plot(name, static_cast<double>(value))
/// @brief A timeline message, std::format'ed only while the profiler runs.
#define CB_MESSAGE(...)                                                                      \
    do {                                                                                     \
        if (::CB::platform::profile::On()) ::CB::platform::profile::detail::Send(std::format(__VA_ARGS__)); \
    } while (0)
/// @brief The end of a frame.
#define CB_FRAME() ::CB::platform::profile::Frame()
