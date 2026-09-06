#pragma once

// CommonLibSSE plugin PCH. RE/Skyrim.h MUST precede any Windows.h pull-in (a CommonLib requirement),
// and force-including the SKSE API here gives EVERY TU — including the helper-generated version file
// (__<Target>Plugin.cpp) — the SKSEPluginVersion / SKSEPluginLoad macros in the right include order.
#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include <spdlog/spdlog.h>

using namespace std::literals;
