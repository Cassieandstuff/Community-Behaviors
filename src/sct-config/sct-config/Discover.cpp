#include <sct-config/SctConfig.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace sct::config {
namespace {

std::string ToLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Relative path -> '/'-separated, lowercased key (stable across platforms and
// case-insensitive Windows filesystems).
std::string MakeKey(const fs::path& rel) {
    return ToLower(rel.generic_string());
}

}  // namespace

std::vector<Source> Discover(const ScanSpec& spec) {
    // Normalize the extension filter once: lowercase, leading dot enforced.
    std::vector<std::string> exts;
    exts.reserve(spec.extensions.size());
    for (std::string e : spec.extensions) {
        if (e.empty()) continue;
        e = ToLower(std::move(e));
        if (e.front() != '.') e.insert(e.begin(), '.');
        exts.push_back(std::move(e));
    }
    const auto matches = [&](const fs::path& p) {
        if (exts.empty()) return true;
        const std::string pe = ToLower(p.extension().string());
        return std::find(exts.begin(), exts.end(), pe) != exts.end();
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
            out.push_back(Source{ file, file.stem().string(), MakeKey(rel) });
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
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    // Strip a leading UTF-8 BOM (mirrors the readers this consolidates).
    if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF &&
        static_cast<unsigned char>(s[1]) == 0xBB && static_cast<unsigned char>(s[2]) == 0xBF)
        s.erase(0, 3);
    return s;
}

}  // namespace sct::config
