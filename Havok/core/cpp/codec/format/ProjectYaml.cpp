// ProjectYaml — the project.yaml <-> ProjectSpec format codec (see ProjectYaml.h). Drained verbatim
// out of the quarantined havok-core ProjectCompiler.cpp: plain text, backslashes verbatim, %g floats,
// std-only. The two thin leaf halves the decompile/compile project wrappers assemble.
#include <codec/format/ProjectYaml.h>

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace havok::sct {

namespace {
    std::string g4(float f) { char b[32]; std::snprintf(b, sizeof(b), "%g", f); return b; }
    std::string trimws(const std::string& s) {
        const auto a = s.find_first_not_of(" \t");
        if (a == std::string::npos) return {};
        const auto z = s.find_last_not_of(" \t\r");
        return s.substr(a, z - a + 1);
    }
}

std::string EmitProjectYaml(const ProjectSpec& s) {
    std::string o;
    o += "worldUpWS: [" + g4(s.worldUpWS[0]) + ", " + g4(s.worldUpWS[1]) + ", " +
         g4(s.worldUpWS[2]) + ", " + g4(s.worldUpWS[3]) + "]\n";
    o += "defaultEventMode: " + std::to_string(static_cast<int>(s.defaultEventMode)) + "\n";
    const auto list = [&o](const char* k, const std::vector<std::string>& v) {
        if (v.empty()) { o += std::string(k) + ": []\n"; return; }
        o += std::string(k) + ":\n";
        for (const auto& e : v) o += "  - " + e + "\n";
    };
    list("animationFilenames", s.animationFilenames);
    list("behaviorFilenames",  s.behaviorFilenames);
    list("characterFilenames", s.characterFilenames);
    list("eventNames",         s.eventNames);
    o += "animationPath: "    + s.animationPath    + "\n";
    o += "behaviorPath: "     + s.behaviorPath     + "\n";
    o += "characterPath: "    + s.characterPath    + "\n";
    o += "fullPathToSource: " + s.fullPathToSource + "\n";
    return o;
}

bool ParseProjectYaml(const std::string& text, ProjectSpec& out) {
    out = ProjectSpec{};
    std::istringstream is(text);
    std::string line;
    std::vector<std::string>* curList = nullptr;
    while (std::getline(is, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // list item: "  - <value>" (value kept verbatim, backslashes and all)
        if (const auto d = line.find("- "); d != std::string::npos &&
            trimws(line.substr(0, d)).empty()) {
            if (curList) curList->push_back(trimws(line.substr(d + 2)));
            continue;
        }
        const auto c = line.find(':');
        if (c == std::string::npos) { curList = nullptr; continue; }
        const std::string key = trimws(line.substr(0, c));
        const std::string val = trimws(line.substr(c + 1));
        curList = nullptr;
        if (key == "worldUpWS") {
            std::string inner = val;
            if (!inner.empty() && inner.front() == '[') inner = inner.substr(1);
            if (!inner.empty() && inner.back()  == ']') inner.pop_back();
            std::stringstream ss(inner); std::string tok; int i = 0;
            while (std::getline(ss, tok, ',') && i < 4)
                out.worldUpWS[i++] = std::strtof(trimws(tok).c_str(), nullptr);
        } else if (key == "defaultEventMode") {
            out.defaultEventMode = static_cast<std::int8_t>(std::atoi(val.c_str()));
        } else if (key == "animationFilenames") { if (val != "[]") curList = &out.animationFilenames; }
          else if (key == "behaviorFilenames")  { if (val != "[]") curList = &out.behaviorFilenames;  }
          else if (key == "characterFilenames") { if (val != "[]") curList = &out.characterFilenames; }
          else if (key == "eventNames")         { if (val != "[]") curList = &out.eventNames;         }
          else if (key == "animationPath")    out.animationPath    = val;
          else if (key == "behaviorPath")     out.behaviorPath     = val;
          else if (key == "characterPath")    out.characterPath    = val;
          else if (key == "fullPathToSource") out.fullPathToSource = val;
    }
    return true;
}

} // namespace havok::sct
