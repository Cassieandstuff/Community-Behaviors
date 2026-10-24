#include <compile/CompileTrace.h>


#include <RymlInclude.h>
#include <codec/resource/PathKey.h>
#include <disk/Disk.h>
#include <interface/reflection/HavokSchema.h>

#include <filesystem>
#include <string>
#include <vector>

namespace CB::engine::compile::trace {

std::vector<WatchVar> LoadRuntimeWatch(const std::string& schemaDir, std::string* warn) {
    namespace fs = std::filesystem;
    std::vector<WatchVar> out;
    std::error_code ec;
    if (!fs::is_directory(schemaDir, ec)) { if (warn) *warn += "schema dir not found: " + schemaDir + "\n"; return out; }
    const auto seen = [&out](const std::string& v) {
        for (const auto& w : out) if (w.var == v) return true; return false;
    };
    for (auto it = fs::recursive_directory_iterator(schemaDir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const fs::path p = it->path();
        const std::string ext = codec::resource::PathKey(p.extension().string());
        if (ext != ".yaml" && ext != ".yml") continue;
        std::string text = disk::ReadFile(p, { .stripBom = true }).value_or(std::string());
        std::string kind, kerr;
        if (!schema::DescriptorKind(text, kind, kerr) || kind != "probe") continue;
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
