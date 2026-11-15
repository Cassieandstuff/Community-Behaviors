#include "PCH.h"

#include "CharCollisionHud.h"
#include "CharGround.h"

#include <core/debug/Settings.h>
#include <core/ui/UiHost.h>
#include <platform/hooks/hookslib.h>
#include <platform/hooks/Override.h>

#include <Common/Base/hkBase.h>
#include <Physics/ConstraintSolver/Simplex/hkpSimplexSolver.h>
#include <BSHavok/bhkCharProxyController.h>

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>

/// @file
/// @brief The collision overlay (CharCollisionHud.h): its store, its publishers' hooks and its drawing.
/// @note The contacts come from an observer on the proxy's constraint callback, after CB's planes (or Bethesda's) ran,
///       so they show the planes the solve was handed; it is subscribed only when char_collision_hud is on at launch.

namespace CB::feature::controller::hud {

    namespace {

        namespace ch = CB::feature::controller;

        constexpr float  kGameUnitsPerHavok = 69.99125f;
        constexpr double kStale             = 0.5;
        constexpr double kSweepAge          = 0.2;    ///< seconds a recorded sweep stays drawn
        constexpr int    kSweeps            = 24;
        constexpr float  kNormalLength      = 0.25f;  ///< Havok units, about 17 game units
        constexpr int    kBandColumns       = 8;

        struct Store {
            std::mutex                      lock;
            Contacts                        contacts;
            Volumes                         volumes;
            std::array<Sweep, kSweeps>      sweeps{};
            std::array<double, kSweeps>     sweepAt{};
            int                             nextSweep = 0;
            double                          contactsAt = -1.0e9, volumesAt = -1.0e9;
        };

        Store& S()
        {
            static Store s;
            return s;
        }

        std::atomic<const void*> g_watched{ nullptr };

        double Now()
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        const ImU32 kBand       = IM_COL32(255, 60, 220, 170);   ///< the band under the floating hull no contact reaches
        const ImU32 kBound      = IM_COL32(255, 230, 90, 200);   ///< m_collisionBound
        const ImU32 kBumper     = IM_COL32(255, 120, 180, 220);  ///< m_bumperCollisionBound
        const ImU32 kWalkable   = IM_COL32(80, 230, 80, 255);
        const ImU32 kSteep      = IM_COL32(255, 160, 40, 255);
        const ImU32 kFlattened  = IM_COL32(240, 60, 60, 255);
        const ImU32 kZeroed     = IM_COL32(150, 150, 150, 200);
        const ImU32 kPlane      = IM_COL32(255, 255, 255, 220);
        const ImU32 kWallPlane  = IM_COL32(240, 60, 60, 255);
        const ImU32 kSweepPath  = IM_COL32(120, 200, 255, 160);
        const ImU32 kSweepHit   = IM_COL32(255, 140, 0, 255);
        const ImU32 kSweepMiss  = IM_COL32(120, 200, 255, 110);

        Point Add(const Point& p, const Point& d, float s) { return { p.x + d.x * s, p.y + d.y * s, p.z + d.z * s }; }
        Point Lerp(const Point& a, const Point& b, float t) { return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t }; }

        std::atomic<bool>& OnFlag()
        {
            static std::atomic<bool> on{ core::debug::FlagOn("char_collision_hud") };
            return on;
        }

        /// @brief A BSBound box in the actor's frame (its heading turns it about z), drawn round the feet.
        /// @note BSBound's centre and extents are game units; the box's corners are centre ± extents.
        void DrawBound(ImDrawList* dl, const Volumes& v, const Point& center, const Point& extents, ImU32 color)
        {
            if (extents.x <= 0.0f && extents.y <= 0.0f && extents.z <= 0.0f) return;
            const float k = v.scale / kGameUnitsPerHavok;
            const float s = std::sin(v.yaw), c = std::cos(v.yaw);
            const auto  world = [&](float lx, float ly, float lz) {
                /// @note Skyrim's heading turns clockwise from +y: local +y is (sin, cos), local +x is (cos, -sin).
                return Point{ v.feet.x + (lx * c + ly * s) * k, v.feet.y + (-lx * s + ly * c) * k, v.feet.z + lz * k };
            };
            std::array<Point, 8> p{};
            for (int i = 0; i < 8; i++)
                p[i] = world(center.x + (i & 1 ? extents.x : -extents.x), center.y + (i & 2 ? extents.y : -extents.y),
                             center.z + (i & 4 ? extents.z : -extents.z));
            for (int i = 0; i < 8; i++)
                for (int bit = 1; bit < 8; bit <<= 1)
                    if (!(i & bit)) draw::Line(dl, p[i], p[i | bit], color, 1.5f);
        }

        /// @brief The band between the feet and the floating hull's bottom: a ring at each end and columns between.
        void DrawBand(ImDrawList* dl, const Volumes& v)
        {
            if (v.lift <= 0.0f) return;
            const Point top{ v.feet.x, v.feet.y, v.feet.z + v.lift };
            draw::Ring(dl, v.feet, v.radius, kBand);
            draw::Ring(dl, top, v.radius, kBand);
            for (int i = 0; i < kBandColumns; i++) {
                const float a = 6.2831853f * static_cast<float>(i) / kBandColumns;
                const Point lo{ v.feet.x + v.radius * std::cos(a), v.feet.y + v.radius * std::sin(a), v.feet.z };
                draw::Line(dl, lo, { lo.x, lo.y, top.z }, kBand, 1.0f);
            }
        }

        void DrawContacts(ImDrawList* dl, const Contacts& c, const Volumes* v)
        {
            for (int i = 0; i < c.count; i++) {
                const Point& at = c.at[i];
                ImU32 color = c.kind[i] == Contacts::Kind::Zeroed ? kZeroed : c.kind[i] == Contacts::Kind::Flattened ? kFlattened
                            : c.normal[i].z >= 0.682f                                                         ? kWalkable
                                                                                                                : kSteep;
                ImVec2 s;
                if (draw::ToScreen(at, s)) dl->AddCircleFilled(s, c.distance[i] < 0.0f ? 5.0f : 3.5f, color);
                draw::Line(dl, at, Add(at, c.normal[i], kNormalLength), color, 2.0f);
                /// @note The plane the solve got, when the callback changed it (flattened, zeroed planes are not drawn).
                const Point& p = c.plane[i];
                const float  dx = p.x - c.normal[i].x, dy = p.y - c.normal[i].y, dz = p.z - c.normal[i].z;
                if (c.kind[i] != Contacts::Kind::Zeroed && dx * dx + dy * dy + dz * dz > 1.0e-4f)
                    draw::Line(dl, at, Add(at, p, kNormalLength * 0.8f), kPlane, 1.5f);
            }
            if (v && c.walls > 0) {
                const Point chest{ v->feet.x, v->feet.y, v->feet.z + v->lift + v->radius };
                for (int w = 0; w < c.walls; w++) draw::Arrow(dl, chest, Point{ c.wall[w].x * 0.5f, c.wall[w].y * 0.5f, 0.0f }, kWallPlane);
            }
        }

        void DrawSweeps(ImDrawList* dl, const Store& s, double now, const Volumes* v, int& shown, int& hits)
        {
            for (int i = 0; i < kSweeps; i++) {
                if (now - s.sweepAt[i] > kSweepAge) continue;
                const Sweep& w    = s.sweeps[i];
                const Point  stop = Lerp(w.from, w.to, w.fraction);
                ++shown;
                draw::Line(dl, w.from, w.hit ? stop : w.to, kSweepPath, 1.0f);
                if (v) {
                    const Point shift{ stop.x - v->origin.x, stop.y - v->origin.y, stop.z - v->origin.z };
                    for (int k = 0; k < v->capsuleCount; k++) {
                        hud::Capsule c = v->capsules[k];
                        c.a            = Add(c.a, shift, 1.0f);
                        c.b            = Add(c.b, shift, 1.0f);
                        draw::Capsule(dl, c, w.hit ? kSweepHit : kSweepMiss);
                    }
                }
                if (w.hit) {
                    ++hits;
                    ImVec2 at;
                    if (draw::ToScreen(w.at, at)) dl->AddCircleFilled(at, 4.0f, kSweepHit);
                    draw::Line(dl, w.at, Add(w.at, w.normal, kNormalLength), kSweepHit, 2.0f);
                }
            }
        }

        bool Active()
        {
            if (!CollisionOn()) return false;
            auto* ui = RE::UI::GetSingleton();
            if (!ui || ui->GameIsPaused() || !RE::Main::WorldRootCamera()) return false;
            Store&       s   = S();
            const double now = Now();
            std::scoped_lock lock(s.lock);
            return now - s.volumesAt < kStale || now - s.contactsAt < kStale;
        }

        void Draw()
        {
            Store&       s   = S();
            const double now = Now();
            std::scoped_lock lock(s.lock);
            const bool haveVolumes  = now - s.volumesAt < kStale;
            const bool haveContacts = now - s.contactsAt < kStale;
            ImDrawList* dl = ImGui::GetBackgroundDrawList();
            const Volumes* v = haveVolumes ? &s.volumes : nullptr;

            if (v) {
                DrawBand(dl, *v);
                DrawBound(dl, *v, v->boundCenter, v->boundExtents, kBound);
                DrawBound(dl, *v, v->bumperCenter, v->bumperExtents, kBumper);
            }
            if (haveContacts) DrawContacts(dl, s.contacts, v);
            int shown = 0, hits = 0;
            DrawSweeps(dl, s, now, v, shown, hits);

            const ImVec2 size = ImGui::GetIO().DisplaySize;
            ImGui::SetNextWindowPos(ImVec2(size.x - 24.0f, 180.0f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
            ImGui::SetNextWindowBgAlpha(0.55f);
            ImGui::Begin("##CBCharacterCollision", nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoFocusOnAppearing);
            ImGui::TextUnformatted("Community Behaviors - collision");
            if (v) {
                if (v->lift > 0.0f) ImGui::Text("hull floats %.0f: no contact below it (magenta band)", v->lift * kGameUnitsPerHavok);
                else                ImGui::TextUnformatted("hull on the feet (no float)");
                ImGui::Text("bound    c(%.0f %.0f %.0f) e(%.0f %.0f %.0f)", v->boundCenter.x, v->boundCenter.y, v->boundCenter.z, v->boundExtents.x,
                            v->boundExtents.y, v->boundExtents.z);
                ImGui::Text("bumper   c(%.0f %.0f %.0f) e(%.0f %.0f %.0f)", v->bumperCenter.x, v->bumperCenter.y, v->bumperCenter.z,
                            v->bumperExtents.x, v->bumperExtents.y, v->bumperExtents.z);
            }
            if (haveContacts) {
                const Contacts& c = s.contacts;
                int zeroed = 0, flattened = 0, penetrating = 0;
                for (int i = 0; i < c.count; i++) {
                    zeroed += c.kind[i] == Contacts::Kind::Zeroed;
                    flattened += c.kind[i] == Contacts::Kind::Flattened;
                    penetrating += c.distance[i] < 0.0f;
                }
                ImGui::Text("contacts %d (drawn %d)   zeroed %d   flattened %d   penetrating %d   walls added %d", c.total, c.count, zeroed,
                            flattened, penetrating, c.walls);
                if (c.total > 0) ImGui::Text("lowest contact %+.0f above the feet", c.lowest * kGameUnitsPerHavok);
            } else {
                ImGui::TextUnformatted("contacts: none this step");
            }
            ImGui::Text("sweeps in the last %.1f s: %d   hits %d", kSweepAge, shown, hits);
            ImGui::End();
        }

        [[maybe_unused]] const bool s_registered = [] {
            core::ui::RegisterOverlay({ "CharCollisionHud", &Active, &Draw, 1 });
            return true;
        }();

        using Process = platform::hooks::Override<&::bhkCharProxyController::processConstraintsCallback>;

        /// @brief Copies the controlled character's manifold and the planes the callbacks before it left.
        /// @note Runs under the world's write lock (integrate): reads only, no world query.
        void OnProcess(Process::Next next, ::bhkCharProxyController* self, const ::hkpCharacterProxy* proxy,
                       const ::hkArray<::hkpRootCdPoint>& manifold, ::hkpSimplexSolverInput& input)
        {
            next(self, proxy, manifold, input);
            if (!CollisionOn() || !self) return;
            ::bhkCharacterController& ctrl = *self;
            if (!ch::IsControlled(&ctrl) || !ch::IsProxy(ctrl)) return;
            const ch::Hull hull  = ch::GetHull(ctrl);
            const int      total = manifold.getSize();
            Contacts       c;
            c.total = total;
            c.count = (std::min)(total, Contacts::kPoints);
            float lowest = 1.0e9f;
            for (int i = 0; i < total; i++) {
                const ::hkContactPoint& cp  = manifold[i].m_contact;
                const ::hkVector4&      pos = cp.getPosition();
                lowest                      = (std::min)(lowest, pos(2) - hull.feetZ);
                if (i >= c.count) continue;
                const ::hkVector4& n = cp.getSeparatingNormal();
                c.at[i]       = { pos(0), pos(1), pos(2) };
                c.normal[i]   = { n(0), n(1), n(2) };
                c.distance[i] = n(3);
                c.layer[i]    = static_cast<std::uint8_t>(manifold[i].m_rootCollidableB->getCollisionFilterInfo() & 0x7f);
                if (i < input.m_numConstraints) {
                    const ::hkVector4& p = input.m_constraints[i].m_plane;
                    c.plane[i]           = { p(0), p(1), p(2) };
                    const bool zero      = p(0) == 0.0f && p(1) == 0.0f && p(2) == 0.0f;
                    c.kind[i] = zero ? Contacts::Kind::Zeroed : (p(2) == 0.0f && std::fabs(n(2)) > 1.0e-3f) ? Contacts::Kind::Flattened : Contacts::Kind::Block;
                }
            }
            c.lowest = total > 0 && hull.valid ? lowest : 0.0f;
            for (int w = total; w < input.m_numConstraints && c.walls < Contacts::kWalls; w++) {
                const ::hkVector4& p = input.m_constraints[w].m_plane;
                c.wall[c.walls++]    = { p(0), p(1), p(2) };
            }
            Publish(c);
        }

        [[maybe_unused]] const bool s_process =
            Process::Subscribe(&OnProcess, platform::hooks::Priority::Normal, "CharCollisionHud", "char_collision_hud");

    }  // namespace

    bool CollisionOn() { return On() && OnFlag().load(std::memory_order_relaxed); }

    void SetCollisionOn(bool on)
    {
        if (OnFlag().exchange(on, std::memory_order_relaxed) == on) return;
        core::debug::SetValue("Character", "bCollisionHud", on ? "true" : "false");
        LOG_INFO("config: the collision overlay is {}", on ? "on" : "off");
    }

    bool CollisionFlag() { return OnFlag().load(std::memory_order_relaxed); }

    void Publish(const Contacts& c)
    {
        Store& s = S();
        std::scoped_lock lock(s.lock);
        s.contacts   = c;
        s.contactsAt = Now();
    }

    void Publish(const Volumes& v)
    {
        g_watched.store(v.collidable, std::memory_order_relaxed);
        Store& s = S();
        std::scoped_lock lock(s.lock);
        s.volumes   = v;
        s.volumesAt = Now();
    }

    void RecordSweep(const void* collidable, const Sweep& w)
    {
        if (!collidable || collidable != g_watched.load(std::memory_order_relaxed)) return;
        Store& s = S();
        std::scoped_lock lock(s.lock);
        s.sweeps[s.nextSweep]  = w;
        s.sweepAt[s.nextSweep] = Now();
        s.nextSweep            = (s.nextSweep + 1) % kSweeps;
    }

}  // namespace CB::feature::controller::hud
