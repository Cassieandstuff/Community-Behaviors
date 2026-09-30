#include <codec/format/AnimationScan.h>

#include <RymlInclude.h>

#include <algorithm>
#include <cctype>
#include <set>
#include <unordered_map>

namespace CB::core::anim {

namespace {
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string quote(const std::string& s) {
    std::string q = "'";
    for (char c : s) { q += c; if (c == '\'') q += '\''; }
    return q + "'";
}
std::string str(c4::yml::ConstNodeRef n) {
    std::string s;
    if (n.has_val()) c4::from_chars(n.val(), &s);
    return s;
}
bool isFolder(std::string_view p) { return !p.empty() && (p.back() == '\\' || p.back() == '/'); }
}  // namespace

std::string EmitAnimationScanYaml(const AnimationScan& scan) {
    std::string y = "sources:\n";
    for (const auto& s : scan.sources) y += "  - " + quote(s) + "\n";
    if (!scan.ignore.empty()) {
        y += "ignore:\n";
        for (const auto& s : scan.ignore) y += "  - " + quote(s) + "\n";
    }
    return y;
}

std::optional<AnimationScan> ParseAnimationScanYaml(std::string_view text, std::string& err) {
    std::string buf(text);
    const c4::yml::Tree tree = c4::yml::parse_in_place(c4::to_substr(buf));
    const auto root = tree.crootref();
    if (!root.is_map() || !root.has_child("sources")) { err = "animations.yaml: no `sources:`"; return std::nullopt; }
    AnimationScan out;
    for (auto e : root["sources"]) out.sources.push_back(str(e));
    if (root.has_child("ignore"))
        for (auto e : root["ignore"]) out.ignore.push_back(str(e));
    return out;
}

std::string AnimationSortKey(std::string_view rosterPath) {
    const auto slash = rosterPath.find_last_of("\\/");
    std::string stem(slash == std::string_view::npos ? rosterPath : rosterPath.substr(slash + 1));
    if (const auto dot = stem.find_last_of('.'); dot != std::string::npos) stem.resize(dot);
    stem = lower(stem);
    std::replace(stem.begin(), stem.end(), '_', ' ');
    return stem;
}

bool IsReplacerPath(std::string_view path) {
    const std::string p = lower(std::string(path));
    return p.find("openanimationreplacer") != std::string::npos || p.find("dynamicanimationreplacer") != std::string::npos;
}

std::string ResolveProjectPath(const std::string& projectDir, std::string_view rel) {
    std::vector<std::string> parts;
    auto push = [&](std::string_view path) {
        std::size_t s = 0;
        while (s <= path.size()) {
            const std::size_t e = std::min(path.find_first_of("\\/", s), path.size());
            const std::string seg = lower(std::string(path.substr(s, e - s)));
            if (seg == "..") { if (!parts.empty()) parts.pop_back(); }
            else if (!seg.empty() && seg != ".") parts.push_back(seg);
            s = e + 1;
        }
    };
    push(projectDir);
    push(rel);
    std::string out;
    for (const auto& p : parts) { if (!out.empty()) out += '/'; out += p; }
    return out;
}

std::vector<std::string> ExpandAnimationScan(const AnimationScan& scan, const std::string& projectDir,
                                             const AnimationIndex& index, std::vector<std::string>* collisions) {
    struct Ignore { std::string path; bool folder; };
    std::vector<Ignore> ignores;
    for (const auto& i : scan.ignore) ignores.push_back({ ResolveProjectPath(projectDir, i), isFolder(i) });
    const auto ignored = [&](const std::string& key) {
        for (const auto& i : ignores)
            if (i.folder ? key.compare(0, i.path.size() + 1, i.path + "/") == 0 : key == i.path) return true;
        return false;
    };

    struct Pick { std::string entry, key; bool ignored = false; };
    std::unordered_map<std::string, Pick> byName;   // lowercase filename -> chosen file
    std::vector<std::string> order;
    std::set<std::string> reported;
    for (const auto& src : scan.sources) {
        const std::string dir = ResolveProjectPath(projectDir, src);
        std::string prefix = src;
        if (!prefix.empty() && !isFolder(prefix)) prefix += '\\';
        for (auto it = index.lower_bound(dir + "/"); it != index.end() && it->first.compare(0, dir.size() + 1, dir + "/") == 0; ++it) {
            const std::string& key = it->first;
            if (IsReplacerPath(key.substr(dir.size()))) continue;
            std::string sub = it->second.substr(dir.size() + 1);
            std::replace(sub.begin(), sub.end(), '/', '\\');
            const std::string name = key.substr(key.find_last_of('/') + 1);
            const bool ign = ignored(key);
            const auto found = byName.find(name);
            if (found == byName.end()) {
                byName.emplace(name, Pick{ prefix + sub, key, ign });
                order.push_back(name);
            } else if (found->second.key != key) {
                Pick& p = found->second;
                if (p.ignored && !ign) p = Pick{ prefix + sub, key, false };
                else if (!p.ignored && !ign && collisions && reported.insert(name).second)
                    collisions->push_back(p.key + " | " + key);
            }
        }
    }
    std::vector<std::string> roster;
    roster.reserve(order.size());
    for (const auto& n : order) roster.push_back(byName.at(n).entry);
    std::stable_sort(roster.begin(), roster.end(), [](const std::string& a, const std::string& b) {
        const std::string ka = AnimationSortKey(a), kb = AnimationSortKey(b);
        return ka != kb ? ka < kb : lower(a) < lower(b);
    });
    return roster;
}

void IndexAnimationFiles(AnimationIndex& index, const std::vector<std::string>& originalPaths) {
    for (std::string p : originalPaths) {
        std::replace(p.begin(), p.end(), '\\', '/');
        const std::string l = lower(p);
        if (l.size() < 4 || l.compare(l.size() - 4, 4, ".hkx") != 0) continue;
        if (l.find(".hkx/") != std::string::npos) continue;   // inside a unit folder
        index[l] = std::move(p);
    }
}

AnimationScan DeriveAnimationScan(const std::vector<std::string>& roster, const std::string& projectDir,
                                  const AnimationIndex& index) {
    const auto folderOf = [](std::string_view p) {
        const auto s = p.find_last_of("\\/");
        return s == std::string_view::npos ? std::string() : std::string(p.substr(0, s + 1));
    };
    const auto parentOf = [&](const std::string& rel) {   // "Animations\DLC01\" -> "Animations\"
        const std::string t = rel.substr(0, rel.size() - 1);
        return folderOf(t);
    };
    const auto meaningful = [](const std::string& rel) {  // a real folder, not the project itself or a bare ".." chain
        std::size_t s = 0;
        while (s < rel.size()) {
            const std::size_t e = std::min(rel.find_first_of("\\/", s), rel.size());
            const std::string seg = rel.substr(s, e - s);
            if (!seg.empty() && seg != "..") return true;
            s = e + 1;
        }
        return false;
    };
    const auto under = [](const std::string& d, const std::string& a) {
        return d == a || d.compare(0, a.size() + 1, a + "/") == 0;
    };

    std::map<std::string, std::set<std::string>> animFolders;   // resolved folder -> filenames
    for (const auto& [key, orig] : index) {
        if (IsReplacerPath(key)) continue;
        const auto s = key.find_last_of('/');
        animFolders[key.substr(0, s)].insert(key.substr(s + 1));
    }
    std::map<std::string, std::string> rosterFolders;           // resolved folder -> project-relative (roster case)
    std::set<std::string>              listed;                  // resolved roster paths
    std::vector<std::string>           firstSeen;
    for (const auto& r : roster) {
        const std::string rel = folderOf(r);
        const std::string res = ResolveProjectPath(projectDir, rel);
        if (rosterFolders.emplace(res, rel).second) firstSeen.push_back(res);
        listed.insert(ResolveProjectPath(projectDir, r));
    }
    const auto valid = [&](const std::string& a) {
        std::set<std::string> keys;
        for (const auto& [d, rel] : rosterFolders)
            if (under(d, a)) if (const auto f = animFolders.find(d); f != animFolders.end()) keys.insert(f->second.begin(), f->second.end());
        for (const auto& [d, names] : animFolders) {
            if (!under(d, a) || rosterFolders.count(d)) continue;
            for (const auto& n : names) if (!keys.count(n)) return false;
        }
        return true;
    };

    AnimationScan out;
    std::vector<std::pair<std::string, std::string>> sources;   // (resolved, relative)
    for (const auto& res : firstSeen) {
        std::string rel = rosterFolders.at(res);
        for (std::string p = parentOf(rel); meaningful(p) && valid(ResolveProjectPath(projectDir, p)); p = parentOf(p)) rel = p;
        const std::string r2 = ResolveProjectPath(projectDir, rel);
        if (std::none_of(sources.begin(), sources.end(), [&](const auto& s) { return s.first == r2; })) sources.push_back({ r2, rel });
    }
    std::erase_if(sources, [&](const auto& s) {
        return std::any_of(sources.begin(), sources.end(), [&](const auto& o) { return o.first != s.first && under(s.first, o.first); });
    });
    for (const auto& [res, rel] : sources) out.sources.push_back(rel);

    /// @note The original-case tail of an indexed path below a resolved source folder.
    const auto relUnder = [&](const std::string& res, const std::string& rel, const std::string& key) {
        std::string tail = index.at(key).substr(res.size() + 1);
        std::replace(tail.begin(), tail.end(), '/', '\\');
        return rel + tail;
    };
    for (const auto& [res, rel] : sources)
        for (const auto& [d, names] : animFolders)
            if (under(d, res) && !rosterFolders.count(d) && !names.empty())
                out.ignore.push_back(folderOf(relUnder(res, rel, d + "/" + *names.begin())));

    std::map<std::string, std::vector<std::pair<std::string, std::string>>> byName;   // filename -> (key, relative)
    for (const auto& [res, rel] : sources)
        for (auto it = index.lower_bound(res + "/"); it != index.end() && it->first.compare(0, res.size() + 1, res + "/") == 0; ++it) {
            const std::string& key = it->first;
            if (IsReplacerPath(key.substr(res.size())) || !rosterFolders.count(key.substr(0, key.find_last_of('/')))) continue;
            byName[key.substr(key.find_last_of('/') + 1)].push_back({ key, relUnder(res, rel, key) });
        }
    for (const auto& [name, files] : byName) {
        if (files.size() < 2) continue;
        auto keep = std::find_if(files.begin(), files.end(), [&](const auto& f) { return listed.count(f.first) != 0; });
        if (keep == files.end()) keep = files.begin();
        for (auto f = files.begin(); f != files.end(); ++f)
            if (f != keep) out.ignore.push_back(f->second);
    }
    return out;
}

}  // namespace CB::core::anim
