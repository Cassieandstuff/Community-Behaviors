#pragma once
#include "interface/SkeletonData.h"
#include "interface/UnitSource.h"

#include <cstdint>
#include <string>
#include <vector>

// Skeleton IMPORT — binary skeleton.hkx → plain SkeletonData. SCHEMA-NATIVE: the packfile is read
// through the generic io::SchemaObject path (MakeSchemaFactory over the shared registry), so the same
// function serves every consumer (the converter, the CB-API facade).
//
// The output is deliberately NOT hkaSkeleton: plain names, parent indices, transforms, and the authored
// ragdoll knobs, so callers need no Havok headers.

namespace CB::engine::schema { class SchemaRegistry; }

namespace CB::engine::skeleton {

// Reads every hkaSkeleton in `bytes`, in file order.
//
// A Skyrim skeleton.hkx commonly holds MORE THAN ONE: the animation skeleton first, then a ragdoll
// skeleton with a reduced bone set. Callers that want "the" skeleton should take out[0].
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

/// @brief A skeleton UNIT from a bundle (its reflective `animation/` + `ragdoll/` folders) -> the animation skeleton as
///        SkeletonData, with ragdoll physics attached: the unit is parsed and serialized as CB serves it, then read like
///        a skeleton.hkx (LoadSkeletonsFromHkx out[0] + ReadSkeletonPhysics).
/// @param unitPath the unit's Data-relative path ("meshes/…/skeleton.hkx"); its bone order comes from the boneorder shim.
/// @return false with `err` set when the unit doesn't parse or holds no skeleton.
bool ReadSkeletonUnit(const CB::engine::common::IUnitSource& unit, const schema::SchemaRegistry& reg,
                      const std::string& unitPath, SkeletonData& out, std::string* err = nullptr);

} // namespace CB::engine::skeleton
