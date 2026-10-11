#pragma once
// core/hooks/hookslib.h — umbrella for the repo's RE hook toolkit: a miniature CommonLib
// for the engine findings we RE'd ourselves (offsets, vtable ids, factories, install
// plumbing) that are NOT in CommonLib-NG.
//
// Include this ONE header to reach every install helper and engine binding:
//
//     #include <core/hooks/hookslib.h>
//
// Design (see the AskUserQuestion decision, 2026-08-23): the library holds the shared
// MECHANISM and the RE'd ENGINE BINDINGS. Hook BODIES stay in their plugin — a plugin
// defines its detour, then installs it with CB::core::hooks::InstallVFunc / InstallCallDetour
// against a binding declared here. Nothing here installs itself; installation is
// opt-in per plugin.
//
// Plugin-world only (needs RE / SKSE / CommonLib).

#include <core/hooks/factory/Install.h>          // InstallVFunc / InstallCallDetour
#include <core/hooks/factory/VTable.h>           // VTableAddress / VTablePtr / CopyVTable
#include <core/hooks/factory/GeometryFactory.h>  // runtime BSTriShape creation (mesh injection)
#include <core/hooks/factory/SkinFactory.h>      // NiSkinData / NiSkinPartition factories (skinning)
#include <core/hooks/registry/Registry.h>        // engine symbol registry (engine.yaml → per-runtime Address Library pins)
#include <core/hooks/points/HookPoint.h>         // the shared hook interface: core-owned seams, subscriber chains
#include <core/hooks/points/Resource.h>          // resource-loading hook points (ResolveTypedHkx, AdsfOpen, AsdsfOpen)
#include <core/hooks/points/SetData.h>           // setdata gate hook points (matcher, weapon producer)
#include <core/hooks/points/ClipBinding.h>       // clip-binding hook points (LoadClips, BSSync scene slots)

// Add new binding headers here as findings are centralized:
//   <core/hooks/factory/AnimGraph.h>   — animationdata / setdata loader sites, clip vtables
//   <core/hooks/factory/Camera.h>      — camera-state vtables
//   ...
