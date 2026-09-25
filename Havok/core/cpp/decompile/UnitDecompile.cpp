// UnitDecompile — see UnitDecompile.h. The schema unit-decompile dispatcher, shared by the converter
// and tree-diff (replaces the typed havok-core DecompileToDir). Each dialect routes to its schema
// codec/decompiler; all are individually byte-gated (character-schema-parity 46/46, behavior emit 17/17,
// project ReadProject+AssembleProject, animation 6116 round-trip), so the dispatcher inherits their fidelity.

#include <decompile/UnitDecompile.h>

#include <decompile/CharacterDecompile.h>    // DecompileCharacterSchema
#include <decompile/BehaviorDecompile.h>     // model::DecompileBehaviorSchema
#include <decompile/AnimationDecompiler.h>   // anim::DecompileAnimation
#include <compile/ProjectRead.h>             // sct::ReadProject
#include <codec/format/ProjectYaml.h>        // sct::EmitProjectYaml
#include <codec/serialization/HavokIo.h>     // io::MakeSchemaFactory / io::SchemaObject
#include <codec/serialization/packfile/PackFileDeserializer.h>
#include <interface/reflection/HavokSchema.h> // schema::SharedRegistry / SchemaRegistry

#include <filesystem>
#include <fstream>

namespace havok::decompile {
namespace fs = std::filesystem;

UnitDecompileResult DecompileUnit(const std::vector<std::uint8_t>& bytes, const std::string& outDir) {
    const fs::path dir = outDir;
    // Cheap root sniff (partial deserialize, like the typed path's project/spline dispatch).
    bool hasProject = false, hasSpline = false;
    try {
        havok::PackFileDeserializer pd;
        havok::BinaryReaderEx br(/*bigEndian*/ false, /*uSizeLong*/ true, bytes);
        pd.DeserializePartially(br);
        for (const auto& [off, cls] : pd.ListObjects()) {
            if (cls == "hkbProjectData")                    hasProject = true;
            else if (cls == "hkaSplineCompressedAnimation") hasSpline  = true;
        }
    } catch (const std::exception& e) { return { false, std::string("partial deserialize: ") + e.what(), "" }; }

    if (hasProject) {
        const auto pr = havok::sct::ReadProject(bytes);
        if (!pr.ok) return { false, pr.error, "project" };
        std::error_code ec; fs::create_directories(dir, ec);
        std::ofstream(dir / "project.yaml", std::ios::binary) << havok::sct::EmitProjectYaml(pr.spec);
        return { true, "", "project" };
    }
    if (hasSpline) {
        const auto ar = havok::anim::DecompileAnimation(bytes, dir, nullptr);
        return { ar.ok, ar.error, "animation" };
    }

    // character / behavior: full schema graph walk (needs the shared registry).
    havok::schema::SchemaRegistry* reg = havok::schema::SharedRegistry();
    if (!reg) return { false, "shared schema registry unavailable", "" };
    havok::PackFileDeserializer des;
    des.ObjectFactory = havok::io::MakeSchemaFactory(*reg);
    try { havok::BinaryReaderEx br(false, true, bytes); des.Deserialize(br); }
    catch (const std::exception& e) { return { false, std::string("deserialize: ") + e.what(), "" }; }

    const havok::io::SchemaObject* cd = nullptr;
    bool hasBehavior = false;
    for (const auto& [off, o] : des.DeserializedObjects()) {
        const auto* so = dynamic_cast<const havok::io::SchemaObject*>(o.get());
        if (!so) continue;
        const std::string cn = so->ClassName();
        if (cn == "hkbCharacterData") { cd = so; break; }
        if (cn == "hkbBehaviorGraph")  hasBehavior = true;
    }
    if (cd) {
        const auto cr = DecompileCharacterSchema(*cd, dir);
        return { cr.ok, cr.error, "character" };
    }
    if (hasBehavior) {
        std::string derr;
        const bool ok = havok::model::DecompileBehaviorSchema(bytes, "", *reg, dir.string(), derr);
        return { ok, derr, "behavior" };
    }
    return { false, "unrecognized root variant (not project/animation/character/behavior)", "unknown" };
}

} // namespace havok::decompile
