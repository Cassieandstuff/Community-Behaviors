#pragma once

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Agent/Query/hkpCdPointCollector.h>
#include <BSHavok/bhkWorld.h>

#include <hky/runtime/Physics.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/// @file
/// @brief CB's in-game backend of hky::physics: the game's own bhkWorld, queried through IPhysicsWorld. Game units at
///        the interface, Havok units (metres) inside, as every backend.
/// @note  The game owns and steps its world: this backend only queries it (CastRay, CastShape). Every call that would
///        create, move or step bodies does nothing, and says so where it can (CreateShape, AddBody).
/// @note  A hit's userData is the hit root collidable (`const hkpCollidable*`), its body id empty (the game's bodies are
///        not ours to name); its layer is the collidable's collision layer.
/// @pre   Queried where vanilla may query the world: inside a character's update, outside the physics step. A shape cast
///        holds the world's read lock for the cast; a ray goes through bhkWorld::PickObject.

namespace CB::platform::physics {

/// @brief A game collidable as an hky shape, for CastShape: what a character controller sweeps is its live phantom.
/// @note  A live shape keeps its collidable's rotation whatever the cast's `from` says: the hull turns with its body.
class LiveShape final : public hky::physics::IShape {
public:
    explicit LiveShape(const ::hkpCollidable* collidable) : m_collidable(collidable) {}
    [[nodiscard]] const ::hkpCollidable* Collidable() const { return m_collidable; }
    /// @note Not read from the game: empty.
    [[nodiscard]] hky::physics::Aabb LocalBounds() const override { return {}; }

private:
    const ::hkpCollidable* m_collidable;
};

/// @brief A caller's say over each sweep hit: return false to pass through it (Havok units).
/// @note  Runs inside the cast, under the world's read lock; a refused hit never hides one beyond it.
struct NativeHitFilter {
    bool (*accept)(void* user, const ::hkpCollidable* body, float fraction) = nullptr;
    void* user = nullptr;
};

/// @brief The first thing a native sweep met (Havok units).
struct NativeSweepHit {
    bool                   hit      = false;
    float                  fraction = 1.0f;   ///< of the path, 0 to 1
    ::hkContactPoint       contact{};         ///< on the hit body; normal points away from it, w the fraction
    const ::hkpCollidable* body = nullptr;    ///< the hit root collidable
};

/// @brief Sweeps `collidable`'s shape from `from` to `to` (Havok world space, the collidable's rotation) and returns the
///        closest hit: hkpWorld::linearCast over a stand-in of the collidable at `from`.
/// @param ignoreAboveNormalZ hits on faces flatter than this (normal z above it) are skipped.
/// @param ignoreLayers bit n set: hits on collision layer n are skipped.
/// @param rejectLeaving hits the motion leaves (contact normal . displacement >= 0) are skipped, as Jolt's
///        ContactCastCollector does: a horizontal hull sweep otherwise meets the floor it slides over.
NativeSweepHit SweepNative(::bhkWorld* world, const ::hkpCollidable* collidable, const ::hkVector4& from, const ::hkVector4& to,
                           float startTolerance, float ignoreAboveNormalZ, std::uint64_t ignoreLayers, bool rejectLeaving,
                           const NativeHitFilter* filter);

class GameWorld final : public hky::physics::IPhysicsWorld {
public:
    explicit GameWorld(::bhkWorld* world) : m_world(world) {}

    void SetLayerTable(const hky::physics::LayerTable&) override {}
    void SetGravity(const hky::physics::Vec3&) override {}
    [[nodiscard]] hky::physics::ShapeRef CreateShape(const hky::physics::ShapeDesc&, std::string* error) override;
    [[nodiscard]] hky::physics::BodyId AddBody(const hky::physics::BodyDesc&, std::string* error) override;
    void RemoveBody(hky::physics::BodyId) override {}
    void SetTransform(hky::physics::BodyId, const hky::physics::Transform&) override {}
    void MoveKinematic(hky::physics::BodyId, const hky::physics::Transform&, float) override {}
    void SetVelocity(hky::physics::BodyId, const hky::physics::Vec3&, const hky::physics::Vec3&) override {}
    void AddImpulse(hky::physics::BodyId, const hky::physics::Vec3&, const std::optional<hky::physics::Vec3>&) override {}
    void Activate(hky::physics::BodyId) override {}
    void Sleep(hky::physics::BodyId) override {}
    [[nodiscard]] hky::physics::Transform GetTransform(hky::physics::BodyId) const override { return {}; }
    void GetVelocity(hky::physics::BodyId, hky::physics::Vec3& linear, hky::physics::Vec3& angular) const override { linear = {}, angular = {}; }
    [[nodiscard]] bool IsSleeping(hky::physics::BodyId) const override { return false; }
    void ReadBodies(std::vector<hky::physics::BodyState>& out) const override { out.clear(); }
    float Step(float) override { return 0.0f; }

    /// @brief bhkWorld::PickObject from `from` to `to`, with `filter.native` as the ray's Havok filter info (else the
    ///        filter's layer); `ignoreLayers` and `maxNormalZ` are applied to its one hit.
    [[nodiscard]] std::optional<hky::physics::Hit> CastRay(const hky::physics::Vec3& from, const hky::physics::Vec3& to,
                                                          const hky::physics::QueryFilter& filter) const override;
    /// @note PickObject returns one hit: this gives that one.
    void CastRayAll(const hky::physics::Vec3& from, const hky::physics::Vec3& to, const hky::physics::QueryFilter& filter,
                    std::vector<hky::physics::Hit>& out) const override;
    /// @brief SweepNative for a LiveShape (no other shape is the game's): at most its closest hit. The pair filter is the
    ///        collidable's own; `ignoreLayers`, `maxNormalZ`, `rejectLeaving` and `startTolerance` apply.
    void CastShape(const hky::physics::ShapeRef& shape, const hky::physics::Transform& from, const hky::physics::Vec3& to,
                   const hky::physics::QueryFilter& filter, std::vector<hky::physics::Hit>& out) const override;
    void Overlap(const hky::physics::ShapeRef&, const hky::physics::Transform&, const hky::physics::QueryFilter&,
                 std::vector<hky::physics::Hit>& out) const override { out.clear(); }
    void DebugTriangles(hky::physics::BodyId, std::vector<hky::physics::Vec3>& out) const override { out.clear(); }

private:
    ::bhkWorld* m_world;
};

}  // namespace CB::platform::physics
