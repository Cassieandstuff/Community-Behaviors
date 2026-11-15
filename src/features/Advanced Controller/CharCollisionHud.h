#pragma once

#include "CharDebugHud.h"

#include <array>
#include <cstdint>

struct ImDrawList;
struct ImVec2;

/// @file
/// @brief The collision overlay (char_collision_hud, drawn while the debug HUD is on): what the controlled character's
///        collision queries see each step. The proxy's contact manifold (points, normals, the planes the solve got,
///        the wall planes added), the shape sweeps of its hull, the band between the feet and the floating hull's
///        bottom that no contact reaches (under bPogo), and the controller's two bounds (m_collisionBound,
///        m_bumperCollisionBound).
/// @note Publishers run on the physics thread (the contact callback under the world's write lock), the draw on the
///       render thread: each part is copied under one lock and stamped, as CharDebugHud's are.

namespace CB::feature::controller::hud {

    /// @brief The proxy's manifold as the constraint callback left it, for the controlled character.
    struct Contacts {
        /// @brief What the callback made of a contact's plane.
        enum class Kind : std::uint8_t {
            Block,       ///< kept as the contact's own plane
            Flattened,   ///< made vertical (another character, Bethesda's walls)
            Zeroed       ///< dropped (light clutter, a sensor, a pinch, a sneaking character)
        };
        static constexpr int kPoints = 32;
        static constexpr int kWalls  = 4;
        std::array<Point, kPoints> at{};       ///< the contact point on the other body (Havok world)
        std::array<Point, kPoints> normal{};   ///< its separating normal, pointing at the character
        std::array<Point, kPoints> plane{};    ///< the plane's normal after the callback
        std::array<float, kPoints> distance{};   ///< the separation (negative: penetrating)
        std::array<std::uint8_t, kPoints> layer{};
        std::array<Kind, kPoints>  kind{};
        int                        count = 0, total = 0;   ///< drawn, and in the manifold
        std::array<Point, kWalls>  wall{};     ///< the wall planes appended past the manifold (their normals)
        int                        walls = 0;
        float                      lowest = 0;   ///< the lowest contact's height above the feet (Havok units)
    };

    /// @brief One shape sweep of the controlled character's hull (CharSweep), Havok world space.
    struct Sweep {
        Point from, to;      ///< the shape's translation at the start and the end of the path
        float fraction = 1;  ///< where it stopped, 0 to 1
        bool  hit = false;
        Point at, normal;    ///< the hit point and its normal (pointing away from the hit body)
        unsigned layer = 0;
    };

    /// @brief The character's own volumes this step (the controller's bounds, the floating band), Havok world space.
    struct Volumes {
        Point feet;
        float radius = 0, lift = 0, yaw = 0;   ///< yaw: the actor's heading (radians clockwise from +y)
        /// @brief bhkCharacterController::m_collisionBound / m_bumperCollisionBound (BSBound: centre and half extents,
        ///        game units, in the actor's frame, unscaled), as the controller holds them.
        Point boundCenter, boundExtents, bumperCenter, bumperExtents;
        float scale = 1;
        const void* collidable = nullptr;   ///< the hull's phantom collidable: the sweeps recorded are this one's
        Point origin;                       ///< its translation: a sweep's shape is drawn as `capsules` moved by (stop - origin)
        std::array<hud::Capsule, 6> capsules{};
        int capsuleCount = 0;
    };

    /// @brief Whether the collision overlay runs: the debug HUD is on and char_collision_hud is set.
    bool CollisionOn();
    /// @brief Whether char_collision_hud is set (the overlay draws once the debug HUD is on too).
    bool CollisionFlag();
    /// @brief Switch the overlay on or off and write `[Character] bCollisionHud`.
    /// @note The contacts need it on at launch: their hook is subscribed only then.
    void SetCollisionOn(bool on);

    void Publish(const Contacts& c);
    void Publish(const Volumes& v);
    /// @brief Records a sweep of `collidable` when it is the controlled hull's (Volumes::collidable); a cheap no-op
    ///        otherwise. Keeps the newest kSweeps for a short while.
    void RecordSweep(const void* collidable, const Sweep& s);

    /// @brief CharDebugHud's drawing helpers, shared with the collision overlay (world points in Havok units).
    namespace draw {
        /// @brief The point on screen through the world root camera; false when behind it or there is no camera.
        bool ToScreen(const Point& p, ImVec2& out);
        void Line(ImDrawList* dl, const Point& a, const Point& b, std::uint32_t color, float thickness);
        void Capsule(ImDrawList* dl, const hud::Capsule& c, std::uint32_t color);
        void Ring(ImDrawList* dl, const Point& c, float radius, std::uint32_t color);
        void Arrow(ImDrawList* dl, const Point& from, const Point& delta, std::uint32_t color);
    }  // namespace draw

}  // namespace CB::feature::controller::hud
