#pragma once
#include "havok/skeleton/SkeletonData.h"

#include <cstdint>
#include <string>
#include <vector>

// Skeleton IMPORT — binary skeleton.hkx → plain SkeletonData. SCHEMA-NATIVE: the packfile is read
// through havok-io's generic SchemaObject path (MakeSchemaFactory over the shared registry), NOT the
// typed hka*/hkp* classes — so the reader carries no havok-core dependency and the same function serves
// every consumer (the compiler resolving bones for the membrane, the converter, and a tool like the
// Scene Editor via CB-API). Its typed havok-core ancestor stays only as the offline correctness oracle.
//
// The output is deliberately NOT hkaSkeleton: plain names, parent indices, transforms, and the authored
// ragdoll knobs, so callers need no Havok headers.

namespace havok::skeleton {

// Reads every hkaSkeleton in `bytes`, in file order.
//
// A Skyrim skeleton.hkx commonly holds MORE THAN ONE: the animation skeleton first, then a ragdoll
// skeleton with a reduced bone set. Callers that want "the" skeleton should take out[0], which matches
// the file order the old typed path relied on (it took the first hkaSkeleton).
//
// Returns false and fills `err` on a malformed file. An empty result with a true return means the file
// parsed but contained no skeleton.
bool LoadSkeletonsFromHkx(const std::uint8_t* data, std::size_t size,
                          std::vector<SkeletonData>& out,
                          std::string* err = nullptr);

// Reads the ragdoll PHYSICS from a full skeleton.hkx (rigid bodies + constraints) and attaches the
// AUTHORED knobs (mass, radius, joint type + limits) onto the matching bones of `animSkel` by NAME
// (the anim↔ragdoll hkaSkeletonMapper is authoritative; a "Ragdoll_" name-strip is the fallback).
// Everything derivable is dropped. `animSkel` should be the animation skeleton (out[0] of
// LoadSkeletonsFromHkx). No-op (returns true) if the file has no physics. Returns false + `err` on a
// malformed file.
bool ReadSkeletonPhysics(const std::uint8_t* data, std::size_t size,
                         SkeletonData& animSkel,
                         std::string* err = nullptr);

} // namespace havok::skeleton
