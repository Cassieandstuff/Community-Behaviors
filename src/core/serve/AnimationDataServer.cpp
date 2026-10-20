#include "PCH.h"
#include <platform/game/Resource.h>     // the AdsfOpen / AsdsfOpen call sites
#include <platform/hooks/registry/Registry.h>

#include "core/serve/AnimationDataServer.h"
#include "core/discover/BundleReader.h"
#include "core/resolve/CacheKey.h"          // ReadCacheInput — OAR config.json is an input of the cached adsf
#include "core/bootstrap/CompileGate.h"     // CB::EnsureCompiledAndArmed — lazy compile driven by the first open

#include <codec/format/AnimationYamlLoader.h>
#include <codec/format/LoadOrderText.h>        // ReadLoadOrder — bundle priority (a later line wins, as everywhere)
#include <codec/resource/PathKey.h>
#include <codec/motion/RootMotion.h>            // ResolveRootMotion — a unit's actor motion (record or folded root)
#include <codec/serialization/MetaText.h>       // WriteMetaText — the schema-driven adsf writer
#include <compile/AnimDataProjection.h>         // ProjectAnimData
#include <interface/reflection/HavokSchema.h>
#include <disk/Disk.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace CB::core::serve {

    namespace {

        using CB::engine::codec::resource::HasExt;
        using CB::engine::codec::resource::PathKey;
        namespace disk = CB::engine::disk;

        /// @brief A DAR/OAR override copy of an animation: the file OAR plays in its base path's roster slot.
        struct OverrideCopy {
            std::string key;              ///< the copy's bundle key (path key)
            int         priority = 0;     ///< DAR condition number / OAR submod priority — the LOWEST is the default copy
            std::size_t bundle   = 0;     ///< index into the priority-sorted bundle list
        };

        /// @brief The base animation an override key stands in for, and its replacer priority.
        /// @note DAR `_CustomConditions/<N>/<rest>` → N. OAR `OpenAnimationReplacer/<mod>/<submod>/<rest>` → the submod's
        ///       config.json `priority`, read from the live Data folder (bundles don't carry it); a disabled submod is no
        ///       candidate. DAR's actor-specific `<plugin>/<formid>/` folders are skipped — they don't apply generally.
        /// @param keyOrig `key` as the bundle spells it: config.json is opened by that real path, never by the key.
        std::optional<std::pair<std::string, int>> OverrideBase(const std::string& key, const std::string& keyOrig,
                                                                const fs::path& dataDir,
                                                                std::unordered_map<std::string, std::optional<int>>& cfgCache)
        {
            const auto a = key.find("/animations/");
            if (a == std::string::npos) return std::nullopt;
            const std::string root = key.substr(0, a + 12);
            const std::string rest = key.substr(a + 12);
            const auto seg = [&](std::size_t from, std::string& out) -> std::size_t {
                const auto s = rest.find('/', from);
                if (s == std::string::npos) return std::string::npos;
                out = rest.substr(from, s - from);
                return s + 1;
            };
            constexpr std::string_view kDar = "dynamicanimationreplacer/_customconditions/";
            constexpr std::string_view kOar = "openanimationreplacer/";
            std::string a1, a2;
            if (rest.compare(0, kDar.size(), kDar) == 0) {
                const auto t = seg(kDar.size(), a1);
                if (t == std::string::npos || a1.empty() || a1.find_first_not_of("0123456789") != std::string::npos) return std::nullopt;
                return std::make_pair(root + rest.substr(t), std::atoi(a1.c_str()));
            }
            if (rest.compare(0, kOar.size(), kOar) == 0) {
                const auto m = seg(kOar.size(), a1);
                const auto t = m == std::string::npos ? m : seg(m, a2);
                if (t == std::string::npos) return std::nullopt;
                const std::string dir = root + rest.substr(0, t);   // ".../openanimationreplacer/<mod>/<submod>/"
                auto it = cfgCache.find(dir);
                if (it == cfgCache.end()) {
                    std::optional<int> prio = 0;
                    const std::string  dirOrig = keyOrig.size() == key.size() ? keyOrig.substr(0, dir.size()) : dir;
                    const std::string  cfg = CB::core::resolve::ReadCacheInput(dataDir / fs::path(dirOrig) / "config.json")
                                                .value_or(std::string());
                    std::smatch        mt;
                    if (std::regex_search(cfg, mt, std::regex(R"re("priority"\s*:\s*(-?\d+))re"))) prio = std::stoi(mt[1].str());
                    if (std::regex_search(cfg, std::regex(R"re("disabled"\s*:\s*true)re"))) prio = std::nullopt;
                    it = cfgCache.emplace(dir, prio).first;
                }
                if (!it->second) return std::nullopt;
                return std::make_pair(root + rest.substr(t), *it->second);
            }
            return std::nullopt;
        }

    }  // namespace

    ServeResult ServeAnimData(const fs::path& dataDir, const fs::path& loadOrderIni, const AdsfSource& adsf,
                              const Executor* exec)
    {
        ServeResult r;
        r.attempted = true;
        std::error_code ec;
        const fs::path pluginsRoot = dataDir / "community_behaviors" / "plugins";
        const auto     order       = CB::engine::codec::ReadLoadOrder(loadOrderIni).value_or(CB::engine::codec::LoadOrder{});

        /// @note Every bundle, lowest priority first (the master, then mods by load order): an animation is read from
        ///       the HIGHEST bundle that carries it.
        std::vector<std::pair<int, BundleReader>> bundles;
        if (fs::is_directory(pluginsRoot, ec))
            for (fs::directory_iterator bi(pluginsRoot, ec), bend; !ec && bi != bend; bi.increment(ec)) {
                if (!HasExt(bi->path().filename().string(), ".hky")) continue;
                const std::string stem = PathKey(bi->path().stem().string());
                auto rd = BundleReader::Open(bi->path());
                if (!rd) { LOG_WARN("AnimData: cannot read bundle '{}' — skipped.", bi->path().string()); continue; }
                bundles.emplace_back(stem == "skyrim" ? -1 : order.Rank(stem), std::move(*rd));
            }
        std::stable_sort(bundles.begin(), bundles.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        if (bundles.empty() || bundles.front().first != -1) {
            r.error = "no Skyrim.hky master (its project index is the adsf's project list)";
            LOG_WARN("AnimData: {} — vanilla animation data stays.", r.error);
            return r;
        }

        std::string err;
        const auto  index = bundles.front().second.read("meshes/animationdatasinglefile.txt/index.yaml");
        const auto  headers = index ? animdata::ParseAnimdataIndexYaml(*index, err) : std::vector<animdata::ProjectHeader>{};
        if (headers.empty()) {
            r.error = "master carries no animationdatasinglefile.txt/index.yaml" + (err.empty() ? std::string() : " (" + err + ")");
            LOG_WARN("AnimData: {} — vanilla animation data stays.", r.error);
            return r;
        }

        /// @note Animations are parsed once, in parallel (gigabytes of keyframe YAML; the engine's adsf open waits on
        ///       this), each with its actor motion resolved — its record, or extracted from a folded root.
        std::unordered_map<std::string, std::optional<CB::engine::anim::AnimationDef>> anims;
        const auto parse = [](const std::string& text, const std::string& path) -> std::optional<CB::engine::anim::AnimationDef> {
            try {
                auto def = CB::engine::anim::AnimationYamlLoader::LoadFromString(text, path);
                def.motion = CB::engine::anim::ResolveRootMotion(def);
                return def;
            } catch (const std::exception& e) {
                LOG_WARN("AnimData: animation '{}' unreadable: {}", path, e.what());
                return std::nullopt;
            }
        };

        /// @note DAR/OAR-only animations: a mod that ships an animation ONLY as a replacer copy (BFCO, Dodge MCO-DXP)
        ///       leaves its clips' base path empty, yet the clip binds that base slot (OAR swaps the file it plays, never
        ///       the slot or the adsf). Without a motion record the slot's root motion never activates, so the slot takes
        ///       its motion from an override copy. The adsf holds ONE record per slot, so of several copies (BFCO's per-weapon
        ///       submods) the DEFAULT one stands for all: the lowest non-negative priority that carries motion.
        std::unordered_map<std::string, std::vector<OverrideCopy>> overrides;
        {
            std::unordered_map<std::string, std::optional<int>> cfgCache;
            /// @note filesUnder (path keys) and filesUnderOrig (original case) walk the same range, so index i aligns.
            for (std::size_t b = 1; b < bundles.size(); ++b) {   // [0] is the master: no replacer folders
                const auto keys = bundles[b].second.filesUnder("meshes/", ".hkx");
                const auto orig = bundles[b].second.filesUnderOrig("meshes/", ".hkx");
                for (std::size_t i = 0; i < keys.size() && i < orig.size(); ++i) {
                    const std::string& key = keys[i];
                    if (key.find("replacer/") != std::string::npos)
                        if (auto base = OverrideBase(key, orig[i], dataDir, cfgCache))
                            overrides[base->first].push_back({ key, base->second, b });
                }
            }
            for (auto& [_, copies] : overrides)
                std::sort(copies.begin(), copies.end(), [](const OverrideCopy& x, const OverrideCopy& y) {
                    if ((x.priority >= 0) != (y.priority >= 0)) return x.priority >= 0;
                    if (x.priority != y.priority) return x.priority < y.priority;
                    if (x.bundle != y.bundle) return x.bundle > y.bundle;
                    return x.key < y.key;
                });
        }
        std::atomic<std::size_t> fromOverride{ 0 }, severalCopies{ 0 };

        const auto load = [&](const std::string& path) -> std::optional<CB::engine::anim::AnimationDef> {
            for (auto it = bundles.rbegin(); it != bundles.rend(); ++it)
                if (const auto text = it->second.read(path)) return parse(*text, path);
            const auto o = overrides.find(path);
            if (o == overrides.end()) return std::nullopt;
            std::optional<CB::engine::anim::AnimationDef> first;
            for (const auto& c : o->second) {
                const auto text = bundles[c.bundle].second.read(c.key);
                if (!text) continue;
                auto def = parse(*text, c.key);
                if (!def) continue;
                if (def->motion) { first = std::move(def); break; }
                if (!first) first = std::move(def);
            }
            if (first) {
                fromOverride.fetch_add(1, std::memory_order_relaxed);
                if (o->second.size() > 1) severalCopies.fetch_add(1, std::memory_order_relaxed);
            }
            return first;
        };
        animdata::ProjectionSource src;
        src.roster        = adsf.roster;
        src.clips         = adsf.clips;
        src.behaviorUnits = adsf.behaviorUnits;
        src.prefetch      = [&](const std::vector<std::string>& paths) {
            std::vector<std::optional<CB::engine::anim::AnimationDef>> parsed(paths.size());
            std::atomic<std::size_t> next{ 0 };
            const auto drain = [&] { for (std::size_t i; (i = next.fetch_add(1)) < paths.size();) parsed[i] = load(paths[i]); };
            if (exec && paths.size() > 1) {
                /// @note One draining task per hardware thread, not one per path: thousands of paths would each pay a
                ///       queue round-trip. Each result lands in its own slot, so the output matches a serial run.
                const std::size_t nTasks = std::min<std::size_t>(paths.size(), std::max(1u, std::thread::hardware_concurrency()));
                (*exec)(std::vector<std::function<void()>>(nTasks, drain));
            }
            drain();
            for (std::size_t i = 0; i < paths.size(); ++i) anims.emplace(paths[i], std::move(parsed[i]));
        };
        src.animation = [&](const std::string& path) -> const CB::engine::anim::AnimationDef* {
            auto it = anims.find(path);
            if (it == anims.end()) it = anims.emplace(path, load(path)).first;
            return it->second ? &*it->second : nullptr;
        };

        const auto* reg = CB::engine::schema::SharedRegistry();
        if (!reg) { r.error = "schema registry not armed"; LOG_ERROR("AnimData: {}.", r.error); return r; }
        animdata::ProjectionStats st;
        const auto t0   = std::chrono::steady_clock::now();
        const auto root = animdata::ProjectAnimData(*reg, headers, src, &st, err);
        const auto t1   = std::chrono::steady_clock::now();
        if (!root) { r.error = "projection failed: " + err; LOG_ERROR("AnimData: {}.", r.error); return r; }

        /// @note The schema writer enforces the descriptors' `framelimit` (the engine reads each project's frame
        ///       counts through a 16-bit truncation): a file it would misread is refused and vanilla stays.
        std::string text;
        if (!CB::engine::codec::io::WriteMetaText(*root, text, err)) {
            r.error = err;
            LOG_ERROR("AnimData: {} — NOT serving the projected animation data (vanilla stays).", r.error);
            return r;
        }
        const fs::path cachePath = dataDir / "community_behaviors_cache" / "animationdatasinglefile.txt";
        if (std::string werr; !disk::WriteFile(cachePath, text, { .atomic = true }, &werr)) {
            r.error = "failed to write the animation data cache: " + werr;
            LOG_ERROR("AnimData: {}", r.error);
            return r;
        }
        r.ok             = true;
        r.cachePath      = cachePath.string();
        const auto ms = [](auto a, auto b) { return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count(); };
        LOG_INFO("AnimData: projected {} anim project(s) — {} clip(s), {} motion(s), {} animation(s) — from {} bundle(s) in "
                 "{} ms; wrote {}.", st.projects, st.clips, st.motions, anims.size(), bundles.size(), ms(t0, t1), r.cachePath);
        if (const auto n = fromOverride.load(); n > 0)
            LOG_INFO("AnimData: {} animation(s) exist only as DAR/OAR replacer copies — their slots take the default copy's "
                     "motion ({} had several copies; the lowest-priority one stands for all).", n, severalCopies.load());
        if (st.unresolved)
            LOG_INFO("AnimData: {} clip(s) play an animation their character's roster lacks (no motion; e.g. {}).", st.unresolved,
                     st.unresolvedSample.empty() ? std::string() : st.unresolvedSample.front());
        return r;
    }

    // ── Deterministic redirect: hook the loader's file-open call site ─────────────
    namespace {

        std::atomic<bool>               s_redirectActive{ false };
        RE::BSFixedString               s_cachePath;

        std::int32_t OnOpenAnimData(CB::platform::game::AdsfOpen::Next next, std::uintptr_t a_path, std::uintptr_t a_out,
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
                    return next(ours, a_out, a_r8, a_r9);
                }
            }
            return next(a_path, a_out, a_r8, a_r9);
        }

    }  // namespace

    void InstallAnimDataHook()
    {
        CB::platform::game::AdsfOpen::Subscribe(&OnOpenAnimData, CB::platform::hooks::Priority::Normal, "AnimDataServe");
    }

    void ArmAnimDataRedirect(const ServeResult& result)
    {
        if (!CB::platform::game::AdsfOpen::Installed()) {
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
        static constexpr const char* kRedirectPath = "community_behaviors_cache/animationdatasinglefile.txt";

        namespace engine = CB::platform::hooks::engine;
        if (!engine::Available(engine::Adsf_SingleFilePath)) {
            LOG_WARN("AnimData: the single-file path global has no verified pin on this runtime — the merged "
                     "cache was written but the game will not read it.");
            return;
        }
        auto& global = *reinterpret_cast<RE::BSFixedString*>(engine::Resolve(engine::Adsf_SingleFilePath));

        const char* cur = global.c_str();
        LOG_INFO("AnimData: engine single-file path global currently = \"{}\".", (cur && *cur) ? cur : "(empty)");

        if (!result.ok) {
            LOG_INFO("AnimData: no merged cache this launch — leaving the engine path global unchanged.");
            return;
        }

        global = RE::BSFixedString(kRedirectPath);
        LOG_INFO("AnimData: redirected engine single-file path global -> \"{}\" (serves {}).",
                 kRedirectPath, result.cachePath);
    }

}  // namespace CB::core::serve
