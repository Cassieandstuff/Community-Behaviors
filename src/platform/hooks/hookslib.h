#pragma once
// platform/hooks/hookslib.h — umbrella for the repo's RE hook toolkit: a miniature CommonLib
// for the engine findings we RE'd ourselves (offsets, install plumbing) that are NOT in
// CommonLib-NG.
//
// Include this ONE header to reach every install helper and engine binding:
//
//     #include <platform/hooks/hookslib.h>
//
// Design (see the AskUserQuestion decision, 2026-08-23): the library holds the shared
// MECHANISM and the RE'd ENGINE BINDINGS. Hook BODIES stay in their plugin — a plugin
// defines its detour, then installs it with CB::platform::hooks::InstallCallDetour
// against a binding declared here. Nothing here installs itself; installation is
// opt-in per plugin.
//
// Plugin-world only (needs RE / SKSE / CommonLib).

#include <platform/hooks/factory/Install.h>          // InstallCallDetour
#include <platform/hooks/registry/Registry.h>        // engine symbol registry (engine.yaml → per-runtime Address Library pins)
#include <platform/hooks/points/HookPoint.h>         // the shared hook interface: core-owned seams, subscriber chains
#include <platform/hooks/points/Movement.h>          // actor movement hook points (MovementControllerFrame)
#include <platform/hooks/points/Resource.h>         // resource-loading hook points (ResolveTypedHkx, AdsfOpen, AsdsfOpen)
#include <platform/hooks/points/SetData.h>           // setdata gate hook points (matcher, weapon producer)
