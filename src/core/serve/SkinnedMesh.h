#pragma once
// SkinnedMesh.h — the runtime render companion to Community Behaviors's skeleton compile.
//
// NATIVE ENGINE-SKINNED path: build a real skin instance (NiSkinInstance + NiSkinData +
// NiSkinPartition) binding the geometry to the actor's skeleton bones BY NAME, attach it
// into the actor's 3D, and let the ENGINE GPU-skin it every frame — exactly like a vanilla
// body/armor mesh. We build once and never touch a vertex again (no CPU update, no thread
// hazard). No NIF, no nifly: geometry via the hooks geometry factory, skin objects via the
// RE'd engine factories (hooks::skin). See memory skyrim-skin-instance-layout for the RE.
//
// v1 vertex format is POSITION + SKINNING only (VF_VERTEX|VF_FULLPREC|VF_SKINNED) — proves
// deform. Normals/UV/color + a material (shaderProperty) are the shading pass. Build-gated
// only; the skinning encoding (weight format, VF_SKINNED byte order, attribute offsets) and
// the Load3D attach timing want in-game iteration.

#include <PCH.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace CB::creature {

// Authored/compiled mesh — positions in the skeleton's model space (bind pose), each vertex
// bound to up to 4 bones BY NAME (index into `boneNames`) with LBS weights.
struct CreatureMeshData {
    struct Vertex {
        RE::NiPoint3                position{};
        std::array<std::uint8_t, 4> bone{};     // indices into boneNames
        std::array<float, 4>        weight{};   // linear-blend weights
    };
    std::vector<std::string>   boneNames;
    std::vector<Vertex>        vertices;
    std::vector<std::uint16_t> indices;         // triangle list (3 per triangle)
};

// A live, engine-skinned mesh injected into an actor's 3D.
class SkinnedCreatureMesh {
public:
    // Build the skinned geometry + skin instance bound to the bones under `actorRoot`, and
    // attach it. `shader` (optional) is the material; null attaches+skins but won't draw.
    // Returns false + err on a missing bone or an allocation failure.
    bool Build(const CreatureMeshData& mesh, RE::NiNode* actorRoot,
               RE::BSShaderProperty* shader = nullptr, std::string* err = nullptr);

    void Detach();
    [[nodiscard]] bool           Valid() const { return _shape != nullptr; }
    [[nodiscard]] RE::BSTriShape* Shape() const { return _shape.get(); }

private:
    RE::NiPointer<RE::BSTriShape> _shape;
    RE::NiPointer<RE::NiNode>     _root;
};

} // namespace CB::creature
