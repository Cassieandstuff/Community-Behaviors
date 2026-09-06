#include <sct-config/SctConfig.h>

namespace sct::config {

YamlBatch LoadYaml(const ScanSpec& spec) {
    ScanSpec s = spec;
    if (s.extensions.empty()) s.extensions = { ".yaml", ".yml" };

    YamlBatch batch;
    for (Source& src : Discover(s)) {
        std::optional<std::string> text = ReadText(src.path);
        if (!text) {
            batch.errors.push_back({ src, "could not read file" });
            continue;
        }
        try {
            // parse_in_arena copies into ryml's own arena, so the Tree is
            // self-contained and safe to move into the batch — unlike
            // parse_in_place, which would alias the local `text` buffer.
            c4::yml::Tree tree = c4::yml::parse_in_arena(c4::to_csubstr(*text));
            batch.docs.push_back(YamlDoc{ std::move(src), std::move(tree) });
        } catch (const std::exception& e) {
            batch.errors.push_back({ src, e.what() });
        }
    }
    return batch;
}

}  // namespace sct::config
