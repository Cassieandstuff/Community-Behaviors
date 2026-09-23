#include "interface/MotionSample.h"

#include <string>
#include <vector>

namespace havok::animdata {

std::string LabelSample(const std::string& verbatim) {
    static const char* const kAxes[] = { "t", "x", "y", "z", "w" };
    std::vector<std::string> toks;
    std::string              cur;
    for (char c : verbatim) { if (c == ' ') { if (!cur.empty()) { toks.push_back(cur); cur.clear(); } } else cur += c; }
    if (!cur.empty()) toks.push_back(cur);
    std::string out;
    for (std::size_t i = 0; i < toks.size(); ++i) {
        if (i) out += ", ";
        if (i < 5) { out += kAxes[i]; out += ": "; }   // >5 tokens (never for motion): leave bare
        out += toks[i];
    }
    return out;
}

std::string UnlabelSample(const std::string& s) {
    const char sep = (s.find(',') != std::string::npos) ? ',' : ' ';
    std::string out;
    bool        first = true;
    std::string cur;
    auto flush = [&] {
        auto b = cur.find_first_not_of(" \t");
        if (b != std::string::npos) {
            auto        e   = cur.find_last_not_of(" \t");
            std::string seg = cur.substr(b, e - b + 1);
            if (const auto colon = seg.find(':'); colon != std::string::npos) {   // strip "axis:" prefix
                auto vb = seg.find_first_not_of(" \t", colon + 1);
                seg     = (vb == std::string::npos) ? std::string() : seg.substr(vb);
            }
            if (!seg.empty()) { if (!first) out += ' '; out += seg; first = false; }
        }
        cur.clear();
    };
    for (char c : s) { if (c == sep) flush(); else cur += c; }
    flush();
    return out;
}

}  // namespace havok::animdata
