#include <sct-config/SctConfig.h>

namespace sct::config {

JsonBatch LoadJson(const ScanSpec& spec) {
    ScanSpec s = spec;
    if (s.extensions.empty()) s.extensions = { ".json" };

    JsonBatch batch;
    for (Source& src : Discover(s)) {
        std::optional<std::string> text = ReadText(src.path);
        if (!text) {
            batch.errors.push_back({ src, "could not read file" });
            continue;
        }
        try {
            // ignore_comments = true tolerates // and /* */ (JSONC), which mod
            // configs commonly carry.
            nlohmann::json j = nlohmann::json::parse(*text, /*cb*/ nullptr,
                                                     /*allow_exceptions*/ true,
                                                     /*ignore_comments*/ true);
            batch.docs.push_back(JsonDoc{ std::move(src), std::move(j) });
        } catch (const std::exception& e) {
            batch.errors.push_back({ src, e.what() });
        }
    }
    return batch;
}

}  // namespace sct::config
