#include "PCH.h"

#include "core/debug/RuntimeTrace.h"

#include <havok/model/CompileTrace.h>   // trace::LoadRuntimeWatch / WatchVar (the `watch:` probe list)

#include "SimpleIni.h"                  // [Debug] bRuntimeTrace toggle (mirrors CB::debug::kFlags row)

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace CB::RuntimeTrace {

    namespace {

        enum class Kind { Int, Float, Bool };

        struct Watched {
            std::string name;
            Kind        kind = Kind::Int;
            bool        known = false;   // did the var resolve on the last sample?
            double      last  = 0.0;     // last resolved value (bool/int widened to double)
        };

        std::atomic<bool>              s_on{ false };
        std::vector<Watched>           s_watch;
        std::ofstream                  s_log;
        std::mutex                     s_mtx;
        std::chrono::steady_clock::time_point s_t0;

        Kind ParseKind(const std::string& k) {
            if (k == "float") return Kind::Float;
            if (k == "bool")  return Kind::Bool;
            return Kind::Int;   // default
        }

        // Read one graph variable off the player into a double. Returns false if it doesn't resolve
        // (no player, or the live graph lacks the variable — e.g. vanilla graph / actor unloaded).
        bool ReadVar(const Watched& w, double& out) {
            auto* pc = RE::PlayerCharacter::GetSingleton();
            if (!pc) return false;
            switch (w.kind) {
                case Kind::Float: { float f = 0.0f; if (!pc->GetGraphVariableFloat(w.name.c_str(), f)) return false; out = f; return true; }
                case Kind::Bool:  { bool  b = false; if (!pc->GetGraphVariableBool (w.name.c_str(), b)) return false; out = b ? 1.0 : 0.0; return true; }
                case Kind::Int:
                default:          { std::int32_t i = 0; if (!pc->GetGraphVariableInt(w.name.c_str(), i)) return false; out = i; return true; }
            }
        }

        std::string Fmt(Kind k, double v) {
            char buf[32];
            if (k == Kind::Float) std::snprintf(buf, sizeof buf, "%.4f", v);
            else                  std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(v));
            return buf;
        }

    }  // namespace

    void Install() {
        bool on = false;
        {
            CSimpleIniA ini;
            if (ini.LoadFile("Data/SKSE/Plugins/Community Behaviors/settings.ini") >= 0)
                on = ini.GetBoolValue("Debug", "bRuntimeTrace", false);
        }
        if (!on) return;

        std::string warn;
        const auto entries = havok::model::trace::LoadRuntimeWatch(
            "Data/Community Behaviors/Havok/core/Schema/metadata/debug", &warn);
        if (!warn.empty()) LOG_WARN("Community Behaviors: runtime-trace watch load: {}", warn);
        if (entries.empty()) {
            LOG_INFO("Community Behaviors: [Debug] bRuntimeTrace ON but no probe defines a `watch:` list — runtime trace idle.");
            return;
        }

        s_watch.clear();
        s_watch.reserve(entries.size());
        for (const auto& e : entries) s_watch.push_back(Watched{ e.var, ParseKind(e.kind), false, 0.0 });

        s_log.open("Data/community_behaviors/runtime_trace.log", std::ios::binary);
        if (!s_log) { LOG_ERROR("Community Behaviors: runtime-trace could not open Data\\community_behaviors\\runtime_trace.log"); return; }
        s_t0 = std::chrono::steady_clock::now();
        s_on.store(true);

        std::string names;
        for (const auto& w : s_watch) { if (!names.empty()) names += ", "; names += w.name; }
        LOG_INFO("Community Behaviors: RUNTIME-TRACE ON ({} var(s): {}) -> Data\\community_behaviors\\runtime_trace.log",
                 s_watch.size(), names);
    }

    bool Enabled() noexcept { return s_on.load(std::memory_order_relaxed); }

    void Sample() {
        if (!s_on.load(std::memory_order_relaxed)) return;
        std::lock_guard<std::mutex> lk(s_mtx);
        if (!s_log) return;
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - s_t0).count();
        for (auto& w : s_watch) {
            double v = 0.0;
            const bool ok = ReadVar(w, v);
            if (ok && (!w.known || v != w.last)) {
                s_log << "[RT] " << ms << ' ' << w.name << ' '
                      << (w.kind == Kind::Float ? "float" : w.kind == Kind::Bool ? "bool" : "int") << ' '
                      << (w.known ? Fmt(w.kind, w.last) : std::string("(none)"))
                      << " -> " << Fmt(w.kind, v) << '\n';
                w.known = true; w.last = v;
            } else if (!ok && w.known) {
                s_log << "[RT] " << ms << ' ' << w.name << ' '
                      << (w.kind == Kind::Float ? "float" : w.kind == Kind::Bool ? "bool" : "int") << ' '
                      << Fmt(w.kind, w.last) << " -> (n/a)\n";
                w.known = false;
            }
        }
        s_log.flush();
    }

}  // namespace CB::RuntimeTrace
