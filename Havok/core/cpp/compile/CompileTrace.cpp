#include <compile/CompileTrace.h>


#include <RymlInclude.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace CB::core::compile::trace {

namespace {
    // A loaded probe definition (Havok/core/Schema/debug/*.yaml). Empty criterion = "any".
    struct Probe {
        bool                     enabled = true;
        std::vector<std::string> phases;    // trace phases to keep (vars/bind/edge/merge/visit)
        std::vector<std::string> classes;   // node classes to keep
        std::vector<std::string> match;     // substrings; any must occur in unit/class/name/field/detail
    };

    Sink               g_sink;      // null by default -> disabled
    std::string        g_filter;    // legacy substring filter (used only when no probes are loaded)
    std::vector<Probe> g_probes;    // schema-driven probes; non-empty => they gate Rec()

    bool inList(const std::vector<std::string>& v, std::string_view s) {
        if (v.empty()) return true;   // empty criterion = any
        for (const auto& e : v) if (e == s) return true;
        return false;
    }
    bool anySubstr(const std::vector<std::string>& subs, std::initializer_list<std::string_view> fields) {
        if (subs.empty()) return true;
        for (const auto& sub : subs)
            for (std::string_view f : fields)
                if (f.find(sub) != std::string_view::npos) return true;
        return false;
    }
    bool probesAllow(std::string_view phase, std::string_view unit, std::string_view cls,
                     std::string_view name, std::string_view field, std::string_view detail) {
        for (const auto& p : g_probes) {
            if (!p.enabled) continue;
            if (inList(p.phases, phase) && inList(p.classes, cls) &&
                anySubstr(p.match, { unit, cls, name, field, detail }))
                return true;
        }
        return false;
    }
    // ryml helper: read a scalar or seq child into a string vector.
    void readSeq(const c4::yml::ConstNodeRef& n, const char* key, std::vector<std::string>& out) {
        if (!n.is_map() || !n.has_child(c4::to_csubstr(key))) return;
        auto c = n[c4::to_csubstr(key)];
        if (c.is_seq()) { for (auto e : c) if (e.has_val()) { std::string s; c4::from_chars(e.val(), &s); out.push_back(std::move(s)); } }
        else if (c.has_val()) { std::string s; c4::from_chars(c.val(), &s); if (!s.empty()) out.push_back(std::move(s)); }
    }
}

void SetSink(Sink sink) { g_sink = std::move(sink); }

bool Enabled() noexcept { return static_cast<bool>(g_sink); }

void SetFilter(std::string substr) { g_filter = std::move(substr); }

void        ClearProbes()         { g_probes.clear(); }
std::size_t ProbeCount() noexcept { return g_probes.size(); }

std::size_t LoadProbes(const std::string& debugDir, std::string* warn) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(debugDir, ec)) { if (warn) *warn += "debug dir not found: " + debugDir + "\n"; return 0; }
    std::size_t loaded = 0;
    for (auto it = fs::directory_iterator(debugDir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const fs::path p = it->path();
        const std::string ext = p.extension().string();
        if (ext != ".yaml" && ext != ".yml") continue;
        std::ifstream f(p, std::ios::binary);
        std::stringstream ss; ss << f.rdbuf();
        std::string text = ss.str();
        try {
            c4::yml::Tree t = c4::yml::parse_in_place(c4::to_substr(text));
            auto root = t.rootref();
            Probe pr;
            if (root.is_map() && root.has_child("enabled")) {
                std::string e; c4::from_chars(root["enabled"].val(), &e);
                pr.enabled = (e == "true" || e == "1");
            }
            readSeq(root, "phases",  pr.phases);
            readSeq(root, "classes", pr.classes);
            readSeq(root, "match",   pr.match);
            g_probes.push_back(std::move(pr));
            ++loaded;
        } catch (const std::exception& e) {
            if (warn) *warn += "probe parse failed (" + p.filename().string() + "): " + e.what() + "\n";
        }
    }
    return loaded;
}

std::vector<WatchVar> LoadRuntimeWatch(const std::string& debugDir, std::string* warn) {
    namespace fs = std::filesystem;
    std::vector<WatchVar> out;
    std::error_code ec;
    if (!fs::is_directory(debugDir, ec)) { if (warn) *warn += "debug dir not found: " + debugDir + "\n"; return out; }
    const auto seen = [&out](const std::string& v) {
        for (const auto& w : out) if (w.var == v) return true; return false;
    };
    for (auto it = fs::directory_iterator(debugDir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const fs::path p = it->path();
        const std::string ext = p.extension().string();
        if (ext != ".yaml" && ext != ".yml") continue;
        std::ifstream f(p, std::ios::binary);
        std::stringstream ss; ss << f.rdbuf();
        std::string text = ss.str();
        try {
            c4::yml::Tree t = c4::yml::parse_in_place(c4::to_substr(text));
            auto root = t.rootref();
            if (!root.is_map()) continue;
            // an enabled probe contributes its watch list; a disabled one is skipped.
            if (root.has_child("enabled")) {
                std::string e; c4::from_chars(root["enabled"].val(), &e);
                if (!(e == "true" || e == "1")) continue;
            }
            if (!root.has_child("watch")) continue;
            auto w = root["watch"];
            if (!w.is_seq()) continue;
            for (auto entry : w) {
                WatchVar wv;
                if (entry.is_map()) {
                    if (entry.has_child("var")  && entry["var"].has_val())  c4::from_chars(entry["var"].val(),  &wv.var);
                    if (entry.has_child("kind") && entry["kind"].has_val()) c4::from_chars(entry["kind"].val(), &wv.kind);
                } else if (entry.has_val()) {           // bare scalar => int var
                    c4::from_chars(entry.val(), &wv.var);
                }
                if (wv.kind.empty()) wv.kind = "int";
                if (!wv.var.empty() && !seen(wv.var)) out.push_back(std::move(wv));
            }
        } catch (const std::exception& e) {
            if (warn) *warn += "watch parse failed (" + p.filename().string() + "): " + e.what() + "\n";
        }
    }
    return out;
}

void Line(std::string_view line) {
    if (g_sink) g_sink(line);
}

void Rec(std::string_view phase, std::string_view unit, std::string_view cls,
         std::string_view name, std::string_view field, std::string_view action,
         std::string_view detail) {
    if (!g_sink) return;
    if (!g_probes.empty()) {
        // Schema-driven probes gate everything once loaded.
        if (!probesAllow(phase, unit, cls, name, field, detail)) return;
    } else if (!g_filter.empty()) {
        const auto hit = [&](std::string_view s) { return s.find(g_filter) != std::string_view::npos; };
        if (!hit(unit) && !hit(cls) && !hit(name)) return;
    }
    const auto dash = [](std::string_view s) { return s.empty() ? std::string_view{"-"} : s; };
    std::string line = "[CC] ";
    line.append(dash(phase));  line.push_back(' ');
    line.append(dash(unit));   line.push_back(' ');
    line.append(dash(cls));    line.push_back('#');
    line.append(dash(name));   line.push_back(' ');
    line.append(dash(field));  line.push_back(' ');
    line.append(dash(action));
    if (!detail.empty()) { line.push_back(' '); line.append(detail); }
    g_sink(line);
}

} // namespace CB::core::compile::trace
