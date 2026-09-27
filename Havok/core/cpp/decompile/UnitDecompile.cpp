// UnitDecompile — see UnitDecompile.h. The schema unit-decompile dispatcher, shared by the converter
// and tree-diff (replaces the typed havok-core DecompileToDir). Each dialect routes to its schema
// codec/decompiler; all are individually byte-gated (character-schema-parity 46/46, behavior emit 17/17,
// project ReadProject+AssembleProject, animation 6116 round-trip), so the dispatcher inherits their fidelity.

#include <decompile/UnitDecompile.h>

#include <decompile/CharacterDecompile.h>    // DecompileCharacterSchema
#include <decompile/BehaviorDecompile.h>     // CB::core::decompile::DecompileBehaviorSchema
#include <decompile/AnimationDecompiler.h>   // anim::DecompileAnimation
#include <compile/ProjectRead.h>             // CB::core::compile::ReadProject
#include <codec/format/ProjectYaml.h>        // CB::core::codec::EmitProjectYaml
#include <codec/serialization/HavokIo.h>     // io::MakeSchemaFactory / io::SchemaObject
#include <codec/serialization/packfile/PackFileDeserializer.h>
#include <interface/reflection/HavokSchema.h> // schema::SharedRegistry / SchemaRegistry

#include <filesystem>
#include <fstream>

namespace CB::core::decompile {
using namespace CB::core::codec;
using namespace CB::core::common;
namespace fs = std::filesystem;

UnitDecompileResult DecompileUnit(const std::vector<std::uint8_t>& bytes, const std::string& outDir) {
    const fs::path dir = outDir;
    // Cheap root sniff (partial deserialize, like the typed path's project/spline dispatch).
    bool hasProject = false, hasSpline = false;
    try {
        CB::core::codec::PackFileDeserializer pd;
        CB::core::codec::BinaryReaderEx br(/*bigEndian*/ false, /*uSizeLong*/ true, bytes);
        pd.DeserializePartially(br);
        for (const auto& [off, cls] : pd.ListObjects()) {
            if (cls == "hkbProjectData")                    hasProject = true;
            else if (cls == "hkaSplineCompressedAnimation") hasSpline  = true;
        }
    } catch (const std::exception& e) { return { false, std::string("partial deserialize: ") + e.what(), "" }; }

    if (hasProject) {
        const auto pr = CB::core::compile::ReadProject(bytes);
        if (!pr.ok) return { false, pr.error, "project" };
        std::error_code ec; fs::create_directories(dir, ec);
        std::ofstream(dir / "project.yaml", std::ios::binary) << CB::core::codec::EmitProjectYaml(pr.spec);
        return { true, "", "project" };
    }
    if (hasSpline) {
        const auto ar = CB::core::anim::DecompileAnimation(bytes, dir, nullptr);
        return { ar.ok, ar.error, "animation" };
    }

    // character / behavior: full schema graph walk (needs the shared registry).
    CB::core::schema::SchemaRegistry* reg = CB::core::schema::SharedRegistry();
    if (!reg) return { false, "shared schema registry unavailable", "" };
    CB::core::codec::PackFileDeserializer des;
    des.ObjectFactory = CB::core::codec::io::MakeSchemaFactory(*reg);
    try { CB::core::codec::BinaryReaderEx br(false, true, bytes); des.Deserialize(br); }
    catch (const std::exception& e) { return { false, std::string("deserialize: ") + e.what(), "" }; }

    const CB::core::codec::io::SchemaObject* cd = nullptr;
    bool hasBehavior = false;
    for (const auto& [off, o] : des.DeserializedObjects()) {
        const auto* so = dynamic_cast<const CB::core::codec::io::SchemaObject*>(o.get());
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
        const bool ok = CB::core::decompile::DecompileBehaviorSchema(bytes, "", *reg, dir.string(), derr);
        return { ok, derr, "behavior" };
    }
    return { false, "unrecognized root variant (not project/animation/character/behavior)", "unknown" };
}

} // namespace CB::core::decompile
