#pragma once
// Forwarding shim (org-pass firesale phase 3f): the havok-model facade split into its two directions —
//   compile half  -> <compile/GraphBuild.h>        (Build*/Assemble*/ResolveBehaviorBindings)
//   decompile half-> <decompile/BehaviorDecompile.h> (Identity/Emit*/tagfile/Convert)
// Kept so <havok-model/HavokModel.h> consumers keep working; delete when they migrate to the halves.
#include <compile/GraphBuild.h>
#include <decompile/BehaviorDecompile.h>
