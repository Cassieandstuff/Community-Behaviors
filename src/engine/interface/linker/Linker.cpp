#include <interface/linker/Linker.h>

#include <cctype>

namespace CB::core::linker {

std::string Linker::fold(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

Linker Linker::OfOrderedNames(const std::vector<std::string>& names, bool caseFold) {
    Linker l;
    l.caseFold_ = caseFold;
    l.fwd_.reserve(names.size() * 2 + 8);
    for (std::size_t i = 0; i < names.size(); ++i) {
        const Value v = static_cast<Value>(i);
        l.fwd_.emplace(caseFold ? fold(names[i]) : names[i], v);  // first name wins on the (folded) key
        l.rev_.emplace(v, names[i]);                              // ORIGINAL name for decode
    }
    return l;
}

Linker Linker::OfPairs(std::vector<std::pair<std::string, Value>> pairs, bool caseFold) {
    Linker l;
    l.caseFold_ = caseFold;
    l.fwd_.reserve(pairs.size() * 2 + 8);
    for (auto& [name, v] : pairs) {
        l.fwd_.emplace(caseFold ? fold(name) : name, v);   // first name wins
        l.rev_.emplace(v, std::move(name));                // first value wins (ordered map keeps first); ORIGINAL name
    }
    return l;
}

std::optional<Value> Linker::encode(std::string_view name) const {
    const auto it = fwd_.find(caseFold_ ? fold(name) : std::string(name));
    if (it == fwd_.end()) return std::nullopt;
    return it->second;
}

std::optional<std::string> Linker::decode(Value v) const {
    const auto it = rev_.find(v);
    if (it == rev_.end()) return std::nullopt;
    return it->second;
}

}  // namespace CB::core::linker
