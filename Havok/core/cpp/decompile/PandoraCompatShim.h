// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>

// PandoraCompatShim — CONVERTER-ONLY. Call ONLY from the offline ingest (the converter's
// Nemesis patch-application sites), NEVER from the runtime compile path. It lives in
// havok-framing beside xml::StripPatchOriginals (the sibling Nemesis MOD_CODE handler) so
// both converter cores — havok-core (PatchConverter) and havok-model (parseSourcesMerged) —
// can reach it; its mere presence in the linked binary changes nothing at runtime because
// the live Resolver/BehaviorCompiler consumes pre-baked .hky bundles and never applies raw
// Nemesis text.
//
// WHY IT EXISTS
// A Nemesis `#NNNN.txt` edit to a *numeric text-array* hkparam (e.g. hkbBoneWeightArray's
// `boneWeights`) is applied by Pandora (PackFileEditor.ReplaceText) as an OCCURRENCE-COUNTED
// text replacement, not a positional element replacement: the block's ORIGINAL text becomes
// a whitespace-flexible regex, and Pandora replaces the Nth NON-OVERLAPPING match of it in
// the base array, where N = how many times ORIGINAL already occurs in the text PRECEDING the
// block. When the edited window lies inside a longer run of identical values, that match
// snaps to the run's start, so the edit lands a few elements early (see docs/bugs/CB-3: a
// Weapon Styles boneWeight edit authored at the L-arm twist bones lands 2 bones early on the
// toes under Pandora). CB's native path (xml::StripPatchOriginals) instead keeps each OPEN
// block in place positionally — more literally faithful to the mod's authored line offsets,
// but divergent from the Nemesis/Pandora output the whole mod ecosystem was validated on.
// This shim makes the CONVERTER reproduce that ecosystem placement so bundles match Pandora.

namespace havok::compat {

// Rewrite every pure-text hkparam in `src` that carries MOD_CODE blocks to the value Pandora
// would produce, consuming those blocks' comment markers. Params that contain nested elements
// (ref/struct params) and INSERT-only blocks are left untouched — the caller's existing
// xml::StripPatchOriginals pass still handles those, unchanged. Idempotent-safe to run
// immediately BEFORE StripPatchOriginals at a converter patch-application site.
void ApplyNemesisTextArrayEdits(std::string& src);

}  // namespace havok::compat
