#include "PCH.h"
#include <Hooks/hookslib.h>   // repo RE hook toolkit — InstallCallDetour

#include "core/serve/AnimationDataServer.h"
#include "core/discover/BundleReader.h"
#include "core/bootstrap/CompileGate.h"     // CB::EnsureCompiledAndArmed — lazy compile driven by the first open

#include <codec/format/AnimationYamlLoader.h>
#include <codec/motion/RootMotion.h>            // ResolveRootMotion — a unit's actor motion (record or folded root)
#include <codec/serialization/MetaText.h>       // WriteMetaText — the schema-driven adsf writer
#include <compile/AnimDataProjection.h>         // ProjectAnimData
#include <havok-schema/HavokSchema.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace CB::core::serve {

    namespace {

        std::string ToLower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        std::string TrimLine(const std::string& line)
        {
            std::size_t b = 0, e = line.size();
            while (b < e && std::isspace(static_cast<unsigned char>(line[b]))) ++b;
            while (e > b && std::isspace(static_cast<unsigned char>(line[e - 1]))) --e;
            return line.substr(b, e - b);
        }

        bool WriteFile(const fs::path& p, const std::string& data)
        {
            std::error_code ec;
            fs::create_directories(p.parent_path(), ec);
            std::ofstream f(p, std::ios::binary | std::ios::trunc);
            if (!f) return false;
            f.write(data.data(), static_cast<std::streamsize>(data.size()));
            return static_cast<bool>(f);
        }

        /// @brief Bundle stem (lowercase) -> priority: loadorder.txt lists winners first.
        std::unordered_map<std::string, int> LoadOrder(const fs::path& ini)
        {
            std::unordered_map<std::string, int> order;
            std::ifstream f(ini);
            std::string   line;
            int           n = 0;
            while (std::getline(f, line)) {
                line = TrimLine(line);
                if (line.empty() || line[0] == '#' || line[0] == ';') continue;
                order.emplace(ToLower(line), n++);
            }
            for (auto& [_, v] : order) v = n - v;   // winner first -> highest priority
            return order;
        }

    }  // namespace

    ServeResult ServeAnimData(const fs::path& dataDir, const fs::path& loadOrderIni, const AdsfSource& adsf)
    {
        ServeResult r;
        r.attempted = true;
        std::error_code ec;
        const fs::path pluginsRoot = dataDir / "community_behaviors" / "plugins";
        const auto     order       = LoadOrder(loadOrderIni);

        /// @note Every bundle, lowest priority first (the master, then mods by load order): an animation is read from
        ///       the HIGHEST bundle that carries it.
        std::vector<std::pair<int, BundleReader>> bundles;
        if (fs::is_directory(pluginsRoot, ec))
            for (fs::directory_iterator bi(pluginsRoot, ec), bend; !ec && bi != bend; bi.increment(ec)) {
                if (ToLower(bi->path().extension().string()) != ".hky") continue;
                const std::string stem = ToLower(bi->path().stem().string());
                auto rd = BundleReader::Open(bi->path());
                if (!rd) { LOG_WARN("AnimData: cannot read bundle '{}' — skipped.", bi->path().string()); continue; }
                const auto oit = order.find(stem);
                bundles.emplace_back(stem == "skyrim" ? -1 : (oit != order.end() ? oit->second : 0), std::move(*rd));
            }
        std::stable_sort(bundles.begin(), bundles.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        if (bundles.empty() || bundles.front().first != -1) {
            r.error = "no Skyrim.hky master (its project index is the adsf's project list)";
            LOG_WARN("AnimData: {} — vanilla animation data stays.", r.error);
            return r;
        }
        r.bundles = bundles.size();

        std::string err;
        const auto  index = bundles.front().second.read("meshes/animationdatasinglefile.txt/index.yaml");
        const auto  headers = index ? animdata::ParseAnimdataIndexYaml(*index, err) : std::vector<animdata::ProjectHeader>{};
        if (headers.empty()) {
            r.error = "master carries no animationdatasinglefile.txt/index.yaml" + (err.empty() ? std::string() : " (" + err + ")");
            LOG_WARN("AnimData: {} — vanilla animation data stays.", r.error);
            return r;
        }
        r.baseProjects = headers.size();

        /// @note Animations are parsed once, in parallel (gigabytes of keyframe YAML; the engine's adsf open waits on
        ///       this), each with its actor motion resolved — its record, or extracted from a folded root.
        std::unordered_map<std::string, std::optional<CB::core::anim::AnimationDef>> anims;
        const auto load = [&](const std::string& path) -> std::optional<CB::core::anim::AnimationDef> {
            for (auto it = bundles.rbegin(); it != bundles.rend(); ++it)
                if (const auto text = it->second.read(path)) {
                    try {
                        auto def = CB::core::anim::AnimationYamlLoader::LoadFromString(*text, path);
                        def.motion = CB::core::anim::ResolveRootMotion(def);
                        return def;
                    } catch (const std::exception& e) {
                        LOG_WARN("AnimData: animation '{}' unreadable: {}", path, e.what());
                        return std::nullopt;
                    }
                }
            return std::nullopt;
        };
        animdata::ProjectionSource src;
        src.roster        = adsf.roster;
        src.clips         = adsf.clips;
        src.behaviorUnits = adsf.behaviorUnits;
        src.prefetch      = [&](const std::vector<std::string>& paths) {
            std::vector<std::optional<CB::core::anim::AnimationDef>> parsed(paths.size());
            std::atomic<std::size_t> next{ 0 };
            std::vector<std::thread> pool;
            for (unsigned t = 0; t < std::max(1u, std::thread::hardware_concurrency()); ++t)
                pool.emplace_back([&] { for (std::size_t i; (i = next++) < paths.size();) parsed[i] = load(paths[i]); });
            for (auto& t : pool) t.join();
            for (std::size_t i = 0; i < paths.size(); ++i) anims.emplace(paths[i], std::move(parsed[i]));
        };
        src.animation = [&](const std::string& path) -> const CB::core::anim::AnimationDef* {
            auto it = anims.find(path);
            if (it == anims.end()) it = anims.emplace(path, load(path)).first;
            return it->second ? &*it->second : nullptr;
        };

        const auto* reg = CB::core::schema::SharedRegistry();
        if (!reg) { r.error = "schema registry not armed"; LOG_ERROR("AnimData: {}.", r.error); return r; }
        animdata::ProjectionStats st;
        const auto t0   = std::chrono::steady_clock::now();
        const auto root = animdata::ProjectAnimData(*reg, headers, src, &st, err);
        const auto t1   = std::chrono::steady_clock::now();
        if (!root) { r.error = "projection failed: " + err; LOG_ERROR("AnimData: {}.", r.error); return r; }

        /// @note The schema writer enforces the descriptors' `framelimit` (the engine reads each project's frame
        ///       counts through a 16-bit truncation): a file it would misread is refused and vanilla stays.
        std::string text;
        if (!CB::core::codec::io::WriteMetaText(*root, text, err)) {
            r.error = err;
            LOG_ERROR("AnimData: {} — NOT serving the projected animation data (vanilla stays).", r.error);
            return r;
        }
        const fs::path cachePath = dataDir / "community_behaviors_cache" / "animationdatasinglefile.txt";
        if (!WriteFile(cachePath, text)) {
            r.error = "failed to write " + cachePath.string();
            LOG_ERROR("AnimData: {}", r.error);
            return r;
        }
        r.ok             = true;
        r.cachePath      = cachePath.string();
        r.mergedProjects = headers.size();
        const auto ms = [](auto a, auto b) { return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count(); };
        LOG_INFO("AnimData: projected {} anim project(s) — {} clip(s), {} motion(s), {} animation(s) — from {} bundle(s) in "
                 "{} ms; wrote {}.", st.projects, st.clips, st.motions, anims.size(), bundles.size(), ms(t0, t1), r.cachePath);
        if (st.unresolved)
            LOG_INFO("AnimData: {} clip(s) play an animation their character's roster lacks (no motion; e.g. {}).", st.unresolved,
                     st.unresolvedSample.empty() ? std::string() : st.unresolvedSample.front());
        return r;
    }

    // ── Deterministic redirect: hook the loader's file-open call site ─────────────
    namespace {

        // FUN_140536ec0's single-file open: open(RCX = path BSFixedString value, RDX = &out,
        // R8 = 0, R9 = 0) -> int (0 ok). Same open fn (0x140d0a100) as set-data.
        using OpenAnimDataFn = std::int32_t (*)(std::uintptr_t a_path, std::uintptr_t a_out,
                                                std::uint64_t a_r8, std::uint64_t a_r9);

        REL::Relocation<OpenAnimDataFn> _openAnimData;
        std::atomic<bool>               s_redirectActive{ false };
        RE::BSFixedString               s_cachePath;
        bool                            s_hookInstalled = false;

        std::int32_t Hook_OpenAnimData(std::uintptr_t a_path, std::uintptr_t a_out,
                                       std::uint64_t a_r8, std::uint64_t a_r9)
        {
            // First open of the collated animdata file drives the compile gate. The gate now KICKS a
            // background arm and returns at once (it no longer parks this thread on the whole Init +
            // compile — that stalled the main thread ~50s and minimized the window). We then block ONLY
            // on the adsf/setdata arm, which the ArmThread does FIRST from cache on warm — milliseconds —
            // so the redirect below is live for THIS open and the graph load ordered after us is
            // consistent. No-op after the first call.
            CB::EnsureCompiledAndArmed();
            CB::WaitAdsfArmed();

            if (s_redirectActive.load(std::memory_order_acquire)) {
                const std::uintptr_t ours = *reinterpret_cast<std::uintptr_t*>(&s_cachePath);
                if (ours) {
                    static std::atomic<bool> logged{ false };
                    bool                     expected = false;
                    if (logged.compare_exchange_strong(expected, true))
                        LOG_INFO("AnimData: intercepted the engine's animationdata open — serving BR's merged cache.");
                    return _openAnimData(ours, a_out, a_r8, a_r9);
                }
            }
            return _openAnimData(a_path, a_out, a_r8, a_r9);
        }

    }  // namespace

    bool InstallAnimDataHook()
    {
        const bool ae = REL::Module::get().version()[1] >= 6;  // 1.6.x = AE
        if (!ae) {
            LOG_WARN("AnimData: deterministic redirect hook is AE-only — SE will use the global-write fallback.");
            return false;
        }

        // RE (AE, decrypted dump): FUN_140536ec0 opens the single file via a 5-byte
        // `CALL 0x140d0a100` at 0x140536f8e (RVA 0x536F8E), right after `MOV RCX,[0x14315c918]`
        // (the single-file path global). It is the FIRST of the loader's three opens (the other
        // two load secondary per-project listing paths) — detour only this one.
        constexpr std::uintptr_t kOpenCallSiteRVA = 0x536F8E;
        const std::uintptr_t     site = REL::Module::get().base() + kOpenCallSiteRVA;

        // InstallCallDetour guards the 0xE8 (CALL) opcode; 0 => not a CALL (different
        // game version) and the caller falls back to the safe global-write path.
        const std::uintptr_t orig = CB::core::hooks::InstallCallDetour<5>(
            site, Hook_OpenAnimData, "AnimData open redirect (AE)");
        if (!orig) return false;
        _openAnimData    = orig;
        s_hookInstalled  = true;
        return true;
    }

    void ArmAnimDataRedirect(const ServeResult& result)
    {
        if (!s_hookInstalled) {
            RedirectAnimDataGlobal(result);  // deterministic hook unavailable — fall back
            return;
        }
        if (!result.ok) {
            LOG_INFO("AnimData: no merged cache this launch — animationdata redirect stays inactive.");
            return;
        }
        s_cachePath = RE::BSFixedString("community_behaviors_cache/animationdatasinglefile.txt");
        s_redirectActive.store(true, std::memory_order_release);
        LOG_INFO("AnimData: redirect armed -> \"community_behaviors_cache/animationdatasinglefile.txt\" (serves {}).",
                 result.cachePath);
    }

    void RedirectAnimDataGlobal(const ServeResult& result)
    {
        // RE (AE 1.6, decrypted dump): the animationdata loader FUN_140536ec0 does
        // `MOV RCX,[0x14315c918]; CALL <open>`, where 0x14315c918 is a global BSFixedString set
        // by static-init FUN_140090050 to "Meshes/AnimationDataSingleFile.txt". RVA = 0x315C918.
        constexpr std::uintptr_t     kSingleFileGlobalRVA = 0x315C918;
        static constexpr const char* kRedirectPath = "community_behaviors_cache/animationdatasinglefile.txt";

        const bool ae = REL::Module::get().version()[1] >= 6;
        if (!ae) {
            LOG_WARN("AnimData: global redirect is AE-only for now — SE offset not mapped; the merged "
                     "cache was written but the game will not read it on SE.");
            return;
        }

        const std::uintptr_t base   = REL::Module::get().base();
        auto&                global = *reinterpret_cast<RE::BSFixedString*>(base + kSingleFileGlobalRVA);

        const char* cur = global.c_str();
        LOG_INFO("AnimData: engine single-file path global (rva 0x{:X}) currently = \"{}\".",
                 kSingleFileGlobalRVA, (cur && *cur) ? cur : "(empty)");

        if (!result.ok) {
            LOG_INFO("AnimData: no merged cache this launch — leaving the engine path global unchanged.");
            return;
        }

        global = RE::BSFixedString(kRedirectPath);
        LOG_INFO("AnimData: redirected engine single-file path global -> \"{}\" (serves {}).",
                 kRedirectPath, result.cachePath);
    }

}  // namespace CB::core::serve
