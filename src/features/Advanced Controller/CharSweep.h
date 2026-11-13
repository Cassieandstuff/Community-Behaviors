#pragma once

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Agent/Query/hkpCdPointCollector.h>
#include <BSHavok/bhkWorld.h>

#include <platform/physics/GameWorld.h>

#include <cstdint>

/// @file
/// @brief The character controller spec's sweep helper (step 2b): moves a collidable's shape along a straight path
///        through the Havok world and reports the first hit. Shared by the step query (M7), the predictor (P2) and the
///        camera. A sweep goes through the game's hky::physics backend (platform::physics::GameWorld::CastShape); one
///        with a HitFilter calls the backend's native sweep, since the interface has no per-hit callback.

namespace CB::feature::controller {

    /// @brief The first thing a sweep met: on the hit body, its contact normal pointing away from it.
    using SweepHit = platform::physics::NativeSweepHit;

    /// @brief A caller's say over each hit: return false to pass through it.
    /// @note Runs inside the cast, under the world's read lock; a refused hit never hides one beyond it.
    using HitFilter = platform::physics::NativeHitFilter;

    /// @brief Sweeps `collidable`'s shape from its current transform to `to` (Havok world space) and returns the first
    ///        hit, holding the world's read lock for the cast.
    /// @param startTolerance points already within this distance at the start are ignored as path hits (Havok's
    ///        m_startPointTolerance).
    /// @param ignoreAboveNormalZ hits on faces flatter than this (normal z above it) are skipped: pass the actor's max
    ///        slope cosine to see only obstacles (walls, steep faces); leave it at 2 to keep floors (a down sweep).
    /// @note Hits the motion leaves (contact normal . displacement >= 0) are always skipped, as Jolt's
    ///       ContactCastCollector does: the first capture showed a horizontal hull sweep meeting the floor it slides over.
    /// @pre Called where vanilla may query the world (inside the character's update, outside the physics step).
    SweepHit Sweep(::bhkWorld* world, const ::hkpCollidable* collidable, const ::hkVector4& to,
                   float startTolerance, float ignoreAboveNormalZ = 2.0f, std::uint64_t ignoreLayers = 0);

    /// @brief Sweep with the shape starting at `from` instead of the collidable's current transform (the step query's
    ///        up / forward / down legs, the predictor's later segments).
    /// @param ignoreLayers bit n set = hits on collision layer n are skipped (e.g. 1 << 30, other characters).
    /// @param filter optional, see HitFilter.
    /// @note The rotation stays the collidable's.
    SweepHit SweepFrom(::bhkWorld* world, const ::hkpCollidable* collidable, const ::hkVector4& from,
                       const ::hkVector4& to, float startTolerance, float ignoreAboveNormalZ = 2.0f, std::uint64_t ignoreLayers = 0,
                       const HitFilter* filter = nullptr);

}  // namespace CB::feature::controller
