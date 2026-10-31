#include "PCH.h"

#include "CharGround.h"
#include "CharSweep.h"

#include <platform/hooks/Override.h>

#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharProxyController.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <mutex>
#include <string>

/// @file
/// @brief Step 5 of the character controller spec (M7): the step query, modelled on Jolt's CharacterVirtual::WalkStairs.
///        When a grounded player's slide falls short of the requested horizontal move against a steep obstacle, the
///        hull is swept up by the step height, forward by the blocked remainder and down again; a landing the floor
///        query calls Supported is taken, by moving the character there. Otherwise the slid position stands.
/// @note Wraps bhkCharProxyController::Unk_0B, the per-step integrate driver (hkpCharacterProxy::integrate, the slide),
///       so it runs after the state's Simulate set m_outVelocity and before Unk_0C reports the move: the snap is part
///       of this step's move.
/// @note The landing's surface material is not checked (Bethesda's stairs / slope-blocking materials); only geometry and
///       the floor query's walkable test decide.

namespace CB::feature {

    namespace {

        namespace hk = CB::havok;
        namespace ch = CB::feature::controller;
        using Integrate   = platform::hooks::Override<&hk::bhkCharProxyController::Unk_0B>;

        constexpr float         kHavokToGame       = 69.99125f;
        constexpr float         kMinRequested      = 0.002f;   ///< Havok units this step; below it nothing is blocked
        constexpr float         kShortfallFraction = 0.1f;     ///< of the requested move, the least that counts as blocked
        constexpr float         kMinShortfall      = 0.001f;
        constexpr float         kMinForward        = 0.05f;    ///< the forward leg's least length (spec M7)
        constexpr float         kMinUp             = 0.02f;    ///< less headroom than this is a ceiling
        constexpr float         kMinForwardGain    = 0.005f;   ///< less than this on the forward leg: blocked above the step
        constexpr float         kDownExtra         = 0.05f;    ///< the down leg goes this far below the starting height
        constexpr float         kSkin              = 0.002f;   ///< kept back from every hit, so the next cast starts clear
        constexpr float         kFootTolerance     = 0.1f;     ///< landings lower than this are not steps up
        constexpr std::uint64_t kCharacters        = 1ull << ch::kCharControllerLayer;
        constexpr const char*   kNotGrounded       = "landing not supported";

        struct Attempt {
            const char* reason   = nullptr;   ///< null: accepted
            float       requested = 0, achieved = 0, up = 0, forward = 0, rise = 0;
            float       obstacleNz = 0, landingNz = 0;
            float       landingSupport = 0;   ///< the landing verdict's support, 0 with no ground
        };

        std::mutex    g_lock;
        std::uint64_t g_attempts = 0, g_accepted = 0;
        const char*   g_lastReason = nullptr;
        std::uint32_t g_sameReason = 0, g_logged = 0;
        constexpr std::uint32_t kMaxLogged = 3000;

        /// @brief Rejections repeat every step while the player walks into a wall: a reason is logged when it changes and
        ///        then every 64th time; acceptances always (up to the cap).
        void Log(const Attempt& a)
        {
            std::scoped_lock lock(g_lock);
            ++g_attempts;
            if (!a.reason) ++g_accepted;
            if ((g_attempts & (g_attempts - 1)) == 0)
                LOG_INFO("step query: {} attempts, {} accepted", g_attempts, g_accepted);
            if (a.reason) {
                g_sameReason = a.reason == g_lastReason ? g_sameReason + 1 : 0;
                g_lastReason = a.reason;
                if (g_sameReason % 64 != 0) return;
            } else g_lastReason = nullptr;
            if (g_logged >= kMaxLogged) return;
            ++g_logged;
            if (!a.reason)
                LOG_INFO("step query #{}: ACCEPTED rise {:.3f} ({:.1f} game units) forward {:.3f}; requested {:.4f} slid {:.4f}; "
                         "obstacle nz {:.2f} landing nz {:.2f}",
                         g_logged, a.rise, a.rise * kHavokToGame, a.forward, a.requested, a.achieved, a.obstacleNz, a.landingNz);
            else
                LOG_INFO("step query #{}: rejected ({}{}) up {:.3f} forward {:.3f} rise {:.3f} ({:.1f} game units); requested {:.4f} "
                         "slid {:.4f}; obstacle nz {:.2f}{}",
                         g_logged, a.reason, a.reason == kNotGrounded ? std::format(": support {:.2f}", a.landingSupport) : std::string(),
                         a.up, a.forward, a.rise, a.rise * kHavokToGame, a.requested, a.achieved, a.obstacleNz,
                         g_sameReason ? " (repeating)" : "");
        }

        hk::hkVector4 Offset(const hk::hkVector4& p, float dx, float dy, float dz) { return hk::hkVector4(p(0) + dx, p(1) + dy, p(2) + dz, p(3)); }

        /// @brief The up / forward / down sweeps from the slid position (`after`, the controller's position) along the
        ///        horizontal unit direction (ux, uy).
        /// @pre The slide fell short by `remainder` Havok units; `ctrl` is managed.
        void TryStep(hk::bhkCharacterController* self, const hk::hkVector4& before, const hk::hkVector4& after, float ux, float uy,
                     float requested, float achieved)
        {
            hk::bhkCharacterController& ctrl = *self;
            const ch::Hull hull = ch::GetHull(ctrl);
            if (!hull.valid) return;
            const float remainder = requested - achieved;

            /// @note The floor query runs at the pre-slide spot (the hull shifted back by the slide): the slide moves only
            ///       the phantom, which the query's rays ignore (character layer), so this is the floor before the slide.
            if (!ch::Supported(ch::QueryFloorAt(ctrl, hull, before(0) - after(0), before(1) - after(1), before(2) - after(2))))
                return;

            const hk::hkVector4& at = hull.collidable->getTransform().getTranslation();
            const ch::SweepHit   obstacle =
                ch::Sweep(hull.world, hull.collidable, Offset(at, ux * remainder, uy * remainder, 0.0f), 0.0f, ch::WalkableZ(ctrl), kCharacters);
            if (!obstacle.hit) return;

            Attempt a;
            a.requested  = requested;
            a.achieved   = achieved;
            a.obstacleNz = obstacle.contact.m_separatingNormal(2);
            const float stepHeight = ctrl.m_stepHeight;
            if (!(stepHeight > kMinUp)) {
                a.reason = "no step height";
                return Log(a);
            }

            const ch::SweepHit up = ch::Sweep(hull.world, hull.collidable, Offset(at, 0.0f, 0.0f, stepHeight), 0.0f);
            a.up = up.hit ? (std::max)(stepHeight * up.fraction - kSkin, 0.0f) : stepHeight;
            if (a.up < kMinUp) {
                a.reason = "ceiling";
                return Log(a);
            }

            const float          forward = (std::max)(remainder, kMinForward);
            const hk::hkVector4  raised  = Offset(at, 0.0f, 0.0f, a.up);
            const ch::SweepHit   ahead   = ch::SweepFrom(hull.world, hull.collidable, raised, Offset(raised, ux * forward, uy * forward, 0.0f), 0.0f);
            a.forward = ahead.hit ? (std::max)(forward * ahead.fraction - kSkin, 0.0f) : forward;
            if (a.forward < kMinForwardGain) {
                a.reason = "blocked above step";
                return Log(a);
            }

            const float          drop    = a.up + kDownExtra;
            const hk::hkVector4  over    = Offset(raised, ux * a.forward, uy * a.forward, 0.0f);
            const ch::SweepHit   down    = ch::SweepFrom(hull.world, hull.collidable, over, Offset(over, 0.0f, 0.0f, -drop), 0.0f, 2.0f, kCharacters);
            if (!down.hit) {
                a.reason = "no floor beyond";
                return Log(a);
            }
            a.landingNz = down.contact.m_separatingNormal(2);
            if (down.fraction <= 0.0f) {
                a.reason = "landing blocked";
                return Log(a);
            }
            a.rise = a.up - (std::max)(drop * down.fraction - kSkin, 0.0f);
            if (a.rise < -kFootTolerance) {
                a.reason = "lands lower";
                return Log(a);
            }

            const float dx = ux * a.forward, dy = uy * a.forward;
            const ch::FloorVerdict landing = ch::QueryFloorAt(ctrl, hull, dx, dy, a.rise);
            a.landingSupport               = landing.ground == ch::Ground::Grounded ? landing.support : 0.0f;
            if (!ch::Supported(landing)) {
                a.reason = kNotGrounded;
                return Log(a);
            }

            /// @note SetPositionImpl(pos, false, false) is the inverse of GetPositionImpl(pos, false): both are the proxy's
            ///       own position, so the sweep's offset applies as is. It takes the world write lock and clears the proxy's
            ///       manifold and old displacement (bhkCharacterProxy::SetPosition), as a teleport does.
            self->SetPositionImpl(Offset(after, dx, dy, a.rise), false, false);
            Log(a);
        }

        /// @note A floating shape (char_pogo) clears what the step query would step over, and the pogo carries the feet
        ///       up: the query stands down.
        bool OnIntegrate(Integrate::Next next, hk::bhkCharacterController* self)
        {
            if (!self || !ch::IsManaged(*self) || (self->m_flags & ch::kJumping) || ch::Lift(*self) > 0.0f) return next(self);
            const float dt = self->m_stepInfo.m_deltaTime;
            const float rx = self->m_outVelocity(0) * dt, ry = self->m_outVelocity(1) * dt;
            const float requested = std::sqrt(rx * rx + ry * ry);
            hk::hkVector4 before;
            self->GetPositionImpl(before, false);
            const bool result = next(self);
            if (!(requested >= kMinRequested)) return result;

            hk::hkVector4 after;
            self->GetPositionImpl(after, false);
            const float ux = rx / requested, uy = ry / requested;
            const float achieved = (after(0) - before(0)) * ux + (after(1) - before(1)) * uy;
            if (requested - achieved < (std::max)(kMinShortfall, kShortfallFraction * requested)) return result;
            TryStep(self, before, after, ux, uy, requested, achieved);
            return result;
        }

        [[maybe_unused]] const bool s_subscribed =
            Integrate::Subscribe(&OnIntegrate, platform::hooks::Priority::Normal, "CharStepQuery", "char_step_query");

    }  // namespace

}  // namespace CB::feature
