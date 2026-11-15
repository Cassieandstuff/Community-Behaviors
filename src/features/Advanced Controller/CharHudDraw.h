#pragma once

#include "CharCollisionHud.h"

#include <cstdint>

struct ImDrawList;
struct ImVec2;

/// @file
/// @brief The HUD drawing helpers, kept out of CharCollisionHud.h: a global ImVec2 forward declaration there
///        breaks CharConfigMenu.cpp, where SKSEMenuFramework.h declares its own ImGuiMCP::ImVec2.

namespace CB::feature::controller::hud {
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
