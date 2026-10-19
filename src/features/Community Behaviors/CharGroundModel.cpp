#include "PCH.h"

#include <core/debug/Settings.h>
#include <core/hooks/takeover/Override.h>
#include <feature-sdk/havok/Physics/Utilities/CharacterControl/Bethesda/bhkCharProxyController.h>

#include <Physics/ConstraintSolver/Simplex/hkpSimplexSolver.h>
#include <Physics/Dynamics/Entity/hkpRigidBody.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>

/// @file
/// @brief Jolt CharacterVirtual's ground model on Skyrim's proxy controller, as two palette overrides: support comes
///        only from a real supporting contact, and OnGround's unsupported down-cast runs only as Jolt's StickToFloor
///        (right after walking off support).
/// @note The supported bit (kSupport, bit 8) is UpdatePhysics' `(m_surfaceInfo == SUPPORTED && !m_onlyWallContacts) ||
///       kHasPotentialSupportManifold (bit 9)`, computed right after CheckSupportImpl. Bit 9 and m_onlyWallContacts
///       come from processConstraintsCallback, m_surfaceInfo from the proxy's solver-based checkSupport (which already
///       tells SUPPORTED from SLIDING the way Jolt's UpdateSupportingContact does). These overrides change those inputs
///       and the wanted state; UpdatePhysics and OnGround::Simulate run as they are.

namespace CB::feature {

    namespace {

        namespace hk = CB::schema;
        using Process      = havok::bhkCharProxyController::processConstraintsCallback;
        using CheckSupport = core::hooks::Override<&hk::bhkCharProxyController::CheckSupportImpl>;
        using GetVelocity  = havok::bhkCharProxyController::GetLinearVelocityImpl;

        constexpr hk::hkUint32 kSupport                     = 1u << 8;
        constexpr hk::hkUint32 kHasPotentialSupportManifold = 1u << 9;
        constexpr hk::hkUint32 kJumping                     = 1u << 13;
        constexpr unsigned     kCharControllerLayer         = 30;

        bool IsPlayer(const hk::bhkCharacterController* ctrl)
        {
            auto*       player = RE::PlayerCharacter::GetSingleton();
            const auto* own    = player ? player->GetCharController() : nullptr;
            return own && static_cast<const void*>(ctrl) == static_cast<const void*>(own);
        }
        /// @brief Whether Bethesda's callback treated point `i` as support: a contact at or above m_zNormal, or a steep
        ///        one whose step it accepted (it rewrote the plane upward).
        /// @note Layers 4/5/6/10 keep Bethesda's verdict: on a dynamic body there it forces the normal up (standing on
        ///       clutter), which the plane does not show.
        bool IsRealSupport(const hk::bhkCharacterController& ctrl, const hk::hkRootCdPoint& point, const hk::hkVector4& before,
                           const hk::hkVector4& after)
        {
            if (!hk::hkpGetRigidBody(point.m_rootCollidableB)) return false;
            const unsigned layer = point.m_rootCollidableB->getCollisionFilterInfo() & 0x7f;
            if (layer == kCharControllerLayer) return false;
            if (layer == 4 || layer == 5 || layer == 6 || layer == 10) return true;
            const float nz = point.m_contact.m_separatingNormal(2);
            if (nz < 0.0f) return false;
            if (nz >= ctrl.m_zNormal) return true;
            const bool changed = before(0) != after(0) || before(1) != after(1) || before(2) != after(2) || before(3) != after(3);
            const bool zeroed  = after(0) == 0.0f && after(1) == 0.0f && after(2) == 0.0f;
            return changed && !zeroed;
        }

        /// @brief Jolt: only a supporting contact supports. Bethesda also sets bit 9 for a contact whose step it
        ///        REJECTED (n.z >= 0.2, or any when it adds no wall), so a character hanging on a lip it cannot step
        ///        onto counts as standing; this clears bit 9 when no real support set it.
        /// @note Bit 9 accumulates over a step's callbacks (UpdatePhysics clears it before CheckSupportImpl); one set
        ///       before this call was already judged here, so it stands.
        void OnProcess(Process::Next next, hk::bhkCharProxyController* self, const hk::hkpCharacterProxy* proxy,
                       const hk::hkArray<hk::hkRootCdPoint>& manifold, hk::hkSimplexSolverInput& input)
        {
            hk::bhkCharacterController& ctrl = *self;
            const int numPoints = (std::min)(input.m_numConstraints, manifold.getSize());
            constexpr int kMaxPoints = 64;
            if (numPoints > kMaxPoints) return next(self, proxy, manifold, input);

            std::array<hk::hkVector4, kMaxPoints> before;
            for (int i = 0; i < numPoints; i++) before[i] = input.m_constraints[i].m_plane;
            const bool hadPotential = (ctrl.m_flags & kHasPotentialSupportManifold) != 0;

            next(self, proxy, manifold, input);

            if (hadPotential || !(ctrl.m_flags & kHasPotentialSupportManifold)) return;
            for (int i = 0; i < numPoints; i++)
                if (IsRealSupport(ctrl, manifold[i], before[i], input.m_constraints[i].m_plane)) return;
            ctrl.m_flags &= ~kHasPotentialSupportManifold;
        }

        /// @brief Controllers in a stick-to-floor run: they walked off support while not rising, and OnGround's
        ///        down-cast is pulling them to the floor below.
        /// @note Keyed by address; a freed controller's slot reused by a new one grants it at most one run.
        std::mutex                                 g_stickLock;
        std::unordered_map<const void*, bool>      g_sticking;
        std::atomic<std::uint32_t>                 g_logged{ 0 };

        /// @brief The gate's inputs and verdict for the player on an unsupported OnGround step, when they change.
        /// @note Diagnostics ride on the contact probe's flag; the player is one thread, so the last-line state is plain.
        void LogGate(const hk::bhkCharacterController& ctrl, bool wasSupported, float vz, bool redirect)
        {
            static const bool on = core::debug::FlagOn("char_contact_probe");
            if (!on) return;
            static std::uint64_t last = ~0ull;
            const std::uint64_t  sig  = (static_cast<std::uint64_t>(ctrl.m_wantState) << 8) | (ctrl.m_fakeSupport ? 0x10 : 0)
                                    | ((ctrl.m_flags & kJumping) ? 0x20 : 0) | (wasSupported ? 0x40 : 0) | (redirect ? 0x80 : 0)
                                    | (static_cast<std::uint64_t>(ctrl.m_surfaceInfo.m_supportedState) << 16) | (ctrl.m_onlyWallContacts ? 1 : 0);
            if (sig == last || g_logged.fetch_add(1, std::memory_order_relaxed) >= 5000) return;
            last = sig;
            LOG_INFO("ground model: OnGround unsupported - surface {} onlyWalls {} want {} fake {} jumping {} wasSupported {} vz {:.2f} -> {}",
                     static_cast<int>(ctrl.m_surfaceInfo.m_supportedState), ctrl.m_onlyWallContacts, static_cast<int>(ctrl.m_wantState),
                     ctrl.m_fakeSupport, (ctrl.m_flags & kJumping) != 0, wasSupported, vz, redirect ? "InAir (no down-cast)" : "vanilla");
        }

        /// @brief Jolt's StickToFloor gate on OnGround's unsupported down-cast. Vanilla casts fUnsupportCastLength down
        ///        whenever OnGround has no support and, on a hit, stays grounded under fUnsupportedGravMult (4x) gravity:
        ///        right for walking down stairs, but it also slams a character that reached OnGround unsupported (a
        ///        forced state at a jump's apex) down a wall. Jolt sticks only on the step that went from supported to
        ///        unsupported while not moving up; anything else requests InAir, which OnGround::Simulate honours
        ///        without the cast (a pending request skips it) and commits this step under normal gravity.
        /// @pre Runs just before UpdatePhysics turns the inputs into kSupport and simulates the state, so bit 8 still
        ///      holds the previous step's support on entry.
        void OnCheckSupport(CheckSupport::Next next, hk::bhkCharacterController* self)
        {
            hk::bhkCharacterController& ctrl = *self;
            const bool wasSupported = (ctrl.m_flags & kSupport) != 0;

            next(self);

            const void* key       = static_cast<const void*>(&ctrl);
            const bool  supported = (ctrl.m_surfaceInfo.m_supportedState == hk::hkpSurfaceInfo::SUPPORTED && !ctrl.m_onlyWallContacts)
                                 || (ctrl.m_flags & kHasPotentialSupportManifold);
            const bool  candidate = ctrl.m_context.m_currentState == hk::HK_CHARACTER_ON_GROUND && !supported && !ctrl.m_fakeSupport
                                 && !(ctrl.m_flags & kJumping) && ctrl.m_wantState == hk::HK_CHARACTER_MAX_STATE_ID;

            const bool onGroundUnsupported = ctrl.m_context.m_currentState == hk::HK_CHARACTER_ON_GROUND && !supported;
            std::scoped_lock lock(g_stickLock);
            bool         redirect = false;
            float        vz       = 0.0f;
            if (!candidate) {
                if (supported || ctrl.m_context.m_currentState != hk::HK_CHARACTER_ON_GROUND) g_sticking.erase(key);
            } else {
                bool& sticking = g_sticking[key];
                if (wasSupported) {
                    hk::hkVector4 velocity;
                    GetVelocity::Call(self, velocity);
                    vz       = velocity(2);
                    sticking = vz <= 1.0e-3f;
                }
                redirect = !sticking;
                if (redirect) {
                    ctrl.m_wantState = hk::HK_CHARACTER_IN_AIR;
                    g_sticking.erase(key);
                }
            }
            if (onGroundUnsupported && IsPlayer(&ctrl)) LogGate(ctrl, wasSupported, vz, redirect);
        }

        [[maybe_unused]] const bool s_support =
            Process::Subscribe(&OnProcess, core::hooks::Priority::Late, "CharGroundModel", "char_support_strict");
        [[maybe_unused]] const bool s_stick =
            CheckSupport::Subscribe(&OnCheckSupport, core::hooks::Priority::Late, "CharGroundModel", "char_stick_to_floor");

    }  // namespace

}  // namespace CB::feature
