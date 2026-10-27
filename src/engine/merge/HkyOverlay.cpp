#include <merge/HkyOverlay.h>

#include <codec/resource/PathKey.h>

#include <unordered_set>

namespace CB::engine::merge {

namespace {

using codec::resource::PathKey;

/// @brief The project bundle's stem, or empty.
std::string ProjectStem(const LoadOrderDocument& doc) {
    for (const auto& b : doc.Bundles())
        if (b.provider == "project") return b.stem;
    return {};
}

}  // namespace

std::vector<std::string> HkyOverlay::Paths() const {
    std::vector<std::string> out;
    if (!m_doc) return out;
    const std::string project = ProjectStem(*m_doc);
    const auto        inScope = [&](const std::vector<std::string>& stems) {
        if (stems.empty()) return false;
        if (m_scope == Scope::Project) return !project.empty() && stems.back() == project;
        for (const auto& s : stems)
            if (s != project) return true;
        return false;
    };

    std::unordered_set<std::string> units;
    for (const auto& u : m_doc->Units()) {
        units.insert(u.key);
        if (inScope(u.layers)) out.push_back(u.path);
    }
    for (const auto& f : m_doc->Files()) {
        const std::string key = PathKey(f);
        if (key.find('/') == std::string::npos || key.starts_with("animationnames/")) continue;
        /// @note A unit's own files (`<unit>.hkx/graph.yaml`, its node folders) are the unit, listed above.
        bool inUnit = false;
        for (std::size_t at = key.find(".hkx/"); at != std::string::npos && !inUnit; at = key.find(".hkx/", at + 1))
            inUnit = units.count(key.substr(0, at + 4)) != 0;
        if (!inUnit && inScope(m_doc->Providers(f))) out.push_back(f);
    }
    return out;
}

std::optional<std::vector<std::uint8_t>> HkyOverlay::Read(std::string_view path, std::string* err) const {
    std::vector<std::uint8_t> out;
    if (!m_doc) {
        if (err) *err = "no load order";
        return std::nullopt;
    }
    if (m_doc->UnitAt(path)) {
        if (m_doc->GraphBytes(path, out, err)) return out;
        return std::nullopt;
    }
    const auto text = m_doc->Text(path);
    if (!text) {
        if (err) *err = "not in the .hky load order: " + std::string(path);
        return std::nullopt;
    }
    return std::vector<std::uint8_t>(text->begin(), text->end());
}

}  // namespace CB::engine::merge
