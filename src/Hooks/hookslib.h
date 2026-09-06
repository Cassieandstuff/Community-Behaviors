#pragma once
// Hooks/hookslib.h — umbrella for the repo's RE hook toolkit: a miniature CommonLib
// for the engine findings we RE'd ourselves (offsets, vtable ids, factories, install
// plumbing) that are NOT in CommonLib-NG.
//
// Include this ONE header to reach every install helper and engine binding:
//
//     #include <Hooks/hookslib.h>
//
// Design (see the AskUserQuestion decision, 2026-08-23): the library holds the shared
// MECHANISM and the RE'd ENGINE BINDINGS. Hook BODIES stay in their plugin — a plugin
// defines its detour, then installs it with hooks::InstallVFunc / InstallCallDetour
// against a binding declared here. Nothing here installs itself; installation is
// opt-in per plugin.
//
// Plugin-world only (needs RE / SKSE / CommonLib).

#include <Hooks/factory/Install.h>          // InstallVFunc / InstallCallDetour
#include <Hooks/factory/VTable.h>           // VTableAddress / VTablePtr / CopyVTable
#include <Hooks/factory/GeometryFactory.h>  // runtime BSTriShape creation (mesh injection)
#include <Hooks/factory/SkinFactory.h>      // NiSkinData / NiSkinPartition factories (skinning)

// Add new binding headers here as findings are centralized:
//   <Hooks/factory/AnimGraph.h>   — animationdata / setdata loader sites, clip vtables
//   <Hooks/factory/Camera.h>      — camera-state vtables
//   ...
