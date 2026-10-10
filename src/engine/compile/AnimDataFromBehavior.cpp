#include <compile/AnimDataFromBehavior.h>

#include "interface/AnimationData.h"

#include <codec/serialization/HavokIo.h>          // io::SchemaObject + MakeSchemaFactory (schema-native anim read)

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <unordered_map>

namespace CB::engine::compile {
using namespace CB::engine::codec;
using namespace CB::engine::common;

namespace {
    // Schema-object field readers (mirror AnimationDecompiler.cpp — no typed hka* classes).
    float rdF32(CB::engine::codec::io::SchemaObject& o, const char* n) {
        const auto& r = o.FieldRef(n).raw; float v = 0.f;
        if (r.size() >= 4) std::memcpy(&v, r.data(), 4); return v;
    }
}  // namespace

std::vector<CB::engine::animdata::DeriveClipInput> DeriveClipInputsFromGraph(const std::shared_ptr<IHavokObject>& root)
{
    using CB::engine::codec::io::SchemaObject;
    std::vector<CB::engine::animdata::DeriveClipInput> out;
    auto so = [](const std::shared_ptr<IHavokObject>& o) { return dynamic_cast<SchemaObject*>(o.get()); };
    auto* r = so(root);
    if (!r) return out;

    const std::vector<std::string>* events = nullptr;
    for (auto& nv : r->FieldRef("namedVariants").objs)
        if (auto* e = so(nv))
            if (auto* graph = so(e->FieldRef("variant").obj); graph && std::string(graph->ClassName()) == "hkbBehaviorGraph")
                if (auto* data = so(graph->FieldRef("data").obj))
                    if (auto* sd = so(data->FieldRef("stringData").obj)) events = &sd->FieldRef("eventNames").strs;

    auto graphEvents = std::make_shared<std::unordered_set<std::string>>();
    if (events)
        for (std::string e : *events) {
            for (char& ch : e) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            graphEvents->insert(std::move(e));
        }

    std::vector<SchemaObject*> clips;
    std::unordered_map<const IHavokObject*, bool> seen;
    std::function<void(IHavokObject*)> walk = [&](IHavokObject* o) {
        auto* s = dynamic_cast<SchemaObject*>(o);
        if (!s || !seen.emplace(o, true).second) return;
        if (std::string(s->ClassName()) == "hkbClipGenerator") clips.push_back(s);
        for (const auto& fv : s->Values()) { walk(fv.obj.get()); for (const auto& e : fv.objs) walk(e.get()); }
    };
    walk(r);

    for (SchemaObject* c : clips) {
        CB::engine::animdata::DeriveClipInput dc;
        dc.name          = c->FieldRef("name").str;
        dc.animationName = c->FieldRef("animationName").str;
        dc.playbackSpeed = rdF32(*c, "playbackSpeed");
        dc.cropStart     = rdF32(*c, "cropStartAmountLocalTime");
        dc.cropEnd       = rdF32(*c, "cropEndAmountLocalTime");
        dc.graphEvents   = graphEvents;
        if (auto* arr = so(c->FieldRef("triggers").obj))
            for (auto& to : arr->FieldRef("triggers").objs) {
                auto* t  = so(to);
                auto* ev = t ? so(t->FieldRef("event").obj) : nullptr;
                if (!t || !ev) continue;
                std::int32_t id = -1;
                const auto& idRaw = ev->FieldRef("id").raw;
                if (idRaw.size() >= 4) std::memcpy(&id, idRaw.data(), 4);
                if (!events || id < 0 || static_cast<std::size_t>(id) >= events->size() || (*events)[id].empty()) continue;
                CB::engine::animdata::DeriveClipInput::Trigger tr;
                tr.event               = (*events)[id];
                tr.localTime           = rdF32(*t, "localTime");
                const auto& rel        = t->FieldRef("relativeToEndOfClip").raw;
                tr.relativeToEndOfClip = !rel.empty() && rel[0] != 0;
                tr.fromAnnotation      = false;
                dc.triggers.push_back(std::move(tr));
            }
        out.push_back(std::move(dc));
    }
    /// @note Name order, as the node-name-keyed model it replaces produced them.
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    return out;
}

}  // namespace CB::engine::compile
