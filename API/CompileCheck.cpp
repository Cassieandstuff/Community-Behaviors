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
                                            std::string&, const std::vector<std::string>*, const std::string&)>(&cb::resolve::ParseHkyReflective);
        (void)parse;
        auto* patch  = &cb::resolve::MergeHkyPatch;               (void)patch;
        auto* derive = static_cast<bool (*)(const cb::resolve::Graph&, const std::vector<std::string>&,
                                            const std::vector<std::string>&, const std::vector<std::uint8_t>&,
                                            const cb::schema::Registry&, const std::string&, std::string&,
                                            const std::vector<std::string>*, cb::resolve::PatchStats*)>(&cb::resolve::DeriveHkyPatch);
        (void)derive;
        auto* decomp = &cb::resolve::DecompileBehaviorReflective; (void)decomp;
        // a unit folder round trip: read it, write it back
        cb::resolve::DiskUnitSource disk(std::filesystem::path{});
        bool pok = cb::resolve::ParseHkyReflective(disk, reg, g, err); (void)pok;
        const cb::resolve::Identity& ident = g.identity;
        bool eok = cb::resolve::EmitHkyReflective(ident, reg, "", err); (void)eok;
        if (g.root) {
            std::shared_ptr<cb::resolve::Object> chr = cb::resolve::FindVariant(*g.root, "hkbCharacterData");
            const cb::resolve::FieldValue* up = chr ? chr->FindField("modelUpMS") : nullptr; (void)up;
            if (chr) (void)cb::resolve::AsSchema(chr->Children("legs").front());
            for (const cb::schema::Field* f : g.root->Fields()) { const std::string& unit = f->unitRef; (void)unit; }
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

        // cb::skeleton — read skeletons
        cb::skeleton::Data sk; (void)sk;
        cb::skeleton::Joint joint; (void)joint;
        std::vector<cb::skeleton::Data> sks;
        bool rok = cb::skeleton::LoadSkeletonsFromHkx(nullptr, 0, sks, &err); (void)rok;
        bool phok = cb::skeleton::ReadSkeletonPhysics(nullptr, 0, sk, &err); (void)phok;
        auto* readUnit  = &cb::skeleton::ReadSkeletonUnit; (void)readUnit;
        auto* readBones = &cb::skeleton::ReadSkeletonBones; (void)readBones;

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

        // cb::locomotion — the movement step, the horse ladder, the slope math
        cb::locomotion::State ls;
        cb::locomotion::Input lin;
        lin.mode = cb::locomotion::Mode::Strafe; lin.gait = cb::locomotion::Gait::Run;
        cb::locomotion::Caps lcaps;
        const cb::locomotion::Profile& lprof = cb::locomotion::kPlayer;
        cb::locomotion::Step(ls, lin, lcaps, lprof, 0.f, 0.f, 1.f / 60.f);
        float lmax = cb::locomotion::MaxSpeed(lcaps, lin.mode, 0.f, ls.yaw); (void)lmax;
        float lmap = cb::locomotion::StrafeSpeedMap(0.f); (void)lmap;
        bool lspr = cb::locomotion::SprintDirectionAllowed(ls, lin, lprof); (void)lspr;
        float lsh = 0.0f; bool lhs = cb::locomotion::StickHeadingOf(0.0f, 1.0f, 0.0f, lsh); (void)lhs;
        float lturn = 0.0f, ldir = 0.0f; cb::locomotion::Face(lturn, ldir, 0.0f, 0.0f, cb::locomotion::kNoDirection, 0.016f);
        (void)cb::locomotion::kStickDeadzone;
        const cb::locomotion::HorseProfile& lhorse = cb::locomotion::kHorse;
        cb::locomotion::Rung lrung = cb::locomotion::TopRung(false, true, 1.f, lhorse);
        float lrs = cb::locomotion::RungSpeed(lrung, 0.f, 0.f, lhorse); (void)lrs;
        float lw = cb::locomotion::WrapPi(0.f); (void)lw;
        float lx = 0.f, lv = 0.f;
        cb::locomotion::SpringDamperExact(lx, lv, 1.f, cb::locomotion::kSlopeSpeedHalflife, 1.f / 60.f);
        cb::locomotion::Surface lg;
        cb::locomotion::Tuning ltune;
        float lrise  = cb::locomotion::SlopeRise(lg, 1.f, 0.f); (void)lrise;
        float lscale = cb::locomotion::SlopeSpeedScale(lg, 1.f, 0.f, ltune); (void)lscale;
        float lsx = 0.f, lsy = 0.f, lox = 0.f, loy = 0.f, loz = 0.f;
        cb::locomotion::SlideStep(lg, 0.f, 0.f, -9.8f, lsx, lsy, 1.f / 60.f);
        cb::locomotion::ClipUphill(lg, lsx, lsy);
        cb::locomotion::JumpLaunch(lg, 0.f, 0.f, 1.f, lox, loy, loz);
        cb::locomotion::DeadBlend lblend;
        cb::locomotion::DeadBlendTuning lbt;
        cb::locomotion::BlendFault lfault;
        cb::locomotion::Bone lpose[1];
        bool lstep = lblend.Step(lpose, 1, 0, lbt, &lfault); (void)lstep;
        float lleft = lblend.Remaining(lbt); (void)lleft;
        bool lfin = cb::locomotion::FiniteBone(lpose[0]); (void)lfin;
    }
}
