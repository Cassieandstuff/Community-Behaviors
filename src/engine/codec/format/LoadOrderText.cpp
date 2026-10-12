#include "codec/format/LoadOrderText.h"

#include <codec/resource/PathKey.h>
#include <disk/Disk.h>
#include <text/Text.h>

#include <utility>

namespace CB::engine::codec {

bool IsLoadOrderEntry(std::string_view line)
{
    const std::string_view t = text::Trim(line);
    return !t.empty() && t[0] != '#' && t[0] != ';';
}

int LoadOrder::Rank(std::string_view bundleStem) const
{
    const auto it = m_rank.find(resource::PathKey(bundleStem));
    return it != m_rank.end() ? it->second : 0;
}

LoadOrder ParseLoadOrder(std::string_view fileText)
{
    LoadOrder lo;
    std::vector<std::pair<std::string, std::string_view>> listed;
    for (const std::string_view line : text::SplitLines(text::StripBom(fileText))) {
        lo.lines.emplace_back(line);
        if (!IsLoadOrderEntry(line)) continue;
        const std::string_view entry = text::Trim(line);
        std::string            key   = resource::PathKey(entry);
        lo.m_rank[key]               = static_cast<int>(listed.size()) + 1;
        listed.emplace_back(std::move(key), entry);
    }
    for (std::size_t i = 0; i < listed.size(); ++i)
        if (lo.m_rank.at(listed[i].first) == static_cast<int>(i) + 1) lo.entries.emplace_back(listed[i].second);
    return lo;
}

std::optional<LoadOrder> ReadLoadOrder(const std::filesystem::path& p)
{
    const auto file = disk::ReadFile(p);
    if (!file) return std::nullopt;
    return ParseLoadOrder(*file);
}

std::string EmitLoadOrder(const std::vector<std::string>& lines)
{
    std::string body;
    for (const auto& l : lines) { body += l; body += '\n'; }
    return body;
}

bool WriteLoadOrder(const std::filesystem::path& p, const std::vector<std::string>& lines, std::string* err)
{
    return disk::WriteFile(p, EmitLoadOrder(lines), { .atomic = true }, err);
}

}  // namespace CB::engine::codec
