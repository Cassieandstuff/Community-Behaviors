// Compile-check for the CB-API facade — ensures <CB-API.h> and every name it re-exports actually
// resolves against the engine modules. Not shipped; built by the cb-api-check target so a plain
// build fails loudly if the public contract drifts. This TU is Community Behaviors acting as a
// FIRST-PARTY CONSUMER of its own public API — the same one door a downstream tool uses.
#include <CB-API.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {
    // Odr-use each re-exported name so a missing/renamed underlying type or function is a compile
    // error here, not a silent gap a downstream tool discovers. Never executed.
    [[maybe_unused]] void cb_api_contract() {
        // cb::resolve — read a bundle + merge a load order → the merged graph
        std::string err;
        std::shared_ptr<cb::resolve::Archive> arc = cb::resolve::Archive::LoadFromFile("", err);
        (void)arc;
        cb::resolve::UnitKind k = cb::resolve::UnitKind::Behavior; (void)k;
        const cb::resolve::UnitSource* src = nullptr; (void)src;

        // cb::schema — the registry
        cb::schema::Registry reg; (void)reg;

        cb::resolve::Graph g;
        bool gok = cb::resolve::MergeLayersReflective(std::vector<cb::resolve::Layer>{}, reg, nullptr, g, err); (void)gok;
        cb::resolve::GraphEdit edit(g, reg); (void)edit;
        std::vector<std::uint8_t> bytes;
        bool sok = cb::resolve::SerializeGraph(g, bytes, err); (void)sok;
        auto* parse  = static_cast<bool (*)(const cb::resolve::UnitSource&, const cb::schema::Registry&, cb::resolve::Graph&,
                                            std::string&, const std::vector<std::string>*)>(&cb::resolve::ParseHkyReflective);
        (void)parse;
        auto* patch  = &cb::resolve::MergeHkyPatch;               (void)patch;
        auto* derive = static_cast<bool (*)(const cb::resolve::Graph&, const std::vector<std::string>&,
                                            const std::vector<std::string>&, const std::vector<std::uint8_t>&,
                                            const cb::schema::Registry&, const std::string&, std::string&,
                                            const std::vector<std::string>*, cb::resolve::PatchStats*)>(&cb::resolve::DeriveHkyPatch);
        (void)derive;
        auto* decomp = &cb::resolve::DecompileBehaviorReflective; (void)decomp;
        if (g.root) {
            std::shared_ptr<cb::resolve::Object> chr = cb::resolve::FindVariant(*g.root, "hkbCharacterData");
            const cb::resolve::FieldValue* up = chr ? chr->FindField("modelUpMS") : nullptr; (void)up;
            if (chr) (void)cb::resolve::AsSchema(chr->Children("legs").front());
        }
        auto* charStrings = &cb::resolve::ReadCharacterStrings; (void)charStrings;
        cb::resolve::CharacterStrings cs; (void)cs;

        cb::schema::SetSharedSchemaDir("");
        std::string sdir = cb::schema::SharedSchemaDir(); (void)sdir;
        cb::schema::Registry* shared = cb::schema::SharedRegistry(); (void)shared;
        const std::string& serr = cb::schema::SharedRegistryError(); (void)serr;
        long mode = cb::schema::EnumTable("PlaybackMode").at("MODE_LOOPING"); (void)mode;
        const float quad[4] = {0.f, 0.f, 0.f, 0.f};
        auto addedQuad = edit.AddQuadVariable("v", "VARIABLE_TYPE_VECTOR4", quad, err); (void)addedQuad;

        // cb::anim — decode / compile / export
        cb::anim::Def def; (void)def;
        cb::anim::AnimTrackDef track; track.translation.push_back(cb::anim::Vec3Keyframe{}); track.rotation.push_back(cb::anim::QuatKeyframe{});
        cb::anim::AnimFloatTrackDef ftrack; ftrack.keyframes.push_back(cb::anim::FloatKeyframe{});
        cb::anim::AnimAnnotationTrackDef atrack; atrack.annotations.push_back(cb::anim::AnimAnnotation{});
        cb::anim::AnimCompressionParams cparams; (void)cparams;
        cb::anim::Def loaded = cb::anim::YamlLoader::LoadFromString("", "<check>"); (void)loaded;
        cb::anim::CompileResult   cr = cb::anim::CompileAnimation(def);            (void)cr;
        cb::anim::DecompileResult dr = cb::anim::DecompileAnimation(std::vector<std::uint8_t>{},
                                                                   std::filesystem::path{});
        (void)dr;
        auto* toFile = &cb::anim::CompileAnimationToFile; (void)toFile;   // odr-use the file variant too
        cb::anim::Decoded decoded;
        bool dok = cb::anim::DecodeAnimation(nullptr, 0, decoded, err); (void)dok;
        cb::anim::Pose pose = decoded.poses.empty() ? cb::anim::Pose{} : decoded.poses.front(); (void)pose;

        // cb::animdata — the motion / root-motion model + sidecar
        cb::animdata::MotionRecord mr; (void)mr;
        std::string sc = cb::animdata::EmitMotionSidecar(mr); (void)sc;
        auto* parseSidecar = &cb::animdata::ParseMotionSidecar; (void)parseSidecar;
        std::string lbl = cb::animdata::LabelSample(""); (void)lbl;
        std::string unl = cb::animdata::UnlabelSample(""); (void)unl;

        // cb::skeleton — decode / compile / export skeletons
        cb::skeleton::Data sk; (void)sk;
        cb::skeleton::Joint joint; (void)joint;
        std::vector<cb::skeleton::Data> sks;
        bool rok = cb::skeleton::LoadSkeletonsFromHkx(nullptr, 0, sks, &err); (void)rok;
        cb::skeleton::CompileResult scr = cb::skeleton::CompileSkeletonFull(sk); (void)scr;
        std::string sy = cb::skeleton::EmitSkeletonYaml(sk); (void)sy;
        std::vector<cb::skeleton::BoneAdd> adds;
        bool mok = cb::skeleton::MergeBoneAdditions(sk, adds, &err); (void)mok;
        auto* overBase = &cb::skeleton::CompileSkeletonOverBase; (void)overBase;

        // cb::mm — the motion-matching bundle generator
        cb::mm::BundleSpec spec;
        cb::mm::Database db;
        db.clips.push_back(cb::mm::Clip{ "", "", cb::mm::kMmDisableReselection | cb::mm::kMmLooping });
        db.baseCostBias = "0"; db.continuingPoseCostBias = ""; db.loopingCostBias = "";
        spec.databases.push_back(db);
        cb::mm::ChooserRow row;
        row.when = "";
        spec.rows.push_back(row);
        spec.extraVariables.push_back(cb::mm::Variable{});
        const cb::mm::VariableDecl decl = cb::mm::kMmChooserVariables.front(); (void)decl;
        std::string an = cb::mm::MmAnimationName("", ""); (void)an;
        cb::mm::Files files;
        bool bok = cb::mm::BuildMotionMatchingBehavior(spec, reg, files, err); (void)bok;
        bool rok2 = cb::mm::BuildMotionMatchingRosters(spec, files, err); (void)rok2;
        bool aok = cb::mm::BuildMotionMatchingBundle(spec, reg, files, err); (void)aok;
    }
}
