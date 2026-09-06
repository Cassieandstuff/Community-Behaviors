#include "PCH.h"
#include <Hooks/hookslib.h>     // hooks::geometry (CreateTriShape) + hooks::skin (CreateSkinData/Partition)
#include "SkinnedMesh.h"

#include <cstring>
#include <vector>

namespace CB::creature {
namespace {

using VA = RE::BSGraphics::Vertex;

// IEEE float32 -> float16 (half). Skin weights are stored as half in the vertex buffer.
// (Encoding is a documented in-game tuning point — if weights read wrong, this is a suspect.)
std::uint16_t F2H(float f) {
    std::uint32_t x; std::memcpy(&x, &f, 4);
    const std::uint32_t sign = (x >> 16) & 0x8000u;
    std::int32_t        exp  = static_cast<std::int32_t>((x >> 23) & 0xFF) - 127 + 15;
    std::uint32_t       mant = x & 0x7FFFFFu;
    if (exp <= 0)  return static_cast<std::uint16_t>(sign);                 // underflow -> 0
    if (exp >= 31) return static_cast<std::uint16_t>(sign | 0x7C00u);       // overflow -> inf
    return static_cast<std::uint16_t>(sign | (exp << 10) | (mant >> 13));
}

// Engine-heap allocation (freed by the skin object dtors via MemoryManager::Deallocate).
void* NiAlloc(std::size_t bytes) {
    auto* mm = RE::MemoryManager::GetSingleton();
    return mm ? mm->Allocate(bytes, 0, false) : nullptr;
}
template <class T>
T* NiAllocArr(std::size_t count) { return static_cast<T*>(NiAlloc(count * sizeof(T))); }

} // namespace

bool SkinnedCreatureMesh::Build(const CreatureMeshData& mesh, RE::NiNode* actorRoot,
                                RE::BSShaderProperty* shader, std::string* err) {
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
    if (!actorRoot)                                    return fail("null actorRoot");
    if (mesh.vertices.empty() || mesh.indices.empty()) return fail("empty mesh");
    if (mesh.indices.size() % 3 != 0)                  return fail("indices not a triangle list");

    const std::uint32_t nb = static_cast<std::uint32_t>(mesh.boneNames.size());
    const std::uint32_t nv = static_cast<std::uint32_t>(mesh.vertices.size());
    const std::uint32_t ni = static_cast<std::uint32_t>(mesh.indices.size());
    if (nb == 0 || nb > 255) return fail("bone count must be 1..255");

    // ── resolve bones by name + capture bind (model->bone inverse) ──────────────────────
    const RE::NiTransform     rootInv = actorRoot->world.Invert();
    std::vector<RE::NiAVObject*> boneNodes(nb);
    std::vector<RE::NiTransform> skinToBone(nb);   // = (rootInv * bone.world)^-1
    for (std::uint32_t i = 0; i < nb; ++i) {
        RE::NiAVObject* o = actorRoot->GetObjectByName(mesh.boneNames[i]);
        if (!o) return fail("bone not found under actor 3D: " + mesh.boneNames[i]);
        boneNodes[i]  = o;
        skinToBone[i] = (rootInv * o->world).Invert();
    }

    // ── vertex descriptor: full-precision position + skinning ───────────────────────────
    RE::BSGraphics::VertexDesc desc{};
    desc.SetFlags(static_cast<VA::Flags>(VA::VF_VERTEX | VA::VF_FULLPREC | VA::VF_SKINNED));
    desc.SetAttributeOffset(VA::VA_SKINNING, 16);   // position @0 (16B), skinning @16
    const std::uint32_t stride = desc.GetSize();    // 16 + (u16*4 weights + u8*4 idx) = 28
    std::uint64_t descRaw = 0;
    std::memcpy(&descRaw, &desc, sizeof(descRaw));

    // pack VB: [pos: 4×f32][weights: 4×half][boneIdx: 4×u8]
    std::vector<std::uint8_t> vb(static_cast<std::size_t>(nv) * stride, 0);
    for (std::uint32_t v = 0; v < nv; ++v) {
        const auto&    mv = mesh.vertices[v];
        std::uint8_t*  p  = vb.data() + static_cast<std::size_t>(v) * stride;
        float* pos = reinterpret_cast<float*>(p);
        pos[0] = mv.position.x; pos[1] = mv.position.y; pos[2] = mv.position.z; pos[3] = 1.0f;
        float ws = mv.weight[0] + mv.weight[1] + mv.weight[2] + mv.weight[3];
        if (ws <= 0.0f) ws = 1.0f;
        std::uint16_t* w  = reinterpret_cast<std::uint16_t*>(p + 16);
        std::uint8_t*  bi = p + 16 + 8;
        for (int k = 0; k < 4; ++k) { w[k] = F2H(mv.weight[k] / ws); bi[k] = mv.bone[k]; }
    }

    // ── geometry (static; the engine skins it) ──────────────────────────────────────────
    auto* rd = hooks::geometry::CreateRendererTriShape(vb.data(), nv * stride, descRaw,
                                                       mesh.indices.data(), ni);
    if (!rd) return fail("CreateRendererTriShape returned null");
    RE::BSTriShape* shape = hooks::geometry::CreateTriShape();
    if (!shape) return fail("CreateTriShape returned null");
    shape->rendererData = rd;
    std::memcpy(&shape->vertexDesc, &descRaw, sizeof(std::uint64_t));
    shape->vertexCount   = static_cast<std::uint16_t>(nv);
    shape->triangleCount = static_cast<std::uint16_t>(ni / 3);

    // ── NiSkinData: per-bone skin-to-bone (inverse bind) transforms ─────────────────────
    RE::NiSkinData* sd = hooks::skin::CreateSkinData();
    if (!sd) return fail("NiSkinData::Create returned null");
    auto* boneData = NiAllocArr<RE::NiSkinData::BoneData>(nb);
    if (!boneData) return fail("boneData alloc failed");
    for (std::uint32_t i = 0; i < nb; ++i) {
        boneData[i] = RE::NiSkinData::BoneData{};
        boneData[i].skinToBone   = skinToBone[i];
        boneData[i].boneVertData = nullptr;
        boneData[i].verts        = 0;
    }
    sd->boneData = boneData;
    sd->bones    = nb;   // rootParentToSkin stays identity (set by the factory)

    // ── NiSkinPartition: one partition covering the whole mesh ──────────────────────────
    RE::NiSkinPartition* sp = hooks::skin::CreateSkinPartition();
    if (!sp) return fail("NiSkinPartition::Create returned null");
    sp->numPartitions = 1;
    sp->vertexCount   = nv;
    auto* part = NiAllocArr<RE::NiSkinPartition::Partition>(1);
    if (!part) return fail("partition alloc failed");
    *part = RE::NiSkinPartition::Partition{};
    std::memcpy(&part->vertexDesc, &descRaw, sizeof(std::uint64_t));
    part->vertices       = static_cast<std::uint16_t>(nv);
    part->triangles      = static_cast<std::uint16_t>(ni / 3);
    part->numBones       = static_cast<std::uint16_t>(nb);
    part->bonesPerVertex = 4;
    part->buffData       = rd;
    // bone palette: identity (partition-local bone i -> skin instance bone i)
    auto* pal = NiAllocArr<std::uint16_t>(nb);
    for (std::uint32_t i = 0; i < nb; ++i) pal[i] = static_cast<std::uint16_t>(i);
    part->bones = pal;
    // vertexMap: identity
    auto* vmap = NiAllocArr<std::uint16_t>(nv);
    for (std::uint32_t i = 0; i < nv; ++i) vmap[i] = static_cast<std::uint16_t>(i);
    part->vertexMap = vmap;
    // triangle list
    auto* tl = NiAllocArr<std::uint16_t>(ni);
    std::memcpy(tl, mesh.indices.data(), static_cast<std::size_t>(ni) * sizeof(std::uint16_t));
    part->triList = tl;
    // per-vertex bone palette indices + weights (bonesPerVertex × vertices)
    auto* bp = NiAllocArr<std::uint8_t>(static_cast<std::size_t>(nv) * 4);
    auto* wt = NiAllocArr<float>(static_cast<std::size_t>(nv) * 4);
    for (std::uint32_t v = 0; v < nv; ++v) {
        const auto& mv = mesh.vertices[v];
        float ws = mv.weight[0] + mv.weight[1] + mv.weight[2] + mv.weight[3];
        if (ws <= 0.0f) ws = 1.0f;
        for (int k = 0; k < 4; ++k) { bp[v * 4 + k] = mv.bone[k]; wt[v * 4 + k] = mv.weight[k] / ws; }
    }
    part->bonePalette = bp;
    part->weights     = wt;
    // set the single-pointer SimpleArray to our partition array
    reinterpret_cast<RE::NiSkinPartition::Partition*&>(sp->partitions) = part;

    sd->skinPartition = RE::NiPointer<RE::NiSkinPartition>(sp);

    // ── NiSkinInstance: root + bones[] + data + partition ───────────────────────────────
    RE::NiSkinInstance* si = RE::NiSkinInstance::Create();
    if (!si) return fail("NiSkinInstance::Create returned null");
    si->skinData      = RE::NiPointer<RE::NiSkinData>(sd);
    si->skinPartition = RE::NiPointer<RE::NiSkinPartition>(sp);
    si->rootParent    = actorRoot;
    auto* boneArr = NiAllocArr<RE::NiAVObject*>(nb);
    for (std::uint32_t i = 0; i < nb; ++i) boneArr[i] = boneNodes[i];
    si->bones = boneArr;

    shape->skinInstance = RE::NiPointer<RE::NiSkinInstance>(si);
    if (shader) shape->shaderProperty.reset(shader);

    _shape.reset(shape);
    _root.reset(actorRoot);
    actorRoot->AttachChild(shape, true);
    shape->SetAppCulled(false);
    return true;
}

void SkinnedCreatureMesh::Detach() {
    if (_shape && _root) _root->DetachChild(_shape.get());
    _shape.reset();
    _root.reset();
}

} // namespace CB::creature
