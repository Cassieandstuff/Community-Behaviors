#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PCH.h — the single, unified precompiled header for Skyrim Content Tools.
//
// A shared C++ STL core is always included; the SKSE plugin world — CommonLibSSE
// (RE/Skyrim.h, SKSE, spdlog, PluginLogger) — is auto-detected via __has_include.
// ─────────────────────────────────────────────────────────────────────────────

// ── Shared STL core ──────────────────────────────────────────────────────────
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// ── SKSE plugin world (CommonLibSSE) ─────────────────────────────────────────
// Auto-selected when CommonLibSSE is on the include path.
#if __has_include(<RE/Skyrim.h>)
#  include "RE/Skyrim.h"        // CommonLib MUST precede Windows.h
#  include "SKSE/SKSE.h"
#  if __has_include(<SKSE/Impl/PCH.h>)
#    include "SKSE/Impl/PCH.h"
#  endif
#  include <Windows.h>          // AFTER CommonLib
#  include <spdlog/spdlog.h>
#  include <spdlog/sinks/basic_file_sink.h>
#  if __has_include("PluginLogger.h")
#    include "PluginLogger.h"   // shared per-plugin logger — LOG_INFO/WARN/ERROR
#  endif
using namespace std::literals;
#endif

// CB Havok-stack shared vocabulary (see src/engine/common/CorePrelude.h). The plugin includes
// the codec/compile/decompile headers, which reference CB::engine::common / CB::engine::codec names
// unqualified; forward-declare + make ambient here so those headers resolve in plugin TUs too.
namespace CB::engine { namespace common {} namespace codec {} }
using namespace CB::engine::common;
using namespace CB::engine::codec;
