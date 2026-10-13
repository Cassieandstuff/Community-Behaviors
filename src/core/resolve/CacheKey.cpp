#include "PCH.h"

#include "core/resolve/CacheKey.h"
#include "core/debug/Settings.h"

#include <codec/resource/PathKey.h>
#include <codec/serialization/HkyArchive.h>
#include <disk/Disk.h>
#include <interface/reflection/HavokSchema.h>
#include <text/Text.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <mutex>
#include <random>
#include <vector>

namespace fs = std::filesystem;

namespace CB::core::resolve {

    namespace {

        using CB::engine::codec::resource::HasExt;

        constexpr std::string_view kHeader = "cb-cache-key 1";
        /// @brief The value of an input that can't be read: never equal to anything, so it always misses.
        constexpr std::string_view kUnreadable = "?";
        /// @brief The value of a recorded read whose file did not exist.
        constexpr std::string_view kAbsent = "-";

        std::string Hex(std::uint64_t h)
        {
            char b[17];
            std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(h));
            return b;
        }

        std::uint64_t Mix(std::uint64_t h, std::uint64_t v)
        {
            return Fnv64(std::string_view(reinterpret_cast<const char*>(&v), sizeof v), h);
        }

        std::string FileValue(const fs::path& p)
        {
            std::error_code ec;
            if (!fs::exists(p, ec)) return std::string(kAbsent);
            const auto bytes = CB::engine::disk::ReadFile(p);
            return bytes ? Hex(Fnv64(*bytes)) : std::string(kUnreadable);
        }

        /// @brief Every regular file under `dir` as (generic relative path, absolute path), sorted by relative path.
        std::vector<std::pair<std::string, fs::path>> FilesUnder(const fs::path& dir, bool recursive)
        {
            std::vector<std::pair<std::string, fs::path>> out;
            std::error_code ec;
            const auto add = [&](const fs::directory_entry& e) {
                std::error_code fe;
                if (e.is_regular_file(fe)) out.emplace_back(e.path().lexically_relative(dir).generic_string(), e.path());
            };
            if (recursive)
                for (fs::recursive_directory_iterator it(CB::engine::disk::LongPath(dir), ec), end; !ec && it != end; it.increment(ec)) add(*it);
            else
                for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) add(*it);
            std::sort(out.begin(), out.end());
            return out;
        }

        /// @brief Every file's relative path and content, folded into one hash; empty dir = a fixed value, missing = absent.
        std::string TreeContentValue(const fs::path& dir, bool recursive)
        {
            std::error_code ec;
            if (!fs::is_directory(dir, ec)) return std::string(kAbsent);
            std::uint64_t h = Fnv64("");
            for (const auto& [rel, abs] : FilesUnder(dir, recursive)) {
                const auto bytes = CB::engine::disk::ReadFile(abs);
                if (!bytes) return std::string(kUnreadable);
                h = Fnv64(rel, h);
                h = Mix(h, Fnv64(*bytes));
            }
            return Hex(h);
        }

        /// @brief A bundle's value. Packed: its zip index (path, size, CRC per file — nothing inflated). Unpacked (an
        ///        author's dev tree): each file's path, size and modification time, the make/ninja stamp.
        std::string BundleValue(const fs::path& bundle)
        {
            std::error_code ec;
            std::uint64_t   h = Fnv64("");
            if (fs::is_directory(bundle, ec)) {
                for (const auto& [rel, abs] : FilesUnder(bundle, true)) {
                    std::error_code fe;
                    const auto size  = fs::file_size(CB::engine::disk::LongPath(abs), fe);
                    const auto mtime = fs::last_write_time(CB::engine::disk::LongPath(abs), fe);
                    if (fe) return std::string(kUnreadable);
                    h = Fnv64(rel, h);
                    h = Mix(h, size);
                    h = Mix(h, static_cast<std::uint64_t>(mtime.time_since_epoch().count()));
                }
                return Hex(h);
            }
            std::string err;
            const auto  index = CB::engine::codec::HkyArchive::ReadIndex(bundle.string(), err);
            if (!index) {
                LOG_WARN("Community Behaviors: cache key — cannot index '{}': {}", bundle.string(), err);
                return std::string(kUnreadable);
            }
            for (const auto& e : *index) {
                h = Fnv64(e.path, h);
                h = Mix(h, e.size);
                h = Mix(h, e.crc);
            }
            return Hex(h);
        }

        /// @brief The plugin DLL itself — CB's compiler identity: any rebuild is a new compiler.
        std::pair<std::string, std::string> CompilerLine()
        {
            HMODULE self = nullptr;
            wchar_t path[MAX_PATH * 4] = {};
            if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                      reinterpret_cast<LPCWSTR>(&CompilerLine), &self) ||
                ::GetModuleFileNameW(self, path, static_cast<DWORD>(std::size(path))) == 0)
                return { "plugin", std::string(kUnreadable) };
            const fs::path dll(path);
            return { dll.filename().string(), FileValue(dll) };
        }

        struct Line {
            std::string kind, name, value;
            std::string Id() const { return kind + '\t' + name; }
        };

        /// @brief The per-input lines, computed once per launch.
        const std::vector<Line>& StaticLines(const fs::path& dataDir)
        {
            static std::once_flag    once;
            static std::vector<Line> lines;
            std::call_once(once, [&] {
                const auto t0 = std::chrono::steady_clock::now();
                const auto [dllName, dllValue] = CompilerLine();
                lines.push_back({ "compiler", dllName, dllValue });
                const std::string schemaDir = CB::engine::schema::SharedSchemaDir();
                lines.push_back({ "schema", schemaDir, schemaDir.empty() ? std::string(kAbsent) : TreeContentValue(schemaDir, true) });
                /// @note Only the settings that change compiled output belong here.
                lines.push_back({ "setting", "ERGate.bEnable", CB::core::debug::FlagOn("er_gate") ? "1" : "0" });
                lines.push_back({ "loadorder", "loadorder.txt", FileValue(dataDir / "community_behaviors" / "loadorder.txt") });
                lines.push_back({ "bdi", "BehaviorDataInjector",
                                  TreeContentValue(dataDir / "SKSE" / "Plugins" / "BehaviorDataInjector", false) });
                const fs::path plugins = dataDir / "community_behaviors" / "plugins";
                std::error_code ec;
                std::vector<fs::path> bundles;
                if (fs::is_directory(plugins, ec))
                    for (fs::directory_iterator it(plugins, ec), end; !ec && it != end; it.increment(ec))
                        if (HasExt(it->path().filename().string(), ".hky")) bundles.push_back(it->path());
                std::sort(bundles.begin(), bundles.end());
                for (const auto& b : bundles) lines.push_back({ "bundle", b.filename().string(), BundleValue(b) });
                const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                LOG_INFO("Community Behaviors: cache key computed in {:.0f} ms ({} bundle(s)).", ms, bundles.size());
            });
            return lines;
        }

        /// @brief Per-output kinds (the Resolver's own keys), not inputs: a graph's key, the adsf/asdsf key, and the
        ///        clip inputs the adsf projection read.
        bool IsOutputKind(std::string_view kind) { return kind == "graph" || kind == "animdata" || kind == "clips"; }

        /// @brief One sentinel row "kind\tname\tvalue"; nullopt for anything else.
        std::optional<Line> ParseRow(std::string_view raw)
        {
            const std::string_view row = CB::engine::text::Trim(raw);
            const auto t1 = row.find('\t');
            const auto t2 = t1 == std::string_view::npos ? t1 : row.rfind('\t');
            if (t2 == std::string_view::npos || t2 == t1) return std::nullopt;
            return Line{ std::string(row.substr(0, t1)), std::string(row.substr(t1 + 1, t2 - t1 - 1)),
                         std::string(row.substr(t2 + 1)) };
        }

        /// @brief A random value fixed for this launch: folded in for an input that can't be read, so a key built from
        ///        it never matches one stored by an earlier launch.
        std::uint64_t LaunchNonce()
        {
            static const std::uint64_t nonce = (std::uint64_t{ std::random_device{}() } << 32) ^ std::random_device{}();
            return nonce;
        }

        /// @brief The static lines of `kinds`, folded into one value.
        /// @note  An unreadable input folds in a per-launch random value, so the result never matches a stored key.
        std::uint64_t FoldLines(const fs::path& dataDir, std::initializer_list<std::string_view> kinds)
        {
            std::uint64_t h = Fnv64("");
            for (const auto& l : StaticLines(dataDir)) {
                if (std::find(kinds.begin(), kinds.end(), l.kind) == kinds.end()) continue;
                h = Fnv64(l.Id() + '\t' + l.value + '\n', h);
                if (l.value == kUnreadable) h = Mix(h, LaunchNonce());
            }
            return h;
        }

        std::mutex                         g_readsMutex;
        std::map<std::string, std::string> g_reads;   ///< path as read -> value
    }  // namespace

    std::uint64_t Fnv64(std::string_view bytes, std::uint64_t h)
    {
        for (const unsigned char c : bytes) { h ^= c; h *= 1099511628211ull; }
        return h;
    }

    std::optional<std::string> ReadCacheInput(const fs::path& p)
    {
        std::error_code ec;
        std::optional<std::string> bytes;
        if (fs::exists(p, ec)) bytes = CB::engine::disk::ReadFile(p);
        const std::string value = bytes ? Hex(Fnv64(*bytes)) : fs::exists(p, ec) ? std::string(kUnreadable) : std::string(kAbsent);
        std::lock_guard<std::mutex> lk(g_readsMutex);
        g_reads[p.generic_string()] = value;
        return bytes;
    }

    std::string KeyHex(std::uint64_t h) { return Hex(h); }

    std::string CacheKeyText(const fs::path& dataDir, std::string_view outputLines)
    {
        std::string out(kHeader);
        out += '\n';
        for (const auto& l : StaticLines(dataDir)) out += l.kind + '\t' + l.name + '\t' + l.value + '\n';
        {
            std::lock_guard<std::mutex> lk(g_readsMutex);
            for (const auto& [path, value] : g_reads) out += "read\t" + path + '\t' + value + '\n';
        }
        out += outputLines;
        return out;
    }

    std::string CompilerIdentity(const fs::path& dataDir)
    {
        return Hex(FoldLines(dataDir, { "compiler", "schema" }));
    }

    std::uint64_t SharedGraphInputs(const fs::path& dataDir)
    {
        return FoldLines(dataDir, { "compiler", "schema", "setting", "bdi" });
    }

    std::uint64_t BundleFilesInput(const fs::path& dataDir, const std::function<bool(const std::string& unitKey)>& skipUnit)
    {
        using CB::engine::codec::resource::PathKey;
        std::uint64_t h = SharedGraphInputs(dataDir);
        h = Fnv64(FileValue(dataDir / "community_behaviors" / "loadorder.txt"), h);
        const auto skipped = [&](const std::string& rel) {
            const std::string k   = PathKey(rel);
            const auto        pos = k.find(".hkx/");
            return pos != std::string::npos && skipUnit(k.substr(0, pos + 4));
        };
        const fs::path        plugins = dataDir / "community_behaviors" / "plugins";
        std::vector<fs::path> bundles;
        std::error_code       ec;
        if (fs::is_directory(plugins, ec))
            for (fs::directory_iterator it(plugins, ec), end; !ec && it != end; it.increment(ec))
                if (HasExt(it->path().filename().string(), ".hky")) bundles.push_back(it->path());
        std::sort(bundles.begin(), bundles.end());
        for (const auto& b : bundles) {
            h = Fnv64(b.filename().string() + '\n', h);
            if (fs::is_directory(b, ec)) {
                for (const auto& [rel, abs] : FilesUnder(b, true)) {
                    if (skipped(rel)) continue;
                    std::error_code fe;
                    const auto size  = fs::file_size(CB::engine::disk::LongPath(abs), fe);
                    const auto mtime = fs::last_write_time(CB::engine::disk::LongPath(abs), fe);
                    h = Mix(Mix(Fnv64(rel, h), size), static_cast<std::uint64_t>(mtime.time_since_epoch().count()));
                }
                continue;
            }
            std::string err;
            const auto  index = CB::engine::codec::HkyArchive::ReadIndex(b.string(), err);
            if (!index) { h = Mix(h, LaunchNonce()); continue; }
            for (const auto& e : *index)
                if (!skipped(e.path)) h = Mix(Mix(Fnv64(e.path, h), e.size), e.crc);
        }
        return h;
    }

    void CarryReads(std::string_view stored)
    {
        for (const auto row : CB::engine::text::SplitLines(stored))
            if (const auto r = ParseRow(row); r && r->kind == "read") {
                const std::string value = FileValue(r->name);
                std::lock_guard<std::mutex> lk(g_readsMutex);
                g_reads.emplace(r->name, value);
            }
    }

    std::unordered_map<std::string, std::string> StoredOutputKeys(std::string_view stored, std::string_view kind)
    {
        std::unordered_map<std::string, std::string> out;
        const auto rows = CB::engine::text::SplitLines(stored);
        if (rows.empty() || CB::engine::text::Trim(rows[0]) != kHeader) return out;
        for (std::size_t i = 1; i < rows.size(); ++i)
            if (const auto r = ParseRow(rows[i]); r && r->kind == kind) out[r->name] = r->value;
        return out;
    }

    std::string CacheKeyMismatch(std::string_view stored, const fs::path& dataDir)
    {
        const auto rows = CB::engine::text::SplitLines(stored);
        if (rows.empty() || CB::engine::text::Trim(rows[0]) != kHeader) return "no cache key (written by an older build)";

        std::map<std::string, std::string> now;
        for (const auto& l : StaticLines(dataDir)) now[l.Id()] = l.value;
        std::map<std::string, std::string> then;
        for (std::size_t i = 1; i < rows.size(); ++i) {
            const auto r = ParseRow(rows[i]);
            if (!r || IsOutputKind(r->kind)) continue;
            if (r->kind == "read") {
                if (const std::string cur = FileValue(r->name); cur != r->value || cur == kUnreadable)
                    return "'" + r->name + "' changed";
                continue;
            }
            then[r->Id()] = r->value;
        }
        const auto label = [](const std::string& id) {
            const auto t = id.find('\t');
            return id.substr(0, t) + " '" + id.substr(t + 1) + "'";
        };
        for (const auto& [id, value] : then) {
            const auto it = now.find(id);
            if (it == now.end()) return label(id) + " removed";
            if (it->second != value || value == kUnreadable) return label(id) + " changed";
        }
        for (const auto& [id, _] : now)
            if (!then.count(id)) return label(id) + " added";
        return {};
    }

}  // namespace CB::core::resolve
