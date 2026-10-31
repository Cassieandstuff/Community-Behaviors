#include "PCH.h"

#include "CharDebugHud.h"
#include "CharFacing.h"
#include "CharSprint.h"

#include <core/bootstrap/ProgressHud.h>
#include <core/debug/Settings.h>

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <string>

/// @file
/// @brief The character debug HUD (char_debug_hud): the controlled character's (IsControlled: the player, or the
///        horse it rides) collision shape, floor rays, pogo spring, velocity arrows, predicted trajectory
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
            Movement   move;
            Matching   match;
            double     ctrlAt = -1.0e9, predAt = -1.0e9, moveAt = -1.0e9, matchAt = -1.0e9;
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
        const ImU32 kPast       = IM_COL32(210, 30, 30, 255);   ///< where the player was (the query's past)
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

        const ImU32 kShape    = IM_COL32(0, 210, 210, 200);
        const ImU32 kPogoPush = IM_COL32(50, 205, 50, 255);    ///< Box3D's lime green: the spring pushes the feet up
        const ImU32 kPogoPull = IM_COL32(0, 191, 255, 255);    ///< Box3D's deep sky blue: it pulls them down
        const ImU32 kPogoIdle = IM_COL32(211, 211, 211, 255);
        const ImU32 kPogoRest = IM_COL32(255, 255, 255, 255);
        /// @brief Below this rate (Havok units/s, about 1 game unit/s) the spring reads as at rest.
        constexpr float kPogoIdleRate = 0.015f;

        Point Add(const Point& p, const Point& d, float s) { return { p.x + d.x * s, p.y + d.y * s, p.z + d.z * s }; }

        /// @brief A circle of `radius` round `c` in the plane of the unit vectors `u`, `v`, from angle `from` to `to`.
        void Arc(ImDrawList* dl, const Projector& proj, const Point& c, const Point& u, const Point& v, float radius, float from, float to,
                 int segments, ImU32 color)
        {
            Point prev = Add(Add(c, u, radius * std::cos(from)), v, radius * std::sin(from));
            for (int i = 1; i <= segments; i++) {
                const float a = from + (to - from) * static_cast<float>(i) / segments;
                const Point p = Add(Add(c, u, radius * std::cos(a)), v, radius * std::sin(a));
                Line(dl, proj, prev, p, color, 1.5f);
                prev = p;
            }
        }

        /// @brief A capsule as a wireframe: a ring at each end of its segment, four side lines, and two half circles over
        ///        each end's cap.
        void DrawCapsule(ImDrawList* dl, const Projector& proj, const Capsule& c, ImU32 color)
        {
            constexpr float kPi = 3.14159265f;
            Point       axis{ c.b.x - c.a.x, c.b.y - c.a.y, c.b.z - c.a.z };
            const float len = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
            axis            = len > 1.0e-5f ? Point{ axis.x / len, axis.y / len, axis.z / len } : Point{ 0.0f, 0.0f, 1.0f };
            const Point helper = std::fabs(axis.z) < 0.9f ? Point{ 0.0f, 0.0f, 1.0f } : Point{ 1.0f, 0.0f, 0.0f };
            Point       u{ axis.y * helper.z - axis.z * helper.y, axis.z * helper.x - axis.x * helper.z, axis.x * helper.y - axis.y * helper.x };
            const float ul = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
            u              = { u.x / ul, u.y / ul, u.z / ul };
            const Point v{ axis.y * u.z - axis.z * u.y, axis.z * u.x - axis.x * u.z, axis.x * u.y - axis.y * u.x };
            Arc(dl, proj, c.a, u, v, c.radius, 0.0f, 2.0f * kPi, 20, color);
            Arc(dl, proj, c.b, u, v, c.radius, 0.0f, 2.0f * kPi, 20, color);
            for (const Point& side : { u, v, Point{ -u.x, -u.y, -u.z }, Point{ -v.x, -v.y, -v.z } })
                Line(dl, proj, Add(c.a, side, c.radius), Add(c.b, side, c.radius), color, 1.5f);
            const Point down{ -axis.x, -axis.y, -axis.z };
            Arc(dl, proj, c.b, u, axis, c.radius, 0.0f, kPi, 10, color);
            Arc(dl, proj, c.b, v, axis, c.radius, 0.0f, kPi, 10, color);
            Arc(dl, proj, c.a, u, down, c.radius, 0.0f, kPi, 10, color);
            Arc(dl, proj, c.a, v, down, c.radius, 0.0f, kPi, 10, color);
        }

        void DrawBox(ImDrawList* dl, const Projector& proj, const Point& lo, const Point& hi, ImU32 color)
        {
            auto corner = [&](int i) { return Point{ i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z }; };
            for (int i = 0; i < 8; i++)
                for (int bit = 1; bit < 8; bit <<= 1)
                    if (!(i & bit)) Line(dl, proj, corner(i), corner(i | bit), color, 1.5f);
        }

        /// @brief The pogo as Box3D draws its joint: a zigzag spring from the floating shape's bottom down to the floor it
        ///        holds the feet to, coloured by what it does, and a white cross at its rest point (the feet).
        void DrawPogo(ImDrawList* dl, const Projector& proj, const Controller& c)
        {
            const Ray&  ray   = c.rays[c.pogoRay];
            const Point top{ ray.top.x, ray.top.y, c.feet.z + c.lift };
            const Point floor{ ray.top.x, ray.top.y, ray.hitZ };
            const Point rest{ ray.top.x, ray.top.y, c.feet.z };
            const ImU32 color = c.pogoRate > kPogoIdleRate ? kPogoPush : c.pogoRate < -kPogoIdleRate ? kPogoPull : kPogoIdle;
            ImVec2      st, sf;
            if (!proj(top, st) || !proj(floor, sf)) return;
            const float dx = sf.x - st.x, dy = sf.y - st.y, len = std::sqrt(dx * dx + dy * dy);
            if (len > 1.0f) {
                constexpr int   kSegments = 12;
                constexpr float kWidth    = 6.0f;
                const float     nx = -dy / len, ny = dx / len;
                ImVec2          prev = st;
                for (int i = 1; i <= kSegments; i++) {
                    const float  side = i == kSegments ? 0.0f : (i & 1) ? kWidth : -kWidth;
                    const float  f    = static_cast<float>(i) / kSegments;
                    const ImVec2 p(st.x + dx * f + nx * side, st.y + dy * f + ny * side);
                    dl->AddLine(prev, p, color, 2.0f);
                    prev = p;
                }
            }
            dl->AddCircleFilled(st, 4.0f, color);
            dl->AddCircleFilled(sf, 4.0f, color);
            ImVec2 sr;
            if (proj(rest, sr)) {
                dl->AddLine(ImVec2(sr.x - 7, sr.y), ImVec2(sr.x + 7, sr.y), kPogoRest, 2.0f);
                dl->AddLine(ImVec2(sr.x, sr.y - 7), ImVec2(sr.x, sr.y + 7), kPogoRest, 2.0f);
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
            Movement   mv;
            Matching   m;
            bool       haveCtrl, havePred, haveMove, haveMatch;
            {
                const double now = Now();
                std::scoped_lock lock(s.lock);
                c = s.ctrl, p = s.pred, mv = s.move, m = s.match;
                haveCtrl = now - s.ctrlAt < kStale;
                havePred = now - s.predAt < kStale;
                haveMove = now - s.moveAt < kStale;
                haveMatch = now - s.matchAt < kStale;
            }
            const Projector proj{ RE::Main::WorldRootCamera(), ImGui::GetIO().DisplaySize };
            if (!proj.camera) return;
            ImDrawList* dl = ImGui::GetBackgroundDrawList();

            if (haveCtrl) {
                for (int i = 0; i < c.capsuleCount; i++) DrawCapsule(dl, proj, c.capsules[i], kShape);
                if (c.otherShape) DrawBox(dl, proj, c.boxMin, c.boxMax, kShape);
                if (c.pogo) DrawPogo(dl, proj, c);
                Ring(dl, proj, c.feet, c.radius, GroundColor(c.verdict, c.support));
                for (int e = 0; e < c.endCount; e++) Ring(dl, proj, c.ends[e], c.radius, GroundColor(c.verdict, c.support));
                for (int i = 0; i < c.rayCount; i++) {
                    const Ray& ray = c.rays[i];
                    if (!ray.cast) continue;
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

            if (haveCtrl && haveMove) {
                Tick(dl, proj, c.feet, c.radius, mv.yaw, kFacingTick);
                if (mv.haveStick) RingArrow(dl, proj, c.feet, c.radius, mv.stickYaw, kStick);
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
                Point back = haveCtrl ? c.feet : p.history[0];
                for (int i = 0; i < p.historyCount && i < Prediction::kHistory; i++) {
                    Line(dl, proj, back, p.history[i], kPast, 3.0f);
                    back = p.history[i];
                }
                for (int k = 0; k < p.pastCount && k < Prediction::kSamples; k++)
                    if (proj(p.pastAt[k], at)) dl->AddCircleFilled(at, 5.0f, kPast);
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
                if (c.lift > 0.0f) {
                    if (c.pogo)
                        ImGui::Text("pogo: hull %.0f up   floor %+.0f   rate %+.0f (%s)", c.lift * kGameUnitsPerHavok,
                                    (c.rays[c.pogoRay].hitZ - c.feet.z) * kGameUnitsPerHavok, c.pogoRate * kGameUnitsPerHavok,
                                    c.pogoRate > kPogoIdleRate ? "push" : c.pogoRate < -kPogoIdleRate ? "pull" : "rest");
                    else ImGui::Text("pogo: hull %.0f up   not holding", c.lift * kGameUnitsPerHavok);
                }
            }
            if (haveMove) {
                ImGui::Text("%s   speed %.0f / cap %.0f   engine %.0f   slope x%.2f", mv.strafe ? "strafe" : "orient", mv.speed, mv.cap, mv.full,
                            mv.slope);
                if (mv.haveStick) ImGui::Text("facing %.0f deg from the stick", std::fabs(WrapPi(mv.stickYaw - mv.yaw)) * kRadToDeg);
            }
            if (sprint::State st; sprint::Current(st))
                ImGui::Text("sprint %s%s%s%s", st.sprinting ? "on" : "off", st.wanted ? "   wanted" : "", st.suspended ? "   suspended" : "",
                            *st.why ? (std::string("   (") + st.why + ")").c_str() : "");
            if (haveMatch) {
                ImGui::Text("matching %s @%d [%s]   x%.2f   cost %.2f   %.1f ms   %d switches   blend %.2f s", m.clip, m.frame, m.database,
                            m.rate, m.cost, m.searchMs, m.switches, m.blend);
                ImGui::Text("chooser %s   searching %s", m.chooser, m.active);
            }
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

    void Publish(const Movement& m)
    {
        Store& s = S();
        std::scoped_lock lock(s.lock);
        s.move   = m;
        s.moveAt = Now();
    }

    void Publish(const Matching& m)
    {
        Store& s = S();
        std::scoped_lock lock(s.lock);
        s.match   = m;
        s.matchAt = Now();
    }

}  // namespace CB::feature::controller::hud
