#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PCH.h — the single, unified precompiled header for Skyrim Content Tools.
//
// One file, two build worlds, auto-detected via __has_include so NO per-target
// define is required — each target simply pulls the world whose headers are on
// its include path:
//
//   • SKSE plugin world — CommonLibSSE (RE/Skyrim.h, SKSE, spdlog, CLibUtilsQTR,
//     PluginLogger). Used by Engine Relay, True Flight, Take to the Sky,
//     Dialogue Camera, Havok Injection Library, True Cinematics.
//   • Editor world — Dear ImGui + glm + nlohmann/json. Used by Scene Editor.
//
// A shared C++ STL core is always included. C# projects (HKX2E / HKBuild) have
// no C++ PCH and are unaffected.
//
// Wiring (per C++ target, once component CMakeLists are back):
//   - put the repo-root  include/  on the target's include path, and
//   - plugins keep their manual  #include "PCH.h"  (it now resolves here);
//   - the editor force-includes this file via  target_precompile_headers(...).
// ─────────────────────────────────────────────────────────────────────────────

// ── Shared STL core (both worlds) ────────────────────────────────────────────
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
#  if __has_include(<CLibUtilsQTR/StringHelpers.hpp>)
#    include "CLibUtilsQTR/StringHelpers.hpp"
#    include "CLibUtilsQTR/FormReader.hpp"
#  endif
#  if __has_include("PluginLogger.h")
#    include "PluginLogger.h"   // shared per-plugin logger — LOG_INFO/WARN/ERROR/DEBUG
#  endif
using namespace std::literals;
#endif

// ── Editor world (Dear ImGui + glm) ──────────────────────────────────────────
// Auto-selected when Dear ImGui AND glm are on the include path (the converter/editor). NOT the
// plugin: it now links a Dear ImGui + D3D11 lib for the compile progress bar (ProgressHud), so
// <imgui.h> alone is on its path — but it has no glm, and this glm-heavy block must stay off there.
#if __has_include(<imgui.h>) && __has_include(<glm/glm.hpp>)
#  if defined(_WIN32)
#    ifndef WIN32_LEAN_AND_MEAN
#      define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#      define NOMINMAX
#    endif
#    include <windows.h>
#  endif
#  ifndef GLM_ENABLE_EXPERIMENTAL
#    define GLM_ENABLE_EXPERIMENTAL
#  endif
#  include <glm/glm.hpp>
#  include <glm/gtc/matrix_transform.hpp>
#  include <glm/gtc/quaternion.hpp>
#  include <glm/gtx/quaternion.hpp>
#  include <imgui.h>
#  include <imgui_internal.h>
#  include <nlohmann/json.hpp>
#endif
