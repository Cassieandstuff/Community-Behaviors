#include <interface/linker/Membrane.h>

#include <cstdlib>

namespace CB::core::linker {

const Linker* BindingContext::space(std::string_view name) const {
    if (name == "roster")         return &roster;
    if (name == "events")         return &events;
    if (name == "variables")      return &variables;
    if (name == "characterProps") return &characterProps;
    if (name == "skeleton")       return &skeleton;
    if (const auto it = enums.find(std::string(name));          it != enums.end())          return &it->second;
    if (const auto it = resolvedFields.find(std::string(name)); it != resolvedFields.end()) return &it->second;
    return nullptr;
}

// ── IndexMembrane arrays ─────────────────────────────────────────────────────────────────────────
std::vector<std::optional<Value>> IndexMembrane::encodeList(const std::vector<std::string>& names) const {
    std::vector<std::optional<Value>> out;
    out.reserve(names.size());
    for (const auto& n : names) out.push_back(table ? table->encode(n) : std::nullopt);
    return out;
}

std::vector<std::optional<std::string>> IndexMembrane::decodeList(const std::vector<Value>& values) const {
    std::vector<std::optional<std::string>> out;
    out.reserve(values.size());
    for (const Value v : values) out.push_back(table ? table->decode(v) : std::nullopt);
    return out;
}

// ── BoneMembrane positional (bone weights) ───────────────────────────────────────────────────────
std::vector<std::pair<std::string, Value>> BoneMembrane::decodePositional(const std::vector<Value>& flat) const {
    std::vector<std::pair<std::string, Value>> out;
    out.reserve(flat.size());
    for (std::size_t i = 0; i < flat.size(); ++i) {
        auto nm = skeleton ? skeleton->decode(static_cast<Value>(i)) : std::nullopt;
        // Unresolved slot (bone not in this skeleton) -> a "#i" sentinel so the flat length round-trips
        // and the missing bone is visible rather than silently dropped.
        out.emplace_back(nm.value_or("#" + std::to_string(i)), flat[i]);
    }
    return out;
}

std::vector<Value> BoneMembrane::encodePositional(
    const std::vector<std::pair<std::string, Value>>& named, std::size_t count, Value fill) const {
    std::vector<Value> flat(count, fill);
    for (const auto& [name, val] : named) {
        // "#i" sentinel from decode: a bone not in the skeleton — place it back by its raw index.
        if (name.size() > 1 && name[0] == '#') {
            char* end = nullptr;
            const long i = std::strtol(name.c_str() + 1, &end, 10);
            if (end && *end == '\0' && i >= 0 && static_cast<std::size_t>(i) < count) flat[static_cast<std::size_t>(i)] = val;
            continue;
        }
        if (auto slot = skeleton ? skeleton->encode(name) : std::nullopt)
            if (*slot >= 0 && static_cast<std::size_t>(*slot) < count) flat[static_cast<std::size_t>(*slot)] = val;
    }
    return flat;
}

// ── BoneMembrane index-list (hkbBoneIndexArray) ──────────────────────────────────────────────────
std::vector<std::optional<std::string>> BoneMembrane::decodeIndexList(const std::vector<Value>& indices) const {
    std::vector<std::optional<std::string>> out;
    out.reserve(indices.size());
    for (const Value i : indices) out.push_back(skeleton ? skeleton->decode(i) : std::nullopt);
    return out;
}

std::vector<std::optional<Value>> BoneMembrane::encodeIndexList(const std::vector<std::string>& names) const {
    std::vector<std::optional<Value>> out;
    out.reserve(names.size());
    for (const auto& n : names) out.push_back(skeleton ? skeleton->encode(n) : std::nullopt);
    return out;
}

}  // namespace CB::core::linker
