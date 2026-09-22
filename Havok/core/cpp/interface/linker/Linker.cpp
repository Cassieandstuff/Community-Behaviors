#include <interface/linker/Linker.h>

namespace CB::core::linker {

Linker Linker::OfOrderedNames(const std::vector<std::string>& names) {
    Linker l;
    l.fwd_.reserve(names.size() * 2 + 8);
    for (std::size_t i = 0; i < names.size(); ++i) {
        const Value v = static_cast<Value>(i);
        l.fwd_.emplace(names[i], v);   // first name wins
        l.rev_.emplace(v, names[i]);   // value -> name (each position is unique, so this is 1:1)
    }
    return l;
}

Linker Linker::OfPairs(std::vector<std::pair<std::string, Value>> pairs) {
    Linker l;
    l.fwd_.reserve(pairs.size() * 2 + 8);
    for (auto& [name, v] : pairs) {
        l.fwd_.emplace(name, v);       // first name wins
        l.rev_.emplace(v, std::move(name));  // first value wins (ordered map, emplace keeps first)
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

}  // namespace CB::core::linker
