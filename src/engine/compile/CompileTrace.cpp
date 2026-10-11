#include <compile/CompileTrace.h>


#include <RymlInclude.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace CB::engine::compile::trace {

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

} // namespace CB::engine::compile::trace
