#include "compile/MotionMatchingCache.h"

#include "compile/MotionFeatures.h"
#include <codec/image/RuntimeImage.h>
#include <codec/serialization/HavokIo.h>
#include <codec/serialization/packfile/PackFileSerializer.h>
#include <decompile/BehaviorDecompile.h>
#include <interface/reflection/HavokSchema.h>

#include <cstring>
#include <exception>
#include <memory>

namespace CB::engine::anim {
    using namespace CB::engine::codec;

    namespace {

        std::shared_ptr<io::SchemaObject> Make(const schema::SchemaRegistry& reg, const char* cls)
        {
            const schema::ClassSchema* cs = reg.Find(cls);
            if (!cs) throw std::runtime_error(std::string("the schema has no ") + cls);
            auto o = std::make_shared<io::SchemaObject>(&reg, cs);
            o->Init();
            return o;
        }

        void PutFloats(io::FieldValue& f, const std::vector<float>& v)
        {
            f.raw.resize(v.size() * sizeof(float));
            if (!v.empty()) std::memcpy(f.raw.data(), v.data(), f.raw.size());
        }

        std::vector<float> GetFloats(const io::FieldValue& f)
        {
            std::vector<float> v(f.raw.size() / sizeof(float));
            if (!v.empty()) std::memcpy(v.data(), f.raw.data(), v.size() * sizeof(float));
            return v;
        }

        std::vector<float> Horizons()
        {
            return { features::kHorizons.begin(), features::kHorizons.end() };
        }

    }  // namespace

    std::uint64_t MmFingerprint(std::string_view unitText) noexcept
    {
        return image::Fnv1a64(reinterpret_cast<const std::uint8_t*>(unitText.data()), unitText.size());
    }

    bool WriteMmCache(const std::vector<MmCachedClip>& clips, std::vector<std::uint8_t>& out, std::string& err)
    {
        try {
            const schema::SchemaRegistry* reg = schema::SharedRegistry();
            if (!reg) { err = "no schema registry (" + schema::SharedRegistryError() + ")"; return false; }
            auto root = Make(*reg, "CBMotionMatchingCache");
            root->SetInt("featureDims", features::kDims);
            PutFloats(root->FieldRef("horizons"), Horizons());
            auto& list = root->FieldRef("clips").objs;
            for (const auto& c : clips) {
                auto clip         = Make(*reg, "CBMotionClip");
                clip->Str("name") = c.name;
                /// @note Raw bytes: SetInt refuses a hash with its top bit set as negative for an unsigned field.
                auto& fp = clip->FieldRef("fingerprint").raw;
                fp.resize(sizeof c.fingerprint);
                std::memcpy(fp.data(), &c.fingerprint, sizeof c.fingerprint);
                clip->SetInt("frames", c.frames);
                clip->SetFloat("frameDuration", c.frameDuration);
                clip->SetFloat("duration", c.duration);
                PutFloats(clip->FieldRef("features"), c.features);
                PutFloats(clip->FieldRef("rootPositions"), c.rootPositions);
                auto& curves = clip->FieldRef("curves").objs;
                for (const auto& cv : c.curves) {
                    auto curve         = Make(*reg, "CBAnimationCurve");
                    curve->Str("name") = cv.name;
                    auto& keys         = curve->FieldRef("keys").objs;
                    for (const auto& k : cv.keys) {
                        auto key = Make(*reg, "CBCurveKey");
                        key->SetFloat("time", k.time);
                        key->SetFloat("value", k.value);
                        key->SetInt("interp", static_cast<std::int64_t>(k.interp));
                        keys.push_back(std::move(key));
                    }
                    curves.push_back(std::move(curve));
                }
                list.push_back(std::move(clip));
            }
            PackFileSerializer ser;
            BinaryWriterEx     bw;
            ser.Serialize(root, bw, HKXHeader::SkyrimSE());
            out = bw.Data();
            return true;
        } catch (const std::exception& e) {
            err = e.what();
            return false;
        }
    }

    bool ReadMmCache(const std::vector<std::uint8_t>& bytes, std::vector<MmCachedClip>& out, std::string& err)
    {
        try {
            const schema::SchemaRegistry* reg = schema::SharedRegistry();
            if (!reg) { err = "no schema registry (" + schema::SharedRegistryError() + ")"; return false; }
            decompile::ParsedHky parsed;
            if (!decompile::ParsePackfile(bytes, *reg, parsed, err)) return false;
            const auto& root = parsed.root;
            if (!root || std::string_view(root->ClassName()) != "CBMotionMatchingCache") {
                err = "the root is not a CBMotionMatchingCache";
                return false;
            }
            if (root->GetInt("featureDims") != features::kDims || GetFloats(root->FieldRef("horizons")) != Horizons()) {
                err = "measured under another feature layout";
                return false;
            }
            out.clear();
            for (const auto& p : root->FieldRef("clips").objs) {
                auto* o = dynamic_cast<io::SchemaObject*>(p.get());
                if (!o) { err = "a clip is not an inline CBMotionClip"; return false; }
                MmCachedClip c;
                c.name          = o->Str("name");
                c.fingerprint   = static_cast<std::uint64_t>(o->GetInt("fingerprint"));
                c.frames        = static_cast<int>(o->GetInt("frames"));
                c.frameDuration = o->GetFloat("frameDuration");
                c.duration      = o->GetFloat("duration");
                c.features      = GetFloats(o->FieldRef("features"));
                c.rootPositions = GetFloats(o->FieldRef("rootPositions"));
                if (c.frames < 0 || c.features.size() != static_cast<std::size_t>(c.frames) * features::kDims ||
                    c.rootPositions.size() != static_cast<std::size_t>(c.frames) * 3) {
                    err = "clip '" + c.name + "' has rows that do not match its frame count";
                    return false;
                }
                for (const auto& cp : o->FieldRef("curves").objs) {
                    auto* cv = dynamic_cast<io::SchemaObject*>(cp.get());
                    if (!cv) { err = "a curve of '" + c.name + "' is not an inline CBAnimationCurve"; return false; }
                    AnimCurveDef def{ cv->Str("name"), {} };
                    for (const auto& kp : cv->FieldRef("keys").objs) {
                        auto* k = dynamic_cast<io::SchemaObject*>(kp.get());
                        if (!k) { err = "a key of '" + c.name + "' is not an inline CBCurveKey"; return false; }
                        def.keys.push_back({ k->GetFloat("time"), k->GetFloat("value"),
                                             static_cast<AnimCurveKey::Interp>(k->GetInt("interp")) });
                    }
                    c.curves.push_back(std::move(def));
                }
                out.push_back(std::move(c));
            }
            return true;
        } catch (const std::exception& e) {
            err = e.what();
            return false;
        }
    }

}  // namespace CB::engine::anim
