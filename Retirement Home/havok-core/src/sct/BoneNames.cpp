#include "havok/sct/BoneNames.h"

#include <cctype>

namespace havok::sct {

namespace {
    std::string ToLower(std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }
    std::string Trim(const std::string& s) {
        std::size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return {};
        std::size_t e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    }
}  // namespace

void BoneNameTable::Reindex() {
    lut.clear();
    lut.reserve(names.size() * 2 + 8);
    for (int i = 0; i < static_cast<int>(names.size()); ++i)
        lut.emplace(ToLower(names[static_cast<std::size_t>(i)]), i);   // first wins on a dup name
}

const std::string* BoneNameTable::NameOf(int index) const {
    if (index < 0 || index >= static_cast<int>(names.size())) return nullptr;
    return &names[static_cast<std::size_t>(index)];
}

int BoneNameTable::IndexOf(const std::string& name) const {
    auto it = lut.find(ToLower(name));
    return it == lut.end() ? -1 : it->second;
}

BoneNameTable ParseBoneList(const std::string& text) {
    BoneNameTable t;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        const std::string b = Trim(line);
        if (!b.empty() && b[0] != '#' && b[0] != ';') t.names.push_back(b);
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    t.Reindex();
    return t;
}

std::string EmitBoneList(const BoneNameTable& table) {
    std::string out;
    for (const auto& n : table.names) out += n + "\r\n";
    return out;
}

void MergeBoneList(BoneNameTable& base, const BoneNameTable& overrideList) {
    for (const auto& n : overrideList.names)
        if (base.IndexOf(n) < 0) {         // genuinely new bone — append (keeps base indices)
            base.names.push_back(n);
            base.lut.emplace(ToLower(n), static_cast<int>(base.names.size()) - 1);
        }
}

}  // namespace havok::sct
