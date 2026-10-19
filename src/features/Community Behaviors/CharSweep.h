#pragma once

#include <Physics/Collide/Agent/Query/hkpCdPointCollector.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkWorld.h>

/// @file
/// @brief The character controller spec's sweep helper (step 2b): moves a collidable's shape along a straight path
///        through the Havok world with hkpWorld::linearCast and reports the first hit. Shared by the step query (M7)
///        and the predictor (P2).

namespace CB::feature::character {

    /// @brief The first thing a sweep met.
    struct SweepHit {
        bool                       hit      = false;
        float                      fraction = 1.0f;   ///< of the path, 0 to 1
        CB::schema::hkContactPoint contact{};          ///< on the hit body; normal points away from it
        const CB::schema::hkpCollidable* body = nullptr;   ///< the hit root collidable
    };

    /// @brief Sweeps `collidable`'s shape from its current transform to `to` (Havok world space) and returns the first
    ///        hit, holding the world's read lock for the cast.
    /// @param startTolerance points already within this distance at the start are ignored as path hits (Havok's
    ///        m_startPointTolerance).
    /// @param ignoreAboveNormalZ hits on faces flatter than this (normal z above it) are skipped: pass the actor's max
    ///        slope cosine to see only obstacles (walls, steep faces); leave it at 2 to keep floors (a down sweep).
    /// @note Hits the motion leaves (contact normal . displacement >= 0) are always skipped, as Jolt's
    ///       ContactCastCollector does: the first capture showed a horizontal hull sweep meeting the floor it slides over.
    /// @pre Called where vanilla may query the world (inside the character's update, outside the physics step).
    SweepHit Sweep(CB::schema::bhkWorld* world, const CB::schema::hkpCollidable* collidable, const CB::schema::hkVector4& to,
                   float startTolerance, float ignoreAboveNormalZ = 2.0f);

}  // namespace CB::feature::character
