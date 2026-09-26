// CharacterDecompile — schema-native character decompile (see CharacterDecompile.h). Ported 1:1 from
// havok-core's typed decompileCharacter, reading the havok-io SchemaObject graph by FIELD NAME instead
// of typed hkb* members. The formatters (fstr/vec4/q/revEnum) are copied verbatim from the typed
// emitter so the emitted tree is BYTE-IDENTICAL (the parity gate over the vanilla corpus proves it).

#include <decompile/CharacterDecompile.h>

#include <interface/HavokEnums.h>          // model::enums::VariableType / Role (value<->name)

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace havok::decompile {
namespace fs = std::filesystem;

namespace {

// ── formatters (verbatim from the typed decompileCharacter — byte-parity) ──
std::string fstr(float v) { char b[32]; std::snprintf(b, sizeof b, "%.9g", v); return b; }
std::string q(const std::string& s) {
    std::string out = "'";
    for (char c : s) { if (c == '\'') out += "''"; else out += c; }
    out += "'";
    return out;
}
std::string revEnum(const std::unordered_map<std::string, long>& t, long v, const char* fb) {
    for (const auto& [k, val] : t) if (val == v) return k;
    return fb;
}
void writeText(const fs::path& p, const std::string& s) {
    std::ofstream f(p, std::ios::binary);
    f.write(s.data(), static_cast<std::streamsize>(s.size()));
}

// ── generic SchemaObject field access by name ──
const io::FieldValue* fv(const io::SchemaObject* so, const char* name) {
    if (!so) return nullptr;
    const auto flds = so->Fields();
    const auto& vs = so->Values();
    for (std::size_t i = 0; i < flds.size() && i < vs.size(); ++i)
        if (flds[i]->name == name) return &vs[i];
    return nullptr;
}
const io::SchemaObject* obj(const io::SchemaObject* so, const char* name) {
    const io::FieldValue* v = fv(so, name);
    return v ? dynamic_cast<const io::SchemaObject*>(v->obj.get()) : nullptr;
}
std::string str(const io::SchemaObject* so, const char* name) { auto* v = fv(so, name); return v ? v->str : std::string(); }
const std::vector<std::uint8_t>& raw(const io::SchemaObject* so, const char* name) {
    static const std::vector<std::uint8_t> empty;
    auto* v = fv(so, name); return v ? v->raw : empty;
}

float        f32(const std::vector<std::uint8_t>& r) { float v = 0;    if (r.size() >= 4) std::memcpy(&v, r.data(), 4); return v; }
std::int32_t i32(const std::vector<std::uint8_t>& r) { std::int32_t v = 0; if (r.size() >= 4) std::memcpy(&v, r.data(), 4); return v; }
std::uint32_t u32(const std::vector<std::uint8_t>& r){ std::uint32_t v = 0; if (r.size() >= 4) std::memcpy(&v, r.data(), 4); return v; }
std::int16_t i16(const std::vector<std::uint8_t>& r) { std::int16_t v = 0; if (r.size() >= 2) std::memcpy(&v, r.data(), 2); return v; }
std::int8_t  i8 (const std::vector<std::uint8_t>& r) { return r.empty() ? std::int8_t(0) : static_cast<std::int8_t>(r[0]); }

std::string vec4Raw(const std::vector<std::uint8_t>& r) {
    float x = 0, y = 0, z = 0, w = 0;
    if (r.size() >= 16) { std::memcpy(&x, r.data(), 4); std::memcpy(&y, r.data()+4, 4); std::memcpy(&z, r.data()+8, 4); std::memcpy(&w, r.data()+12, 4); }
    return "[" + fstr(x) + ", " + fstr(y) + ", " + fstr(z) + ", " + fstr(w) + "]";
}

std::string f32(const io::SchemaObject* so, const char* n) { return fstr(f32(raw(so, n))); }

} // namespace

namespace en = havok::model::enums;

CharDecompileResult DecompileCharacterSchema(const io::SchemaObject& cd, const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::create_directories(dir / "properties", ec);

    const io::SchemaObject* sd  = obj(&cd, "stringData");
    const io::SchemaObject* vvs = obj(&cd, "characterPropertyValues");
    const io::SchemaObject* ik  = obj(&cd, "footIkDriverInfo");
    const io::SchemaObject* mi  = obj(&cd, "mirroredSkeletonInfo");
    if (!sd) return { false, "character has no stringData" };

    // ── character.yaml ──
    {
        std::string y;
        y += "packfile:\n  classversion: 8\n  contentsversion: \"hk_2010.2.0-r1\"\n\n";
        y += "character:\n";
        y += "  name: " + q(str(sd, "name")) + "\n";
        y += "  rig: " + q(str(sd, "rigName")) + "\n";
        y += "  ragdoll: " + q(str(sd, "ragdollName")) + "\n";
        y += "  behavior: " + q(str(sd, "behaviorFilename")) + "\n";
        y += "  scale: " + f32(&cd, "scale") + "\n";
        const io::SchemaObject* cci = obj(&cd, "characterControllerInfo");
        y += "  controller:\n";
        y += "    capsuleHeight: " + f32(cci, "capsuleHeight") + "\n";
        y += "    capsuleRadius: " + f32(cci, "capsuleRadius") + "\n";
        y += "    collisionFilterInfo: " + std::to_string(u32(raw(cci, "collisionFilterInfo"))) + "\n";
        y += "  model:\n";
        y += "    up: " + vec4Raw(raw(&cd, "modelUpMS")) + "\n";
        y += "    forward: " + vec4Raw(raw(&cd, "modelForwardMS")) + "\n";
        y += "    right: " + vec4Raw(raw(&cd, "modelRightMS")) + "\n";
        writeText(dir / "character.yaml", y);
    }

    // ── data/animations.yaml ──
    {
        std::error_code de;
        fs::create_directories(dir / "data", de);
        std::string y;
        if (const io::FieldValue* an = fv(sd, "animationNames"))
            for (const auto& a : an->strs) y += "- " + q(a) + "\n";
        writeText(dir / "data" / "animations.yaml", y);
    }

    // ── properties/*.yaml + _order.txt ──
    {
        std::string order;
        const io::FieldValue* namesV = fv(sd, "characterPropertyNames");
        const io::FieldValue* infosV = fv(&cd, "characterPropertyInfos");
        const io::FieldValue* wordsV = fv(vvs, "wordVariableValues");
        const io::FieldValue* variV  = fv(vvs, "variantVariableValues");
        const std::size_t nProps = namesV ? namesV->strs.size() : 0;
        for (std::size_t i = 0; i < nProps; ++i) {
            const std::string& name = namesV->strs[i];
            order += name + "\n";

            const io::SchemaObject* info = (infosV && i < infosV->objs.size())
                ? dynamic_cast<const io::SchemaObject*>(infosV->objs[i].get()) : nullptr;
            const std::int8_t  type = info ? i8(raw(info, "type")) : std::int8_t(0);
            const io::SchemaObject* roleObj = obj(info, "role");
            const std::int16_t role = roleObj ? i16(raw(roleObj, "role")) : std::int16_t(0);
            const bool isPointer = (type == 5 /*VARIABLE_TYPE_POINTER*/);

            std::string y;
            y += "name: " + q(name) + "\n";
            y += "type: " + revEnum(en::VariableType(), type, "VARIABLE_TYPE_INT32") + "\n";
            y += "role: " + revEnum(en::Role(), role, "ROLE_DEFAULT") + "\n";

            const io::SchemaObject* wv = (wordsV && i < wordsV->objs.size())
                ? dynamic_cast<const io::SchemaObject*>(wordsV->objs[i].get()) : nullptr;
            const std::int32_t word = wv ? i32(raw(wv, "value")) : 0;
            if (isPointer) {
                std::vector<float> weights;
                if (variV && word >= 0 && static_cast<std::size_t>(word) < variV->objs.size()) {
                    if (const auto* bw = dynamic_cast<const io::SchemaObject*>(variV->objs[word].get())) {
                        const auto& br = raw(bw, "boneWeights");
                        weights.resize(br.size() / 4);
                        for (std::size_t w = 0; w < weights.size(); ++w) std::memcpy(&weights[w], br.data() + w*4, 4);
                    }
                }
                y += "bone_weights:\n";
                y += "  count: " + std::to_string(weights.size()) + "\n";
                std::string vals;
                for (std::size_t w = 0; w < weights.size(); ++w) { if (w) vals += ' '; vals += fstr(weights[w]); }
                y += "  values: " + q(vals) + "\n";
            } else {
                y += "initial_value: " + std::to_string(word) + "\n";
            }

            std::string safe = name;
            for (char& c : safe) if (c == '+') c = '_';
            writeText(dir / "properties" / (safe + ".yaml"), y);
        }
        writeText(dir / "properties" / "_order.txt", order);
    }

    // ── foot_ik.yaml ──
    {
        std::string y = "legs:\n";
        if (ik) {
            if (const io::FieldValue* legsV = fv(ik, "legs")) {
                for (const auto& lp : legsV->objs) {
                    const auto* L = dynamic_cast<const io::SchemaObject*>(lp.get());
                    if (!L) continue;
                    y += "  - kneeAxisLS: " + vec4Raw(raw(L, "kneeAxisLS")) + "\n";
                    y += "    footEndLS: " + vec4Raw(raw(L, "footEndLS")) + "\n";
                    y += "    footPlantedAnkleHeightMS: " + f32(L, "footPlantedAnkleHeightMS") + "\n";
                    y += "    footRaisedAnkleHeightMS: " + f32(L, "footRaisedAnkleHeightMS") + "\n";
                    y += "    maxAnkleHeightMS: " + f32(L, "maxAnkleHeightMS") + "\n";
                    y += "    minAnkleHeightMS: " + f32(L, "minAnkleHeightMS") + "\n";
                    y += "    maxKneeAngleDegrees: " + f32(L, "maxKneeAngleDegrees") + "\n";
                    y += "    minKneeAngleDegrees: " + f32(L, "minKneeAngleDegrees") + "\n";
                    y += "    maxAnkleAngleDegrees: " + f32(L, "maxAnkleAngleDegrees") + "\n";
                    y += "    hipIndex: " + std::to_string(i16(raw(L, "hipIndex"))) + "\n";
                    y += "    kneeIndex: " + std::to_string(i16(raw(L, "kneeIndex"))) + "\n";
                    y += "    ankleIndex: " + std::to_string(i16(raw(L, "ankleIndex"))) + "\n";
                }
            }
            y += "raycastDistanceUp: " + f32(ik, "raycastDistanceUp") + "\n";
            y += "raycastDistanceDown: " + f32(ik, "raycastDistanceDown") + "\n";
            y += "originalGroundHeightMS: " + f32(ik, "originalGroundHeightMS") + "\n";
            y += "verticalOffset: " + f32(ik, "verticalOffset") + "\n";
            y += "collisionFilterInfo: " + std::to_string(u32(raw(ik, "collisionFilterInfo"))) + "\n";
            y += "forwardAlignFraction: " + f32(ik, "forwardAlignFraction") + "\n";
            y += "sidewaysAlignFraction: " + f32(ik, "sidewaysAlignFraction") + "\n";
            y += "sidewaysSampleWidth: " + f32(ik, "sidewaysSampleWidth") + "\n";
            y += std::string("lockFeetWhenPlanted: ") + (i8(raw(ik, "lockFeetWhenPlanted")) ? "true" : "false") + "\n";
            y += std::string("useCharacterUpVector: ") + (i8(raw(ik, "useCharacterUpVector")) ? "true" : "false") + "\n";
            y += std::string("isQuadrupedNarrow: ") + (i8(raw(ik, "isQuadrupedNarrow")) ? "true" : "false") + "\n";
        }
        writeText(dir / "foot_ik.yaml", y);
    }

    // ── mirror.yaml ──
    {
        std::string y;
        if (mi) {
            y += "mirrorAxis: " + vec4Raw(raw(mi, "mirrorAxis")) + "\n";
            const auto& bp = raw(mi, "bonePairMap");
            const std::size_t n = bp.size() / 2;
            y += "bonePairMap:\n";
            y += "  count: " + std::to_string(n) + "\n";
            std::string vals;
            for (std::size_t i = 0; i < n; ++i) { if (i) vals += ' '; std::int16_t v; std::memcpy(&v, bp.data()+i*2, 2); vals += std::to_string(v); }
            y += "  values: " + q(vals) + "\n";
        }
        writeText(dir / "mirror.yaml", y);
    }

    return { true, "" };
}

} // namespace havok::decompile
