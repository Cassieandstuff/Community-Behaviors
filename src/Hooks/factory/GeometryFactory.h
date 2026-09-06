#pragma once
// Hooks/factory/GeometryFactory.h — build a renderable BSTriShape at RUNTIME from
// packed engine-format buffers, without loading a NIF. RE'd from "Skyrim Precombines"
// (MeshBuilder::Finalize); these are the engine's own geometry-factory functions,
// NOT exposed by CommonLib.
//
// Reuse: static combined meshes (precombines) and — with a BSDynamicTriShape whose
// CPU-writable vertices are rewritten per frame — vertex animation / CPU skinning
// (the tree-bend mechanism, generalized). See the custom-creatures memory.
//
// ⚠ AE-only: the ids/offsets below are the AE (1.6.x) address-library ids and raw AE
// module offsets. This repo's plugins build for AE. If an SE target is ever added,
// these need SE ids and the dynamic path must be gated per runtime.
//
// Plugin-world only.

#include <PCH.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace hooks::geometry {

// BSGraphics::Renderer::CreateTriShape — packs a vertex buffer + index buffer (in the
// engine's own format, described by `vertexDesc`) into renderer data (GPU buffers).
// Returns the BSGraphics::TriShape renderer data, or nullptr. AE id 77260.
inline RE::BSGraphics::TriShape* CreateRendererTriShape(const void* vb, std::uint32_t vbBytes,
                                                        std::uint64_t vertexDesc,
                                                        const void* ib, std::uint32_t indexCount) {
    using Fn = RE::BSGraphics::TriShape* (*)(RE::BSGraphics::Renderer*, const void*, std::uint32_t,
                                             std::uint64_t, const void*, std::uint32_t);
    static const REL::Relocation<Fn> fn{ REL::ID(77260) };
    auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
    return renderer ? fn(renderer, vb, vbBytes, vertexDesc, ib, indexCount) : nullptr;
}

// Allocate a bare BSTriShape (static geometry). AE id 70655.
inline RE::BSTriShape* CreateTriShape() {
    using Fn = RE::BSTriShape* (*)();
    static const REL::Relocation<Fn> fn{ REL::ID(70655) };
    return fn();
}

// Allocate a BSDynamicTriShape (CPU-writable `dynamicData`, re-uploaded each frame —
// the vertex-animation / CPU-skinning path). AE module offset 0xD38A20.
inline RE::BSDynamicTriShape* CreateDynamicTriShape() {
    using Fn = RE::BSDynamicTriShape* (*)();
    static const REL::Relocation<Fn> fn{ REL::Offset(0xD38A20) };
    return fn();
}

// The engine vertex allocator that BSDynamicTriShape's dtor expects to free `dynamicData`.
// Use this (not new/malloc) for the dynamic master copy. AE module offset 0xCE7DD0.
inline void* AllocateVertexData(std::size_t bytes) {
    using Fn = void* (*)(std::size_t);
    static const REL::Relocation<Fn> fn{ REL::Offset(0xCE7DD0) };
    return fn(bytes);
}

// Build a renderable BSTriShape from packed engine-format buffers and wire the renderer
// data + vertex descriptor + counts. `dynamic` builds a BSDynamicTriShape and installs a
// CPU-writable master copy of the vertices (rewrite `dynamicData` per frame for skinning /
// vertex animation).
//
// The caller owns policy the engine also needs before it draws: `local` transform,
// model bound, a shader property (e.g. clone one via NiObjectNET::CreateClone), an
// optional alpha property, AttachChild onto a parent, and SetAppCulled(false).
inline RE::BSTriShape* MakeTriShape(const void* packedVB, std::uint32_t vertCount,
                                    std::uint32_t stride, std::uint64_t vertexDesc,
                                    const std::uint16_t* packedIB, std::uint32_t indexCount,
                                    bool dynamic) {
    if (vertCount == 0 || indexCount == 0) return nullptr;

    auto* rd = CreateRendererTriShape(packedVB, vertCount * stride, vertexDesc, packedIB, indexCount);
    if (!rd) return nullptr;

    RE::BSDynamicTriShape* dyn = dynamic ? CreateDynamicTriShape() : nullptr;
    RE::BSTriShape*        shape = dyn ? static_cast<RE::BSTriShape*>(dyn) : CreateTriShape();
    if (!shape) return nullptr;   // rd leaked on this OOM-only path

    shape->rendererData = rd;
    std::memcpy(&shape->vertexDesc, &vertexDesc, sizeof(std::uint64_t));
    shape->vertexCount   = static_cast<std::uint16_t>(vertCount);
    shape->triangleCount = static_cast<std::uint16_t>(indexCount / 3);

    if (dyn) {
        const std::uint32_t vbBytes = vertCount * stride;
        if (void* dd = AllocateVertexData(static_cast<std::size_t>(vbBytes))) {
            std::memcpy(dd, packedVB, vbBytes);
            dyn->dynamicData = dd;
            dyn->dataSize    = vbBytes;
        }
    }
    return shape;
}

} // namespace hooks::geometry
