// Community Behaviors — standalone build-environment smoke test.
// Phase 1: prove the build environment produces a loadable SKSE plugin that logs on startup.
// Version data + entry point are defined here directly (raw exports) so the build owns its includes
// — no reliance on the helper-generated version TU. RE/Skyrim.h precedes SKSE via the force-included PCH.

#include "SKSE/SKSE.h"

#include <spdlog/sinks/basic_file_sink.h>

namespace {
    void InitLog()
    {
        auto path = SKSE::log::log_directory();
        if (!path) {
            return;
        }
        *path /= "CommunityBehaviors.log";
        auto sink   = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        auto logger = std::make_shared<spdlog::logger>("global", std::move(sink));
        logger->set_level(spdlog::level::info);
        logger->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(logger));
    }

    consteval SKSE::PluginVersionData MakeVersion() noexcept
    {
        SKSE::PluginVersionData v{};
        v.PluginVersion(REL::Version{ 0, 3, 1, 0 });
        v.PluginName("Community Behaviors");
        v.AuthorName("Cassie");
        v.UsesNoStructs();
        v.UsesAddressLibrary();
        v.MinimumRequiredXSEVersion(REL::Version{ 0, 0, 0, 0 });
        return v;
    }
}

extern "C" [[maybe_unused]] __declspec(dllexport) constinit SKSE::PluginVersionData SKSEPlugin_Version = MakeVersion();

extern "C" [[maybe_unused]] __declspec(dllexport) bool SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse);
    InitLog();
    SKSE::log::info("Community Behaviors — standalone build environment online.");
    return true;
}
