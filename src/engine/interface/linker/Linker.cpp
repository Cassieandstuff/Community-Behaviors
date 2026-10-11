#include <interface/linker/Linker.h>

namespace CB::engine::linker {

Linker Linker::OfOrderedNames(const std::vector<std::string>& names) {
    Linker l;
    l.fwd_.reserve(names.size() * 2 + 8);
    for (std::size_t i = 0; i < names.size(); ++i) {
        const Value v = static_cast<Value>(i);
        l.fwd_.emplace(names[i], v);   // first name wins
        l.rev_.emplace(v, names[i]);   // ORIGINAL name for decode
    }
    return l;
}

std::optional<Value> Linker::encode(std::string_view name) const {
    const auto it = fwd_.find(std::string(name));
    if (it == fwd_.end()) return std::nullopt;
    return it->second;
}

std::optional<std::string> Linker::decode(Value v) const {
    const auto it = rev_.find(v);
    if (it == rev_.end()) return std::nullopt;
    return it->second;
}

}  // namespace CB::engine::linker
