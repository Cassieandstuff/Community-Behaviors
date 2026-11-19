#pragma once

#include <Common/Base/hkBase.h>
#include <BSHavok/bhkWorld.h>

#include <hky/runtime/Physics.h>

#include <platform/physics/QueryTag.h>

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
/// @pre   Queried where vanilla may query the world: inside a character's update, outside the physics step.
/// @note  THE way CB queries the game's world: nothing outside platform/physics calls PickObject or linearCast. Every
///        query is counted, timed and zoned under the QueryTag the GameWorld was made with (QueryStats.h), its wait for
///        the world's read lock timed apart: a shape cast takes that lock for the cast, a ray takes it around
///        bhkWorld::PickObject, which takes it again inside (a reader's lock nests: BSReadWriteLock::LockForRead blocks
///        only on a writer that holds it).

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

class GameWorld final : public hky::physics::IPhysicsWorld {
public:
    GameWorld(::bhkWorld* world, QueryTag& tag) : m_world(world), m_tag(&tag) {}

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
    ///        filter's layer); `ignoreLayers`, `maxNormalZ` and `accept` are applied to its one hit. A pick the game skips
    ///        for its frame budget is no hit, counted as skipped.
    [[nodiscard]] std::optional<hky::physics::Hit> CastRay(const hky::physics::Vec3& from, const hky::physics::Vec3& to,
                                                          const hky::physics::QueryFilter& filter) const override;
    /// @note PickObject returns one hit: this gives that one.
    void CastRayAll(const hky::physics::Vec3& from, const hky::physics::Vec3& to, const hky::physics::QueryFilter& filter,
                    std::vector<hky::physics::Hit>& out) const override;
    /// @brief hkpWorld::linearCast of a LiveShape (no other shape is the game's) over a stand-in of its collidable placed
    ///        at `from`: at most the closest hit. The pair filter is the collidable's own; `ignoreLayers`, `maxNormalZ`,
    ///        `rejectLeaving`, `startTolerance` and `accept` apply, the collidable itself is never a hit.
    void CastShape(const hky::physics::ShapeRef& shape, const hky::physics::Transform& from, const hky::physics::Vec3& to,
                   const hky::physics::QueryFilter& filter, std::vector<hky::physics::Hit>& out) const override;
    void Overlap(const hky::physics::ShapeRef&, const hky::physics::Transform&, const hky::physics::QueryFilter&,
                 std::vector<hky::physics::Hit>& out) const override { out.clear(); }
    void DebugTriangles(hky::physics::BodyId, std::vector<hky::physics::Vec3>& out) const override { out.clear(); }

private:
    [[nodiscard]] std::optional<hky::physics::Hit> Ray(const hky::physics::Vec3& from, const hky::physics::Vec3& to,
                                                      const hky::physics::QueryFilter& filter, QueryTimer& timer) const;

    ::bhkWorld* m_world;
    QueryTag*   m_tag;
};

}  // namespace CB::platform::physics
