#pragma once
// ProjectYaml — the bidirectional project.yaml <-> ProjectSpec format codec (org-pass firesale, the
// character/project decompile port). A project is a flat string-table record (hkbProjectData ->
// hkbProjectStringData), so its authoring form is one flat project.yaml; this codec is the two thin
// leaf halves the decompile/compile wrappers assemble:
//   EmitProjectYaml  (spec -> yaml)  the decompile leg  (paired with compile/ReadProject: bytes -> spec)
//   ParseProjectYaml (yaml -> spec)  the compile leg    (paired with compile/AssembleProject: spec -> bytes)
// std-only, no ryml, no typed hkb* — drained out of the quarantined havok-core ProjectCompiler.cpp.
// Namespace kept havok::sct (normalization deferred to the mechanical pass).

#include <interface/ProjectData.h>   // model::ProjectSpec

#include <string>

namespace havok::sct {

using havok::model::ProjectSpec;

// spec -> project.yaml text (the decompile leg). Verbatim string fields, %g floats — parse(emit(s)) == s.
std::string EmitProjectYaml(const ProjectSpec& spec);

// project.yaml text -> spec (the compile leg). Returns false only on malformed input.
bool ParseProjectYaml(const std::string& text, ProjectSpec& out);

} // namespace havok::sct
