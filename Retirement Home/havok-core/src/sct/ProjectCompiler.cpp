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

// ReadProject moved to Havok/core/cpp/compile/ProjectRead.{h,cpp} (schema-native, firesale).
// This file keeps the typed BuildProject/RoundTrip/Rewrite path + LoadProjectGraph (the offline
// oracle / editor byte-exact rewrite), which still uses the typed hkb* classes.

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


} // namespace havok::sct
