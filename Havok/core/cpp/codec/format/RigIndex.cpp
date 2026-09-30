#include <codec/format/RigIndex.h>

#include <algorithm>
#include <cctype>

namespace CB::core::skeleton {

namespace {
void bind(std::unordered_map<std::string, std::vector<std::string>>& m, const std::string& k, const std::string& rig) {
    auto& v = m[k];
    if (std::find(v.begin(), v.end(), rig) == v.end()) v.push_back(rig);
}
}  // namespace

std::string RigIndex::DirOf(const std::string& key) {
    const auto p = key.rfind('/');
    return p == std::string::npos ? std::string{} : key.substr(0, p);
}

std::string RigIndex::JoinRel(const std::string& dir, const std::string& rel) {
    std::vector<std::string> parts;
    auto push = [&parts](std::string path) {
        for (char& c : path) c = (c == '\\') ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::size_t s = 0;
        while (s <= path.size()) {
            const std::size_t e   = std::min(path.find('/', s), path.size());
            const std::string seg = path.substr(s, e - s);
            if (seg == "..") { if (!parts.empty()) parts.pop_back(); }
            else if (!seg.empty() && seg != ".") parts.push_back(seg);
            s = e + 1;
        }
    };
    push(dir);
    push(rel);
    std::string out;
    for (const auto& p : parts) { if (!out.empty()) out += '/'; out += p; }
    return out;
}

void RigIndex::AddProject(const std::string& projUnit, const std::vector<std::string>& characterFilenames,
                          const std::function<std::optional<CharacterRig>(const std::string&)>& loadChar,
                          const std::function<bool(const std::string&)>& hasSkeleton,
                          const std::function<void(const std::string&)>& warn) {
    const std::string projDir = DirOf(projUnit);
    for (const std::string& cf : characterFilenames) {
        const std::string charUnit = JoinRel(projDir, cf);
        const auto def = loadChar(charUnit);
        if (!def || def->rig.empty()) continue;
        const std::string rig = JoinRel(projDir, def->rig);
        if (!hasSkeleton(rig)) {
            if (warn) warn("character '" + charUnit + "' rig '" + rig + "' has no skeleton unit");
            continue;
        }
        bind(m_byProjectDir, projDir, rig);
        if (!def->behavior.empty()) {
            const std::string beh = JoinRel(projDir, def->behavior);
            bind(m_byBehavior, beh, rig);
            bind(m_byBehaviorDir, DirOf(beh), rig);
        }
        m_bound.insert(rig);
    }
}

void RigIndex::Finalize() {
    for (auto* m : { &m_byBehavior, &m_byBehaviorDir, &m_byProjectDir })
        for (auto& [k, v] : *m) std::sort(v.begin(), v.end());
}

const std::vector<std::string>* RigIndex::RigsFor(const std::string& key) const {
    std::string k = key;
    for (char& c : k) c = (c == '\\') ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (const auto it = m_byBehavior.find(k); it != m_byBehavior.end()) return &it->second;
    if (const auto it = m_byBehaviorDir.find(DirOf(k)); it != m_byBehaviorDir.end()) return &it->second;
    for (std::string d = DirOf(k); !d.empty(); d = DirOf(d))
        if (const auto it = m_byProjectDir.find(d); it != m_byProjectDir.end()) return &it->second;
    return nullptr;
}

}  // namespace CB::core::skeleton
