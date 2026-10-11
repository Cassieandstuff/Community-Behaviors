#pragma once

#include <core/hooks/points/HookPoint.h>

namespace CB::core::hooks {

/// @brief LoadClips — binds a project's clips at graph load. Pass 1 maps the roster (lowercased filename →
///        position); pass 2 visits every EXACT hkbClipGenerator with no animationControl attached and stamps
///        its int16 animationBindingIndex (+0x70) from that map, or, for a clip whose file is not in the roster,
///        loads the file by name through `assetLoader` and attaches it directly (index stays -1).
/// @note The only live writer of +0x70 in both SE 1.5.97 and AE: Havok's own linkers
///       (hkbCharacter_LinkGraph_AssignRosterIndices / ClipGen_BindOrAssignRosterIndex) have no callers and no
///       pointers in either build. A clip that already has a control attached when this runs is skipped
///       entirely — the engine-supported index-free state (Activate plays the attached control when +0x70 is -1).
/// @note Arguments past `root` are named for their observed use: `loadArg` goes to the asset loader's load
///       (vtable +0x18) with each unrostered clip's name; `triggerArg` to the annotation→trigger merge
///       (ClipGen_MergeAnnotationTriggers); `resourceArg` with each loaded resource to its state fix-up. Seven
///       arguments in both builds (RCX/RDX/R8/R9 + three stack slots), stringData loaded from hkbCharacterData+0x98.
struct LoadClips : HookPoint<LoadClips,
    void(RE::hkbCharacterStringData* stringData, RE::hkbAnimationBindingSet* bindingSet, void* assetLoader,
         RE::hkbGenerator* root, void* loadArg, void* triggerArg, void* resourceArg)> {
    static constexpr const char*           name = "LoadClips";
    static constexpr Seam                  seam = Seam::Entry;
    static constexpr const engine::Symbol& symbol = engine::LoadClips;
};

/// @brief Appends one binding-set slot per BSSynchronizedClipGenerator (paired / sync animations) after the
///        roster's slots and stores that slot's int16 index at BSSync +0x128 — the second 16-bit index in the
///        clip-binding path, so the effective binding-set cap is roster + paired nodes.
/// @note Reads the child clip's +0x70 and only appends for a child with a valid index: a child left at -1 by an
///       index-free bind gets no scene slot. Called from the graph-load routine that runs LoadClips.
struct BSSyncAppendSceneBindingSlots : HookPoint<BSSyncAppendSceneBindingSlots,
    bool(RE::hkbBehaviorGraph* graph, RE::hkbCharacter* character, void* arg)> {
    static constexpr const char*           name = "BSSyncAppendSceneBindingSlots";
    static constexpr Seam                  seam = Seam::Entry;
    static constexpr const engine::Symbol& symbol = engine::BSSync_AppendSceneBindingSlots;
};

}  // namespace CB::core::hooks
