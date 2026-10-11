#include <config/Config.h>

#include <codec/resource/PathKey.h>
#include <disk/Disk.h>

#include <algorithm>

namespace fs = std::filesystem;

namespace CB::config {
namespace {

using CB::engine::codec::resource::HasExt;
using CB::engine::codec::resource::PathKey;

}  // namespace

std::vector<Source> Discover(const ScanSpec& spec) {
    // Normalize the extension filter once: path key, leading dot enforced.
    std::vector<std::string> exts;
    exts.reserve(spec.extensions.size());
    for (std::string e : spec.extensions) {
        if (e.empty()) continue;
        e = PathKey(e);
        if (e.front() != '.') e.insert(e.begin(), '.');
        exts.push_back(std::move(e));
    }
    const auto matches = [&](const fs::path& p) {
        if (exts.empty()) return true;
        const std::string fn = p.filename().string();
        return std::any_of(exts.begin(), exts.end(), [&](const std::string& e) { return HasExt(fn, e); });
    };

    std::vector<Source> out;
    for (const auto& root : spec.roots) {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) continue;   // config dirs are optional

        const auto emit = [&](const fs::path& file) {
            if (!matches(file)) return;
            std::error_code rec;
            fs::path rel = fs::relative(file, root, rec);
            if (rec || rel.empty()) rel = file.filename();
            out.push_back(Source{ file, file.stem().string(), PathKey(rel.generic_string()) });
        };

        if (spec.recursive) {
            for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
                 !ec && it != end; it.increment(ec)) {
                std::error_code fec;
                if (it->is_regular_file(fec)) emit(it->path());
            }
        } else {
            for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
                std::error_code fec;
                if (it->is_regular_file(fec)) emit(it->path());
            }
        }
    }

    // Deterministic order: lexicographic by key, then absolute path to break the
    // tie when the same relative name appears under two roots (the resolver picks
    // the winner; discovery just guarantees a stable total order).
    std::sort(out.begin(), out.end(), [](const Source& a, const Source& b) {
        if (a.key != b.key) return a.key < b.key;
        return a.path < b.path;
    });
    return out;
}

std::optional<std::string> ReadText(const fs::path& path) {
    return CB::engine::disk::ReadFile(path, { .stripBom = true });
}

}  // namespace CB::config
