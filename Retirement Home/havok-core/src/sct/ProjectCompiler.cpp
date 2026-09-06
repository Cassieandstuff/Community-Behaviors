#include "havok/sct/ProjectCompiler.h"

#include "havok/classes/Classes.h"
#include "havok/core/PackFileDeserializer.h"
#include "havok/core/PackFileSerializer.h"

#include "SchemaCompilerState.h"   // shared data-driven-compiler toggle + schema registry

#include <havok-model/HavokModel.h>   // model::AssembleProject (the schema-driven emit)
#include <havok-schema/HavokSchema.h> // schema::SchemaRegistry

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <sstream>

namespace havok::sct {
namespace {

// Deserialize project bytes to (root, projectData, stringData). Returns null root
// on any structural failure (message in outErr).
std::shared_ptr<hkRootLevelContainer>
LoadProjectGraph(const std::vector<std::uint8_t>& bytes, HKXHeader& outHeader,
                 std::shared_ptr<hkbProjectData>& outData,
                 std::shared_ptr<hkbProjectStringData>& outStrings,
                 std::string& outErr) {
    try {
        PackFileDeserializer des;
        BinaryReaderEx br(/*bigEndian*/ false, /*uSizeLong*/ true, bytes);
        auto root = std::dynamic_pointer_cast<hkRootLevelContainer>(des.Deserialize(br));
        outHeader = des._header;
        if (!root || root->m_namedVariants.empty()) {
            outErr = "not a project packfile (no hkRootLevelContainer variant)";
            return nullptr;
        }
        auto data = std::dynamic_pointer_cast<hkbProjectData>(root->m_namedVariants[0].m_variant);
        if (!data) {
            outErr = "root variant is not hkbProjectData";
            return nullptr;
        }
        outData    = data;
        outStrings = data->m_stringData;
        if (!outStrings) {
            outErr = "hkbProjectData has no stringData";
            return nullptr;
        }
        return root;
    } catch (const std::exception& e) {
        outErr = e.what();
        return nullptr;
    }
}

CompileResult SerializeRoot(const std::shared_ptr<hkRootLevelContainer>& root, const HKXHeader& header) {
    CompileResult r;
    try {
        PackFileSerializer ser;
        BinaryWriterEx bw;
        ser.Serialize(std::static_pointer_cast<IHavokObject>(root), bw, header);
        r.bytes = bw.Data();
        r.ok    = true;
    } catch (const std::exception& e) {
        r.ok = false;
        r.error = e.what();
    }
    return r;
}

} // namespace

ProjectReadResult ReadProject(const std::vector<std::uint8_t>& bytes) {
    ProjectReadResult out;
    std::shared_ptr<hkbProjectData>       data;
    std::shared_ptr<hkbProjectStringData> strings;
    auto root = LoadProjectGraph(bytes, out.header, data, strings, out.error);
    if (!root) return out;

    out.spec.worldUpWS = { data->m_worldUpWS.x, data->m_worldUpWS.y,
                           data->m_worldUpWS.z, data->m_worldUpWS.w };
    out.spec.defaultEventMode  = data->m_defaultEventMode;
    out.spec.animationFilenames = strings->m_animationFilenames;
    out.spec.behaviorFilenames  = strings->m_behaviorFilenames;
    out.spec.characterFilenames = strings->m_characterFilenames;
    out.spec.eventNames         = strings->m_eventNames;
    out.spec.animationPath      = strings->m_animationPath;
    out.spec.behaviorPath       = strings->m_behaviorPath;
    out.spec.characterPath      = strings->m_characterPath;
    out.spec.fullPathToSource   = strings->m_fullPathToSource;
    out.ok = true;
    return out;
}

CompileResult BuildProject(const ProjectSpec& spec, const HKXHeader& header) {
    // Data-driven path (opt-in): assemble via the Havok/ schema descriptors (model::AssembleProject),
    // proven byte-identical to the typed path below. Any failure falls through to typed — enabling the
    // schema compiler can only match or fall back, never serve a worse project.
    if (SchemaCompileEnabled()) {
        if (schema::SchemaRegistry* reg = SchemaCompileRegistry()) {
            try {
                if (auto sroot = model::AssembleProject(spec, *reg)) {
                    CompileResult r;
                    PackFileSerializer ser;
                    BinaryWriterEx bw;
                    ser.Serialize(sroot, bw, header);
                    r.bytes = bw.Data();
                    r.ok    = true;
                    return r;
                }
            } catch (const std::exception&) { /* fall through to the typed builder */ }
        }
    }

    auto strings = std::make_shared<hkbProjectStringData>();
    strings->m_animationFilenames = spec.animationFilenames;
    strings->m_behaviorFilenames  = spec.behaviorFilenames;
    strings->m_characterFilenames = spec.characterFilenames;
    strings->m_eventNames         = spec.eventNames;
    strings->m_animationPath      = spec.animationPath;
    strings->m_behaviorPath       = spec.behaviorPath;
    strings->m_characterPath      = spec.characterPath;
    strings->m_fullPathToSource   = spec.fullPathToSource;

    auto data = std::make_shared<hkbProjectData>();
    data->m_worldUpWS = Vector4{ spec.worldUpWS[0], spec.worldUpWS[1],
                                 spec.worldUpWS[2], spec.worldUpWS[3] };
    data->m_stringData       = strings;
    data->m_defaultEventMode = spec.defaultEventMode;

    auto root = std::make_shared<hkRootLevelContainer>();
    hkRootLevelContainerNamedVariant nv;
    nv.m_name      = "hkbProjectData";
    nv.m_className = "hkbProjectData";
    nv.m_variant   = data;
    root->m_namedVariants.push_back(std::move(nv));
    return SerializeRoot(root, header);
}

CompileResult RoundTripProject(const std::vector<std::uint8_t>& bytes) {
    CompileResult r;
    HKXHeader header;
    std::shared_ptr<hkbProjectData>       data;
    std::shared_ptr<hkbProjectStringData> strings;
    std::string err;
    auto root = LoadProjectGraph(bytes, header, data, strings, err);
    if (!root) { r.error = err; return r; }
    return SerializeRoot(root, header);
}

CompileResult RewriteProjectCharacterFilenames(const std::vector<std::uint8_t>& bytes,
                                               const std::vector<std::string>& newCharacterFilenames) {
    CompileResult r;
    HKXHeader header;
    std::shared_ptr<hkbProjectData>       data;
    std::shared_ptr<hkbProjectStringData> strings;
    std::string err;
    auto root = LoadProjectGraph(bytes, header, data, strings, err);
    if (!root) { r.error = err; return r; }
    if (newCharacterFilenames.size() != strings->m_characterFilenames.size()) {
        r.error = "characterFilenames count mismatch (have " +
                  std::to_string(strings->m_characterFilenames.size()) + ", got " +
                  std::to_string(newCharacterFilenames.size()) + ")";
        return r;
    }
    strings->m_characterFilenames = newCharacterFilenames;
    return SerializeRoot(root, header);
}

CompileResult RewriteProjectCharacterFilename(const std::vector<std::uint8_t>& bytes,
                                              const std::string& newFirst) {
    CompileResult r;
    HKXHeader header;
    std::shared_ptr<hkbProjectData>       data;
    std::shared_ptr<hkbProjectStringData> strings;
    std::string err;
    auto root = LoadProjectGraph(bytes, header, data, strings, err);
    if (!root) { r.error = err; return r; }
    if (strings->m_characterFilenames.empty()) {
        r.error = "project has no characterFilenames to rewrite";
        return r;
    }
    strings->m_characterFilenames[0] = newFirst;
    return SerializeRoot(root, header);
}

// ── project.yaml serde (co-located; plain text, backslashes verbatim) ─────────
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
