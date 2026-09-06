#pragma once
// Hooks/factory/SkinFactory.h — engine object factories for runtime skin construction,
// RE'd from the NiObjectFactory registration table (FUN_140d20b70 @ 0xD41880). Each
// factory does MemoryManager::Allocate + NiObject::ctor + sets the real vtable, so the
// returned object is a valid NiObject — we NEVER hand-stamp a vtable (that manual path
// is what crashed the Precombines shader clones).
//
// NiSkinInstance::Create is already wrapped by CommonLib (RE::NiSkinInstance::Create());
// only NiSkinData / NiSkinPartition are missing, so we bind those two here.
//
// ⚠ AE module offsets (image base 0x140000000). AE-only until SE ids are added.
//
// Plugin-world only.

#include <PCH.h>

namespace hooks::skin {

// NiSkinData::Create — alloc 0x60, vtable, identity rootParentToSkin, null boneData. AE 0xD49310.
inline RE::NiSkinData* CreateSkinData() {
    using Fn = RE::NiSkinData* (*)();
    static const REL::Relocation<Fn> fn{ REL::Offset(0xD49310) };
    return fn();
}

// NiSkinPartition::Create — alloc 0x28, vtable, empty partitions. AE 0xD42920.
inline RE::NiSkinPartition* CreateSkinPartition() {
    using Fn = RE::NiSkinPartition* (*)();
    static const REL::Relocation<Fn> fn{ REL::Offset(0xD42920) };
    return fn();
}

} // namespace hooks::skin
