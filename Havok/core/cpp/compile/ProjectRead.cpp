#include <compile/ProjectRead.h>

#include "havok/core/PackFileDeserializer.h"

#include <havok-io/HavokIo.h>          // io::SchemaObject + MakeSchemaFactory
#include <havok-schema/HavokSchema.h>  // schema::SharedRegistry

#include <cstring>
#include <memory>

namespace havok::sct {
namespace {

using havok::io::SchemaObject;

std::shared_ptr<SchemaObject> asSO(const std::shared_ptr<IHavokObject>& o) {
    return std::dynamic_pointer_cast<SchemaObject>(o);
}

// hkVector4 worldUpWS is stored as 4 contiguous little-endian floats in the field's raw bytes.
void readVec4(SchemaObject& o, const char* n, std::array<float, 4>& out) {
    const auto& r = o.FieldRef(n).raw;
    for (int i = 0; i < 4; ++i)
        if (r.size() >= static_cast<std::size_t>(4 * (i + 1)))
            std::memcpy(&out[static_cast<std::size_t>(i)], r.data() + 4 * i, 4);
}

} // namespace

ProjectReadResult ReadProject(const std::vector<std::uint8_t>& bytes) {
    ProjectReadResult out;

    // SCHEMA-NATIVE (no typed hkb* classes): deserialize through havok-io's generic SchemaObject
    // graph. Walk hkRootLevelContainer.namedVariants for the hkbProjectData variant, then read its
    // stringData record + worldUpWS/defaultEventMode off the tagged FieldValue store.
    try {
        schema::SchemaRegistry* reg = schema::SharedRegistry();
        if (!reg) { out.error = "schema registry unavailable (" + schema::SharedRegistryError() + ")"; return out; }

        PackFileDeserializer des;
        BinaryReaderEx       br(bytes);
        des.ObjectFactory = havok::io::MakeSchemaFactory(*reg);
        auto root = asSO(des.Deserialize(br));
        out.header = des._header;
        if (!root) { out.error = "not a project packfile (no hkRootLevelContainer root)"; return out; }

        std::shared_ptr<SchemaObject> data;
        for (auto& nvObj : root->FieldRef("namedVariants").objs) {
            auto nv = asSO(nvObj);
            if (nv && nv->FieldRef("className").str == "hkbProjectData") {
                data = asSO(nv->FieldRef("variant").obj);
                break;
            }
        }
        if (!data) { out.error = "root has no hkbProjectData variant"; return out; }

        auto strings = asSO(data->FieldRef("stringData").obj);
        if (!strings) { out.error = "hkbProjectData has no stringData"; return out; }

        readVec4(*data, "worldUpWS", out.spec.worldUpWS);
        const auto& dem = data->FieldRef("defaultEventMode").raw;
        out.spec.defaultEventMode = dem.empty() ? 0 : static_cast<std::int8_t>(dem[0]);

        out.spec.animationFilenames = strings->FieldRef("animationFilenames").strs;
        out.spec.behaviorFilenames  = strings->FieldRef("behaviorFilenames").strs;
        out.spec.characterFilenames = strings->FieldRef("characterFilenames").strs;
        out.spec.eventNames         = strings->FieldRef("eventNames").strs;
        out.spec.animationPath      = strings->FieldRef("animationPath").str;
        out.spec.behaviorPath       = strings->FieldRef("behaviorPath").str;
        out.spec.characterPath      = strings->FieldRef("characterPath").str;
        out.spec.fullPathToSource   = strings->FieldRef("fullPathToSource").str;
        out.ok = true;
    } catch (const std::exception& e) {
        out.error = e.what();
    }
    return out;
}

} // namespace havok::sct
