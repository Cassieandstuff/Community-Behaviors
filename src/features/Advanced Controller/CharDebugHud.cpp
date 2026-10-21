#include "PCH.h"

#include "CharDebugHud.h"
#include "CharFacing.h"

#include <core/bootstrap/ProgressHud.h>
#include <core/debug/Settings.h>

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>

/// @file
/// @brief The character debug HUD (char_debug_hud): the player's floor rays, velocity arrows, predicted trajectory
///        and a text panel, drawn with ProgressHud's ImGui over the finished frame.
/// @note Publishers run on the physics thread and the draw on the render thread, so each part is copied under one
///       lock and stamped; a part older than kStale seconds is not drawn (a load, a menu, a feature switched off).

namespace CB::feature::controller::hud {

    namespace {

        constexpr float  kGameUnitsPerHavok = 69.99125f;
        constexpr double kStale             = 0.5;
        constexpr float  kArrowSeconds      = 0.5f;   ///< an arrow is where its velocity reaches in this time

        struct Store {
            std::mutex lock;
            Controller ctrl;
            Prediction pred;
            Ramp       ramp;
            Matching   match;
            double     ctrlAt = -1.0e9, predAt = -1.0e9, rampAt = -1.0e9, matchAt = -1.0e9;
        };

        Store& S()
        {
            static Store s;
            return s;
        }

        double Now()
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        const ImU32 kWalkable = IM_COL32(80, 230, 80, 255);
        const ImU32 kSteep    = IM_COL32(255, 160, 40, 255);
        const ImU32 kWall     = IM_COL32(240, 60, 60, 255);
        const ImU32 kMiss     = IM_COL32(150, 150, 150, 160);
        const ImU32 kVelocity = IM_COL32(255, 255, 255, 255);
        const ImU32 kDesired  = IM_COL32(60, 200, 255, 255);
        const ImU32 kAirborne = IM_COL32(120, 140, 255, 255);
        const ImU32 kStick      = IM_COL32(255, 220, 40, 255);
        const ImU32 kFacingTick = IM_COL32(255, 255, 255, 255);
        constexpr float kRadToDeg = 57.29578f;

        /// @brief Airborne blue; on the ground, orange (holds nothing) through green (full support).
        ImU32 GroundColor(Ground g, float support)
        {
            if (g == Ground::Airborne) return kAirborne;
            const float s = std::clamp(support, 0.0f, 1.0f);
            const auto  mix = [s](int lo, int hi) { return static_cast<int>(static_cast<float>(lo) + static_cast<float>(hi - lo) * s); };
            return IM_COL32(mix(255, 80), mix(160, 230), mix(40, 80), 255);
        }

        /// @brief World (Havok units) to screen pixels through the world root camera.
        /// @note NiCamera::WorldPtToScreenPt3 returns the point in the camera's port (0-1, y up); a point behind the
        ///       camera is skipped.
        struct Projector {
            RE::NiCamera* camera = nullptr;
            ImVec2        size;

            bool operator()(const Point& p, ImVec2& out) const
            {
                const RE::NiPoint3 world(p.x * kGameUnitsPerHavok, p.y * kGameUnitsPerHavok, p.z * kGameUnitsPerHavok);
                float x = 0, y = 0, z = 0;
                if (!camera->WorldPtToScreenPt3(world, x, y, z, 1.0e-5f) || z <= 0.0f) return false;
                out = ImVec2(x * size.x, (1.0f - y) * size.y);
                return true;
            }
        };

        void Line(ImDrawList* dl, const Projector& proj, const Point& a, const Point& b, ImU32 color, float thickness)
        {
            ImVec2 sa, sb;
            if (proj(a, sa) && proj(b, sb)) dl->AddLine(sa, sb, color, thickness);
        }

        void Arrow(ImDrawList* dl, const Projector& proj, const Point& from, const Point& velocity, ImU32 color)
        {
            const Point to{ from.x + velocity.x * kArrowSeconds, from.y + velocity.y * kArrowSeconds, from.z + velocity.z * kArrowSeconds };
            ImVec2 sa, sb;
            if (!proj(from, sa) || !proj(to, sb)) return;
            dl->AddLine(sa, sb, color, 3.0f);
            const float dx = sb.x - sa.x, dy = sb.y - sa.y, len = std::sqrt(dx * dx + dy * dy);
            if (len < 4.0f) return;
            const float ux = dx / len, uy = dy / len, head = (std::min)(14.0f, len * 0.4f);
            dl->AddTriangleFilled(sb, ImVec2(sb.x - ux * head - uy * head * 0.5f, sb.y - uy * head + ux * head * 0.5f),
                                  ImVec2(sb.x - ux * head + uy * head * 0.5f, sb.y - uy * head - ux * head * 0.5f), color);
        }

        void Ring(ImDrawList* dl, const Projector& proj, const Point& c, float radius, ImU32 color)
        {
            constexpr int kSegments = 24;
            Point prev{ c.x + radius, c.y, c.z };
            for (int i = 1; i <= kSegments; i++) {
                const float a = 6.2831853f * static_cast<float>(i) / kSegments;
                const Point p{ c.x + radius * std::cos(a), c.y + radius * std::sin(a), c.z };
                Line(dl, proj, prev, p, color, 1.5f);
                prev = p;
            }
        }

        /// @brief The horizontal unit direction of a Skyrim heading (radians clockwise from +y).
        Point Direction(float heading) { return { std::sin(heading), std::cos(heading), 0.0f }; }

        /// @brief A short tick across the ring at `heading`: the body's facing.
        void Tick(ImDrawList* dl, const Projector& proj, const Point& c, float radius, float heading, ImU32 color)
        {
            const Point d = Direction(heading);
            Line(dl, proj, { c.x + d.x * radius * 0.7f, c.y + d.y * radius * 0.7f, c.z },
                 { c.x + d.x * radius * 1.3f, c.y + d.y * radius * 1.3f, c.z }, color, 3.0f);
        }

        /// @brief A small arrowhead on the ring pointing out at `heading`: where the stick asks to go.
        void RingArrow(ImDrawList* dl, const Projector& proj, const Point& c, float radius, float heading, ImU32 color)
        {
            const Point d = Direction(heading);
            const Point n{ -d.y, d.x, 0.0f };
            const Point tip{ c.x + d.x * radius * 1.45f, c.y + d.y * radius * 1.45f, c.z };
            const Point left{ c.x + d.x * radius + n.x * radius * 0.25f, c.y + d.y * radius + n.y * radius * 0.25f, c.z };
            const Point right{ c.x + d.x * radius - n.x * radius * 0.25f, c.y + d.y * radius - n.y * radius * 0.25f, c.z };
            ImVec2 st, sl, sr;
            if (proj(tip, st) && proj(left, sl) && proj(right, sr)) dl->AddTriangleFilled(st, sl, sr, color);
        }

        float HorizontalSpeed(const Point& v) { return std::sqrt(v.x * v.x + v.y * v.y) * kGameUnitsPerHavok; }

        bool Active()
        {
            static const bool on = On();
            if (!on) return false;
            auto* ui = RE::UI::GetSingleton();
            if (!ui || ui->GameIsPaused() || !RE::Main::WorldRootCamera()) return false;
            Store&       s = S();
            const double now = Now();
            std::scoped_lock lock(s.lock);
            return now - s.ctrlAt < kStale || now - s.predAt < kStale;
        }

        void Draw()
        {
            Store&     s = S();
            Controller c;
            Prediction p;
            Ramp       r;
            Matching   m;
            bool       haveCtrl, havePred, haveRamp, haveMatch;
            {
                const double now = Now();
                std::scoped_lock lock(s.lock);
                c = s.ctrl, p = s.pred, r = s.ramp, m = s.match;
                haveCtrl = now - s.ctrlAt < kStale;
                havePred = now - s.predAt < kStale;
                haveRamp = now - s.rampAt < kStale;
                haveMatch = now - s.matchAt < kStale;
            }
            const Projector proj{ RE::Main::WorldRootCamera(), ImGui::GetIO().DisplaySize };
            if (!proj.camera) return;
            ImDrawList* dl = ImGui::GetBackgroundDrawList();

            if (haveCtrl) {
                Ring(dl, proj, c.feet, c.radius, GroundColor(c.verdict, c.support));
                for (const Ray& ray : c.rays) {
                    const ImU32 color = !ray.hit ? kMiss : ray.walkable ? kWalkable : ray.wall ? kWall : kSteep;
                    const Point end   = ray.hit ? Point{ ray.top.x, ray.top.y, ray.hitZ } : ray.bottom;
                    Line(dl, proj, ray.top, end, color, 1.5f);
                    ImVec2 at;
                    if (ray.hit && proj(end, at)) dl->AddCircleFilled(at, 3.0f, color);
                }
                const Point chest{ c.feet.x, c.feet.y, c.feet.z + c.height * 0.5f };
                Arrow(dl, proj, chest, c.desired, kDesired);
                Arrow(dl, proj, chest, c.slide, kSteep);
                Arrow(dl, proj, chest, c.velocity, kVelocity);
            }

            FacingState facing;
            const bool  haveFacing = CurrentFacing(facing);
            if (haveCtrl && haveFacing) {
                Tick(dl, proj, c.feet, c.radius, facing.yaw, kFacingTick);
                if (facing.haveGoal) RingArrow(dl, proj, c.feet, c.radius, facing.goal, kStick);
            }

            if (havePred) {
                Point prev = haveCtrl ? c.feet : p.path[0];
                for (int i = 0; i < p.pathCount && i < Prediction::kPath; i++) {
                    Line(dl, proj, prev, p.path[i], GroundColor(p.pathGround[i], p.pathSupport[i]), 3.0f);
                    prev = p.path[i];
                }
                ImVec2 at;
                for (int k = 0; k < Prediction::kSamples; k++)
                    if (proj(p.at[k], at)) dl->AddCircleFilled(at, 5.0f, GroundColor(p.ground[k], p.support[k]));
                if (p.blocked && proj(p.blockedAt, at)) {
                    dl->AddLine(ImVec2(at.x - 8, at.y - 8), ImVec2(at.x + 8, at.y + 8), kWall, 3.0f);
                    dl->AddLine(ImVec2(at.x - 8, at.y + 8), ImVec2(at.x + 8, at.y - 8), kWall, 3.0f);
                }
            }

            ImGui::SetNextWindowPos(ImVec2(24.0f, 180.0f), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.55f);
            ImGui::Begin("##CBCharacterDebug", nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoFocusOnAppearing);
            ImGui::TextUnformatted("Community Behaviors - character");
            if (haveCtrl) {
                ImGui::Text("mode %s   verdict %s   n.z %.2f   hits %d/%d", c.mode, Name(c.verdict), c.groundNz, c.supportHits, c.walkableHits);
                ImGui::Text("support %.2f   slide %.0f   air %.2f s (%s)", c.support, HorizontalSpeed(c.slide), c.airTime, c.entry);
                ImGui::Text("speed %.0f   desired %.0f   vz %.0f", HorizontalSpeed(c.velocity), HorizontalSpeed(c.desired), c.velocity.z * kGameUnitsPerHavok);
                if (c.variableJump) ImGui::Text("variable jump: %s", c.jumpReleased ? "released" : "held");
                if (c.jumpIn >= 0.0f) ImGui::Text("jump wind-up %.0f ms left", c.jumpIn * 1000.0f);
            }
            if (haveRamp) ImGui::Text("ramp %.0f -> %.0f   turn x%.2f   slope x%.2f", r.speed, r.target, r.turn, r.slope);
            if (haveMatch) ImGui::Text("matching %s @%d   cost %.2f   %.1f ms   %d switches", m.clip, m.frame, m.cost, m.searchMs, m.switches);
            if (haveFacing && facing.haveGoal) ImGui::Text("facing %.0f deg from the stick", std::fabs(WrapPi(facing.goal - facing.yaw)) * kRadToDeg);
            if (havePred) {
                if (p.blocked) ImGui::Text("iState %d   blocked in %.2f s", p.iState, p.blockedIn);
                else           ImGui::Text("iState %d", p.iState);
            }
            ImGui::End();
        }

        [[maybe_unused]] const bool s_registered = [] {
            if (!On()) return false;
            core::bootstrap::RegisterOverlay({ "CharDebugHud", &Active, &Draw });
            return true;
        }();

    }  // namespace

    bool On()
    {
        static const bool on = core::debug::FlagOn("char_debug_hud");
        return on;
    }

    void Publish(const Controller& c)
    {
        Store& s = S();
        std::scoped_lock lock(s.lock);
        s.ctrl   = c;
        s.ctrlAt = Now();
    }

    void Publish(const Prediction& p)
    {
        Store& s = S();
        std::scoped_lock lock(s.lock);
        s.pred   = p;
        s.predAt = Now();
    }

    void Publish(const Ramp& r)
    {
        Store& s = S();
        std::scoped_lock lock(s.lock);
        s.ramp   = r;
        s.rampAt = Now();
    }

    void Publish(const Matching& m)
    {
        Store& s = S();
        std::scoped_lock lock(s.lock);
        s.match   = m;
        s.matchAt = Now();
    }

}  // namespace CB::feature::controller::hud
