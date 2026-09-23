#pragma once
// MotionSample — the root-motion SAMPLE-LABEL codec (the "t x y z" token <-> the labeled
// "t: <t>, x: <x>, …" form). A motion sample is carried VERBATIM (the cache's own %g float tokens,
// never reparsed) so every round-trip stays byte-exact; this pair only adds/strips the axis labels
// for readable YAML.
//
// Home ruling (org-pass): this is the ONE format transform that rides in `interface` beside its
// struct — MotionRecord lives in <interface/AnimationData.h>, and both the adsf sidecar codec
// (codec/format) and the animation.yaml `motion:` loader (codec/format/AnimationYamlLoader) must
// produce byte-identical samples through the SAME rule. `codec` depends on `interface`, so hosting
// the rule here (rather than in codec) lets every layer call it with no cycle. std-only.

#include <string>

namespace havok::animdata {

// A canonical "t x y z" (translation) / "t x y z w" (rotation) verbatim sample -> the labeled,
// readable form "t: <t>, x: <x>, …" (float tokens kept verbatim). Labels are chosen by token count,
// so the caller never needs to know which array it's in; >5 tokens (never for motion) stay bare.
std::string LabelSample(const std::string& verbatim);

// Inverse of LabelSample, tolerant of the legacy bare "t x y z" form: split on ',' when labeled
// else on ' ', drop each "axis:" prefix, and rejoin the raw tokens with a single space — the
// canonical verbatim string the model + .txt carry. Idempotent on bare input.
std::string UnlabelSample(const std::string& labeledOrBare);

}  // namespace havok::animdata
