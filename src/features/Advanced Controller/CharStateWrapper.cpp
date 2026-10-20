#include "PCH.h"

#include "CharDebugHud.h"
#include "CharGround.h"

#include <core/debug/Settings.h>
#include <platform/hooks/hookslib.h>
#include <platform/hooks/Override.h>

#include <Physics/Dynamics/Entity/hkpRigidBody.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterController.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterState.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkWorld.h>
#include <Physics/Utilities/CharacterControl/StateMachine/hkpCharacterStateManager.h>
#include <Physics/Utilities/CharacterControl/StateMachine/Util/hkpCharacterMovementUtil.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <mutex>
#include <unordered_map>

/// @file
/// @brief Steps 1, 3, 6 and 7 of the character controller spec. hkpCharacterStateManager::getState (overridden)
///        returns a wrapper for ids 0 to 2 (OnGround, Jumping, InAir); the wrapper's Simulate decides per controller.
///        Under char_ground_states the player's proxy controller runs CB's ground states (Grounded mapped to OnGround,
///        Airborne mapped to InAir, Jumping only a request token); every other controller, and the player
///        without the flag, forwards to the vanilla state.
/// @note Why getState and not registerState: one hkpCharacterStateManager is shared by every controller (created by
///       the bhkCharacterControllerCinfo constructor, 0x140e9c960), so a registered state would replace it for every
///       actor; the wrapper dispatches per controller instead.

namespace CB::feature {

    namespace {

        namespace hk = CB::havok;
        namespace ch = CB::feature::controller;
        namespace eng        = platform::hooks::engine;
        using GetState       = platform::hooks::Override<&hk::hkpCharacterStateManager::getState>;
        using SimulateFn     = void (*)(const void* state, hk::bhkCharacterController* controller);

        constexpr int kSlots        = 9;   ///< hkReferencedObject 0-2, getType 3, enter/leave 4-5, update/change 6-7, Simulate 8
        constexpr int kSimulateSlot = 8;
        constexpr int kWrapped      = 3;   ///< OnGround 0, Jumping 1, InAir 2

        /// @brief Object sizes per vanilla (bhkCharacterState.h): OnGround and InAir 0x18, Jumping 0x10.
        constexpr std::array<std::size_t, kWrapped> kStateSize = { 0x18, 0x10, 0x18 };

        /// @brief A byte copy of one vanilla state object under a cloned vtable whose Simulate is CB's.
        /// @invariant Every slot but Simulate is vanilla's own code, run on a byte-identical copy (the shared state
        ///            objects hold configuration only, never per-character data).
        /// @note Reference counting: m_memSizeAndFlags 0 and a reference count of 0x4000 so no add/removeReference
        ///       pair can ever free it, whichever rule Havok applies (removeReference is out of line, unread).
        /// @note vtable[0] is vanilla's RTTI complete-object locator (MSVC reads it at vptr[-1] for dynamic_cast); the
        ///       object's vptr points at vtable[1].
        struct Wrapper {
            alignas(16) std::uint8_t     bytes[0x20]{};
            std::array<const void*, kSlots + 1> vtable{};
            const void*                  vanilla = nullptr;           ///< the vanilla state object it copies
            SimulateFn                   vanillaSimulate = nullptr;
            std::atomic<bool>            ready{ false };
        };

        std::array<Wrapper, kWrapped> g_wrappers;
        std::mutex                    g_buildLock;

        /// @brief Player calls per state id, logged at the first call and every power of two.
        std::array<std::atomic<std::uint64_t>, kWrapped> g_playerCalls{};
        std::atomic<int>                                 g_lastPlayerState{ -1 };

        using ch::SpringDamperExact;

        /// @brief Game units per Havok unit: bhkWorld::s_worldScaleInverse, which the plugin does not link.
        constexpr float kGameUnitsPerHavok = 69.99125f;
        /// @brief P2: the Grounded velocity spring's halflife (seconds).
        constexpr float kGroundHalflife = 0.08f;
        /// @brief G2: how long after walking off a jump request is still honoured (seconds).
        constexpr float kCoyoteTime = 0.1f;
        /// @brief G4: a character that loses the ground while rising slower than this (Havok units/s, about 35 game
        ///        units/s) is following the ground, not launching, and sticks to ground within the probe.
        /// @note Why not 0: the ground step lays the velocity along the slope, so walking up uneven ground rises a little
        ///       every step; requiring vz <= 0 sent 18 such walk-offs in one session airborne for a few hundredths of a
        ///       second, bouncing the character between ground and air (and the fall animation with it) for up to 2 s.
        constexpr float kStickMaxRise = 0.5f;
        /// @brief G6: the step vanilla InAir's per-step gain was tuned at; the gain becomes a halflife at this rate so
        ///        air control no longer depends on the frame rate.
        /// @note Assumed 60 Hz (vanilla's usual physics step); unverified against bhkWorld's step time.
        constexpr float kAirReferenceStep = 1.0f / 60.0f;
        constexpr hk::hkUint32 kHitFlags  = 1u << 7;   ///< landing: UpdatePhysics returns 2, the fall-damage path
        constexpr hk::hkUint32 kChaseOrRagdoll = 0x1800;   ///< kChaseBip | kFollowRagdoll: left to vanilla

        /// @note No Sliding mode: ground too steep to stand on is Grounded with low support, where the slide velocity
        ///       (ch::SlideStep) drags the character down and the player cannot climb (ch::ClipUphill).
        enum class Mode : std::uint8_t { Grounded, Airborne };
        enum class AirEntry : std::uint8_t { None, WalkedOff, Jumped, Launched, OutOfWater };

        const char* Name(Mode m) { return m == Mode::Grounded ? "Grounded" : "Airborne"; }
        const char* Name(AirEntry e)
        {
            switch (e) {
                case AirEntry::WalkedOff:  return "walked off";
                case AirEntry::Jumped:     return "jumped";
                case AirEntry::Launched:   return "launched";
                case AirEntry::OutOfWater: return "out of water";
                default:                   return "-";
            }
        }

        /// @brief One managed controller's state between steps.
        /// @invariant `vx`/`vy` is the Grounded spring's commanded horizontal velocity relative to the surface (Havok
        ///            units), BEFORE the proxy's collision: the jump launch reads it (G5's wall-jump fix).
        struct Track {
            bool     init = false;
            Mode     mode = Mode::Grounded;
            AirEntry entry = AirEntry::None;
            int      lastState = -1;   ///< m_context.m_currentState as of the last CB step
            float    airTime = 0.0f;
            bool     springInit = false;
            float    vx = 0, vy = 0, ax = 0, ay = 0;
            float    sx = 0, sy = 0;   ///< the slide velocity (horizontal, Havok units/s), added to the spring's on the ground
            ch::Ground lastVerdict = ch::Ground::Airborne;
            bool     variableJump = false;   ///< this jump was pressed by the player, so its release shapes it
            bool     hopDecided   = false;   ///< iJumpMode 2: short or full is settled
            bool     released     = false;   ///< Jump came up during this jump's rise
            float    launchHeight = 0.0f;    ///< game units, where the jump left the ground
            float    jumpHeight   = 0.0f;    ///< Havok units, this jump's full height after the support scaling
            float    groundSupport = 1.0f;   ///< support at the last Grounded step: a coyote jump's
        };

        /// @brief The fraction of the full jump height a jump from ground of `support` gets: 1 on supporting ground,
        ///        down to [Character] fMinJumpScale (default 0.5) where the ground holds nothing.
        /// @note Why scaled and not refused: any ground under the feet lets the player jump (agency), and the uphill
        ///       clip already keeps a hop on a steep face from climbing it, so the scaling only sets how much air it gets.
        float JumpScale(float support)
        {
            static const float floor = [] {
                const float f = std::clamp(ch::SettingFloat("fMinJumpScale", 0.5f), 0.0f, 1.0f);
                LOG_INFO("ground states: a jump from unsupported ground gets {:.2f} of the full height", f);
                return f;
            }();
            return floor + (1.0f - floor) * std::clamp(support, 0.0f, 1.0f);
        }

        /// @note Keyed by controller address; references into an unordered_map survive rehashing, and only one
        ///       thread steps a given controller, so the lock guards lookup only.
        std::mutex                                  g_trackLock;
        std::unordered_map<const void*, Track>      g_tracks;

        std::atomic<std::uint32_t> g_logs{ 0 };
        constexpr std::uint32_t    kLogCap = 4000;
        bool LogBudget() { return g_logs.fetch_add(1, std::memory_order_relaxed) < kLogCap; }

        Track& TrackFor(const hk::bhkCharacterController* ctrl)
        {
            std::scoped_lock lock(g_trackLock);
            return g_tracks[static_cast<const void*>(ctrl)];
        }

        /// @brief A float INI / game setting, `def` while absent (callers cache the value).
        using ch::IniFloat;
        float GmstFloat(const char* name, float def)
        {
            auto* gmst = RE::GameSettingCollection::GetSingleton();
            RE::Setting* s = gmst ? gmst->GetSetting(name) : nullptr;
            return s ? s->GetFloat() : def;
        }

        /// @brief The world's gravity (Havok units/s^2), zero without a world.
        void WorldGravity(hk::bhkCharacterController& c, float& gx, float& gy, float& gz)
        {
            gx = gy = gz = 0.0f;
            hk::bhkWorld* world = c.GetHavokWorld();
            if (!world) return;
            const hk::hkVector4& g = world->GetGravity();
            gx = g(0), gy = g(1), gz = g(2);
        }

        using ch::DesiredHorizontal;

        float HeightGame(hk::bhkCharacterController& c)
        {
            hk::hkVector4 pos;
            c.GetPositionImpl(pos, true);
            return pos(2) * kGameUnitsPerHavok;
        }

        void Commit(hk::bhkCharacterController& c, hk::hkpCharacterStateType state)
        {
            c.m_wantState = state;
            c.CommitWantedState();
        }

        /// @brief The player's Jump button, from the input event stream (every device, as the player mapped it).
        /// @note Registered lazily from the physics step: features have no load-time callback, and the input device
        ///       manager exists long before the player can step.
        class JumpInput final : public RE::BSTEventSink<RE::InputEvent*> {
        public:
            static JumpInput& Get()
            {
                static JumpInput s;
                return s;
            }

            void EnsureRegistered()
            {
                if (m_registered.load(std::memory_order_acquire)) return;
                auto* devices = RE::BSInputDeviceManager::GetSingleton();
                bool  expected = false;
                if (devices && m_registered.compare_exchange_strong(expected, true)) {
                    devices->AddEventSink(this);
                    LOG_INFO("variable jump: listening for the Jump button");
                }
            }

            bool Held() const { return m_held.load(std::memory_order_relaxed); }

            /// @brief Seconds since Jump last went down; huge before the first press.
            float SincePress() const
            {
                const std::int64_t at = m_pressNs.load(std::memory_order_relaxed);
                if (at == 0) return 1.0e9f;
                return static_cast<float>(Now() - at) * 1.0e-9f;
            }

            RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override
            {
                auto* names = RE::UserEvents::GetSingleton();
                for (RE::InputEvent* e = a_event && names ? *a_event : nullptr; e; e = e->next) {
                    const RE::ButtonEvent* b = e->AsButtonEvent();
                    if (!b || b->QUserEvent() != names->jump) continue;
                    if (b->IsDown()) {
                        m_pressNs.store(Now(), std::memory_order_relaxed);
                        m_held.store(true, std::memory_order_relaxed);
                    } else if (b->IsUp()) {
                        m_held.store(false, std::memory_order_relaxed);
                    }
                }
                return RE::BSEventNotifyControl::kContinue;
            }

        private:
            static std::int64_t Now()
            {
                return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            }

            std::atomic<bool>         m_registered{ false };
            std::atomic<bool>         m_held{ false };
            std::atomic<std::int64_t> m_pressNs{ 0 };
        };

        /// @brief settings.ini [Character] tunables for char_variable_jump, read once.
        struct JumpTunables {
            int   mode             = 1;      ///< 1 = release gravity (continuous), 2 = short / full hop
            float releaseGravity   = 3.0f;   ///< mode 1: gravity multiplier while rising after release
            float shortHopFraction = 0.5f;   ///< mode 2: a short hop's height as a fraction of the full jump
            float shortHopWindow   = 0.1f;   ///< mode 2: release within this many seconds of the press = short hop
            float pressMemory      = 0.5f;   ///< a launch this long after a press still belongs to it (animation lead-in)
        };

        using ch::SettingFloat;

        const JumpTunables& Jump()
        {
            static const JumpTunables t = [] {
                JumpTunables j;
                j.mode             = static_cast<int>(SettingFloat("iJumpMode", 1.0f));
                j.releaseGravity   = (std::max)(1.0f, SettingFloat("fJumpReleaseGravity", j.releaseGravity));
                j.shortHopFraction = std::clamp(SettingFloat("fShortHopFraction", j.shortHopFraction), 0.05f, 1.0f);
                j.shortHopWindow   = (std::max)(0.0f, SettingFloat("fShortHopWindow", j.shortHopWindow));
                LOG_INFO("variable jump: mode {} release gravity {:.2f} short hop {:.2f} within {:.3f} s", j.mode, j.releaseGravity,
                         j.shortHopFraction, j.shortHopWindow);
                return j;
            }();
            return t;
        }

        bool VariableJumpOn()
        {
            static const bool on = core::debug::FlagOn("char_variable_jump");
            return on;
        }

        /// @brief A player jump's rising step: returns the gravity multiplier and may lower `oz` (Havok units/s).
        /// @note Full height is vanilla's whenever Jump stays held, so jump-height settings and NPC path jumps (never
        ///       variable: player only) keep their meaning; releasing only ever shortens the rise.
        float VariableRise(hk::bhkCharacterController& c, Track& t, float& oz, float height, float g)
        {
            const JumpTunables& j    = Jump();
            JumpInput&          in   = JumpInput::Get();
            const bool          held = in.Held();
            if (!held && !t.released) {
                t.released = true;
                if (LogBudget())
                    LOG_INFO("variable jump: released {:.3f} s after the press, risen {:.1f} game units", in.SincePress(), height - t.launchHeight);
            }
            if (j.mode == 2) {
                if (t.hopDecided) return 1.0f;
                const float since = in.SincePress();
                if (!held && since <= j.shortHopWindow) {
                    const float target = t.jumpHeight * j.shortHopFraction;
                    const float risen  = (height - t.launchHeight) / kGameUnitsPerHavok;
                    const float left   = (std::max)(0.0f, target - risen);
                    oz                 = (std::min)(oz, std::sqrt(2.0f * g * left));
                    t.hopDecided       = true;
                    if (LogBudget()) LOG_INFO("variable jump: short hop ({:.2f} of {:.2f} Havok units), vz -> {:.2f}", target, t.jumpHeight, oz);
                } else if (since > j.shortHopWindow) {
                    t.hopDecided = true;
                    if (LogBudget()) LOG_INFO("variable jump: full hop");
                }
                return 1.0f;
            }
            return held ? 1.0f : j.releaseGravity;
        }

        /// @brief G7 + G6: one Airborne step. Gravity 1x while rising (above m_fallStartHeight, which it raises),
        ///        fInAirFallingCharGravityMult:HAVOK while falling; air control as the ground spring at the stiffness
        ///        vanilla InAir's gain implies.
        void AirStep(hk::bhkCharacterController& c, Track& t, float dt, bool steer)
        {
            const hk::hkVector4 v = c.m_outVelocity;
            float ox = v(0), oy = v(1), oz = v(2);
            if (steer && c.m_acrobatics > 0.0f) {
                static const float base = GmstFloat("fJumpMoveBase", 0.0f);
                static const float mult = GmstFloat("fJumpMoveMult", 0.0f);
                float gain = (c.m_flags & kChaseOrRagdoll) ? 1.0f : c.m_acrobatics * mult + base;
                if (gain > 0.0f) {
                    if (gain > 0.999f) gain = 0.999f;
                    const float rate     = -std::log(1.0f - gain) / kAirReferenceStep;
                    const float halflife = 0.6931472f / rate;
                    float dx, dy;
                    DesiredHorizontal(c, dx, dy);
                    SpringDamperExact(ox, t.ax, dx, halflife, dt);
                    SpringDamperExact(oy, t.ay, dy, halflife, dt);
                }
            }
            float      gravityMult = 1.0f;
            const float height     = HeightGame(c);
            if (height > c.m_fallStartHeight) c.m_fallStartHeight = height;
            else {
                static const float falling = IniFloat("fInAirFallingCharGravityMult:HAVOK", 1.35f);
                gravityMult = falling;
            }
            float gx, gy, gz;
            WorldGravity(c, gx, gy, gz);
            if (t.variableJump) {
                if (oz > 0.0f) {
                    const float g = std::sqrt(gx * gx + gy * gy + gz * gz) * c.m_gravity;
                    gravityMult   = (std::max)(gravityMult, VariableRise(c, t, oz, height, g));
                } else {
                    t.variableJump = false;
                    if (LogBudget())
                        LOG_INFO("variable jump: apex {:.1f} game units above the launch (full {:.1f}){}", height - t.launchHeight,
                                 t.jumpHeight * kGameUnitsPerHavok, t.released ? "" : ", held throughout");
                }
            }
            const float k = gravityMult * c.m_gravity * dt;
            c.m_outVelocity.set(ox + gx * k, oy + gy * k, oz + gz * k, 0.0f);
            c.AccumulateFallTime();
            t.airTime += dt;
        }

        /// @brief P2 + step 7: one Grounded step. Each horizontal component springs toward the desired velocity (its
        ///        uphill part cut by the ground's lack of support), the slide velocity is added, the sum is laid along
        ///        the ground plane at the same speed (as vanilla's surface frame does), plus one step of gravity
        ///        pressing into the floor (vanilla OnGround's pressure, never its 4x).
        void GroundStep(hk::bhkCharacterController& c, Track& t, const ch::FloorVerdict& fv, float dt)
        {
            const hk::hkVector4 v    = c.m_outVelocity;
            const hk::hkVector4 surf = c.m_surfaceInfo.m_surfaceVelocity;
            if (!t.springInit) {
                t.vx = v(0) - surf(0) - t.sx, t.vy = v(1) - surf(1) - t.sy, t.ax = t.ay = 0.0f;
                t.springInit = true;
            }
            float dx, dy;
            DesiredHorizontal(c, dx, dy);
            ch::ClipUphill(fv, dx, dy);
            SpringDamperExact(t.vx, t.ax, dx, kGroundHalflife, dt);
            SpringDamperExact(t.vy, t.ay, dy, kGroundHalflife, dt);

            /// @note A slide the body is not making (a wall or a ledge lip stopped it) does not keep growing: along its
            ///       own direction it is held to the body's actual speed before this step's gain.
            const float slide = std::sqrt(t.sx * t.sx + t.sy * t.sy);
            if (slide > 1.0e-6f) {
                hk::hkVector4 body;
                c.GetLinearVelocityImpl(body);
                const float made = (std::max)(0.0f, ((body(0) - surf(0)) * t.sx + (body(1) - surf(1)) * t.sy) / slide);
                if (made < slide) t.sx *= made / slide, t.sy *= made / slide;
            }
            float gx, gy, gz;
            WorldGravity(c, gx, gy, gz);
            ch::SlideStep(fv, gx * c.m_gravity, gy * c.m_gravity, gz * c.m_gravity, t.sx, t.sy, dt);

            float hx = t.vx + t.sx, hy = t.vy + t.sy, hz = 0.0f;
            const float nz = fv.groundNz;
            if (nz > 1.0e-3f) {
                hz = -(fv.groundNx * hx + fv.groundNy * hy) / nz;
                const float h2 = hx * hx + hy * hy, l2 = h2 + hz * hz;
                if (l2 > 1.0e-12f) {
                    const float s = std::sqrt(h2 / l2);
                    hx *= s, hy *= s, hz *= s;
                }
            }
            c.m_outVelocity.set(hx + surf(0), hy + surf(1), hz + surf(2) + gz * dt, 0.0f);
            c.ApplyInitialVelocity();
        }

        void LogTransition(const Track& t, Mode to, const char* why, const ch::FloorVerdict& fv, float vz)
        {
            if (!LogBudget()) return;
            LOG_INFO("ground states: {} -> {} ({}) - verdict {} support {}/{} stickable {} stickDz {:.3f} vz {:.2f} airTime {:.3f}", Name(t.mode),
                     Name(to), why, ch::Name(fv.ground), fv.supportHits, fv.walkableHits, fv.stickable, fv.stickDz, vz, t.airTime);
        }

        void EnterAirborne(hk::bhkCharacterController& c, Track& t, AirEntry entry, const ch::FloorVerdict& fv, float vz)
        {
            LogTransition(t, Mode::Airborne, Name(entry), fv, vz);
            t.mode       = Mode::Airborne;
            t.entry      = entry;
            t.airTime    = 0.0f;
            t.variableJump = false;
            t.ax = t.ay  = 0.0f;
            t.sx = t.sy  = 0.0f;
            t.springInit = false;
            if (entry != AirEntry::Jumped && entry != AirEntry::OutOfWater) c.m_fallStartHeight = HeightGame(c);
            Commit(c, hk::HK_CHARACTER_IN_AIR);
        }

        void EnterGrounded(hk::bhkCharacterController& c, Track& t, Mode to, const char* why, const ch::FloorVerdict& fv, float vz)
        {
            LogTransition(t, to, why, fv, vz);
            if (t.mode == Mode::Airborne) {
                c.m_flags |= kHitFlags;
                c.m_flags &= ~ch::kJumping;
                c.m_fallTime = 0.0f;
                t.springInit = false;
            }
            t.mode    = to;
            t.entry   = AirEntry::None;
            t.airTime = 0.0f;
            Commit(c, hk::HK_CHARACTER_ON_GROUND);
        }

        /// @brief G5: the jump's entry action, then the launch step's gravity (as vanilla Jumping simulates InAir at
        ///        once).
        /// @note The horizontal launch is the Grounded spring's commanded velocity (before the proxy slid it along a
        ///       wall); from the air (coyote, out of water) the body's own.
        void Launch(hk::bhkCharacterController& c, Track& t, AirEntry entry, const ch::FloorVerdict& fv, float dt)
        {
            const hk::hkVector4 v    = c.m_outVelocity;
            const hk::hkVector4 surf = c.m_surfaceInfo.m_surfaceVelocity;
            float hx = v(0), hy = v(1);
            if (t.mode != Mode::Airborne && t.springInit) hx = t.vx + t.sx + surf(0), hy = t.vy + t.sy + surf(1);
            float gx, gy, gz;
            WorldGravity(c, gx, gy, gz);
            gx *= c.m_gravity, gy *= c.m_gravity, gz *= c.m_gravity;
            const float g = std::sqrt(gx * gx + gy * gy + gz * gz);
            /// @note The support the jump leaves from: the floor's on the ground, the last ground's from the coyote
            ///       window, full out of the water.
            const float support = entry == AirEntry::OutOfWater ? 1.0f
                                : t.mode == Mode::Airborne      ? t.groundSupport
                                : fv.ground == ch::Ground::Grounded ? fv.support
                                                                    : 1.0f;
            const float jumpHeight = std::fabs(c.m_jumpHeight) * JumpScale(support);
            const float speed      = std::sqrt(jumpHeight * 2.0f * g);
            float ux = 0.0f, uy = 0.0f, uz = 1.0f;
            if (g > 1.0e-6f) ux = -gx / g, uy = -gy / g, uz = -gz / g;
            if (LogBudget())
                LOG_INFO("ground states: jump ({}) - launch horizontal ({:.2f}, {:.2f}) vs body ({:.2f}, {:.2f}), vertical {:.2f} (support {:.2f}, "
                         "height x{:.2f})",
                         Name(entry), hx, hy, v(0), v(1), speed, support, JumpScale(support));
            c.m_outVelocity.set(hx + ux * speed, hy + uy * speed, uz * speed, 0.0f);
            c.m_flags |= ch::kJumping;
            c.m_flags &= ~ch::kSupport;
            c.m_fallStartHeight = HeightGame(c);
            EnterAirborne(c, t, entry, fv, uz * speed);
            t.launchHeight = c.m_fallStartHeight;
            t.jumpHeight   = jumpHeight;
            t.hopDecided   = false;
            t.released     = false;
            if (entry == AirEntry::Jumped && VariableJumpOn() && ch::IsPlayer(&c)) {
                const float since = JumpInput::Get().SincePress();
                t.variableJump    = since <= Jump().pressMemory;
                if (LogBudget())
                    LOG_INFO("variable jump: launch {:.3f} s after the press, held {}{}", since, JumpInput::Get().Held(),
                             t.variableJump ? "" : " - no recent press, full jump");
            }
            AirStep(c, t, dt, false);
        }

        /// @brief Snaps the character down onto the walkable floor `dz` (negative, Havok units) below its feet (M8).
        void Stick(hk::bhkCharacterController& c, float dz)
        {
            hk::hkVector4 pos;
            c.GetPositionImpl(pos, true);
            pos(2) += dz;
            c.SetPositionImpl(pos, true, false);
        }

        void SetSupportFrom(hk::bhkCharacterController& c, const ch::FloorVerdict& fv)
        {
            if (!fv.supportBody) return;
            hk::hkpRigidBody* body = hk::hkpGetRigidBody(fv.supportBody);
            if (body && body != c.m_supportBody.m_pntr) c.SetSupportBody(body);
        }

        /// @brief Runs one managed step through the vanilla state now current (Swimming, Climbing, ...).
        void RunCurrentVanilla(hk::bhkCharacterController& c)
        {
            const hk::hkpCharacterState* state = c.m_context.m_stateManager->getState(c.m_context.m_currentState);
            if (!state) return;
            const auto fn = reinterpret_cast<SimulateFn>((*reinterpret_cast<void* const* const*>(state))[kSimulateSlot]);
            fn(state, &c);
        }

        void PublishHud(hk::bhkCharacterController& c, const Track& t, const ch::Hull& hull, const ch::FloorVerdict& fv)
        {
            ch::hud::Controller h;
            const ch::FloorTunables tun;
            h.feet         = { hull.cx, hull.cy, hull.feetZ };
            h.radius       = hull.radius;
            h.height       = hull.topZ - hull.feetZ;
            h.mode         = Name(t.mode);
            h.entry        = Name(t.entry);
            h.verdict      = fv.ground;
            h.groundNz     = fv.groundNz;
            h.supportHits  = fv.supportHits;
            h.walkableHits = fv.walkableHits;
            h.support      = fv.support;
            h.slide        = { t.sx, t.sy, 0.0f };
            h.airTime      = t.airTime;
            h.variableJump = t.variableJump;
            h.jumpReleased = t.released;
            const hk::hkVector4 v = c.m_outVelocity;
            h.velocity = { v(0), v(1), v(2) };
            float dx, dy;
            DesiredHorizontal(c, dx, dy);
            h.desired = { dx, dy, 0.0f };
            for (int i = 0; i < static_cast<int>(h.rays.size()); i++) {
                const ch::FloorHit& hit = fv.hits[i];
                ch::hud::Ray&       r   = h.rays[i];
                float x, y;
                ch::FloorRayXY(hull, i, tun, x, y);
                r.top      = { x, y, hull.feetZ + hull.radius };
                r.bottom   = { x, y, hull.feetZ - c.m_stepHeight };
                r.hit      = hit.hit;
                r.hitZ     = hull.feetZ + hit.dz;
                r.walkable = hit.hit && hit.nz >= c.m_zNormal && hit.layer != ch::kCharControllerLayer;
                r.wall     = hit.hit && (hit.nz < tun.wallNormalZ || hit.layer == ch::kCharControllerLayer);
            }
            ch::hud::Publish(h);
        }

        /// @brief CB's Simulate for the player in OnGround / Jumping / InAir.
        /// @note Contract outputs each step: m_outVelocity (Havok units, what the proxy integrates), the committed
        ///       state, kCanJump, kJumping, the support body when Grounded, the fall data (m_fallStartHeight,
        ///       m_fallTime, kHitFlags on landing).
        /// @warning kCanJump is written here, but the proxy's constraints callback during the integrate (Unk_0B) may
        ///          OR it in again for a supported, unchanged state; it never clears it.
        void CbSimulate(int id, hk::bhkCharacterController& c)
        {
            Track&      t   = TrackFor(&c);
            const float dt  = c.m_stepInfo.m_deltaTime;
            if (VariableJumpOn() && ch::IsPlayer(&c)) JumpInput::Get().EnsureRegistered();
            const int   cur = static_cast<int>(c.m_context.m_currentState);
            const int   previousState = t.init ? t.lastState : -1;

            if (!t.init || t.lastState != cur) {
                const Mode before = t.mode;
                t.mode       = cur == hk::HK_CHARACTER_IN_AIR ? Mode::Airborne : Mode::Grounded;
                t.entry      = t.mode == Mode::Airborne ? AirEntry::Launched : AirEntry::None;
                t.springInit = false;
                t.sx = t.sy  = 0.0f;
                t.airTime    = 0.0f;
                if (LogBudget())
                    LOG_INFO("ground states: resync to vanilla state {} ({} -> {}{})", cur, Name(before), Name(t.mode), t.init ? "" : ", first step");
                t.init = true;
            }

            int want = static_cast<int>(c.m_wantState);
            if (want == cur) want = hk::HK_CHARACTER_MAX_STATE_ID;

            /// @note G2: states CB does not run are honoured at once and simulated by vanilla this step.
            if (want == hk::HK_CHARACTER_CLIMBING || want == hk::HK_CHARACTER_FLYING
                || (want >= hk::HK_CHARACTER_USER_STATE_0 && want < hk::HK_CHARACTER_MAX_STATE_ID)) {
                if (LogBudget()) LOG_INFO("ground states: {} -> vanilla state {} (requested)", Name(t.mode), want);
                if (t.mode == Mode::Airborne) c.m_fallTime = 0.0f;
                Commit(c, static_cast<hk::hkpCharacterStateType>(want));
                t.lastState = want;
                t.init      = false;
                if (want != hk::HK_CHARACTER_FLYING) RunCurrentVanilla(c);
                return;
            }

            const ch::Hull     hull = ch::GetHull(c);
            const ch::FloorVerdict fv = hull.valid ? ch::QueryFloor(c, hull) : ch::FloorVerdict{};
            const float        vz   = c.m_outVelocity(2);
            if (fv.ground != t.lastVerdict) {
                if (LogBudget())
                    LOG_INFO("ground states: verdict {} -> {} in {} (support {}/{} stickable {} stickDz {:.3f} n.z {:.3f} vz {:.2f})", ch::Name(t.lastVerdict),
                             ch::Name(fv.ground), Name(t.mode), fv.supportHits, fv.walkableHits, fv.stickable, fv.stickDz, fv.groundNz, vz);
                t.lastVerdict = fv.ground;
            }

            if (id == hk::HK_CHARACTER_JUMPING || cur == hk::HK_CHARACTER_JUMPING) {
                /// @note A Jumping state some vanilla state committed (Swimming's jump out of the water): launch.
                Launch(c, t, previousState == hk::HK_CHARACTER_USER_STATE_0 ? AirEntry::OutOfWater : AirEntry::Jumped, fv, dt);
            } else if (t.mode != Mode::Airborne) {
                bool grounded = true;
                if (want == hk::HK_CHARACTER_IN_AIR) {
                    EnterAirborne(c, t, AirEntry::Launched, fv, vz);
                    grounded = false;
                } else if (fv.ground != ch::Ground::Airborne) {
                    /// @note Any ground under the feet keeps the character Grounded; how much it holds them up is the
                    ///       verdict's support, which the slide and the uphill clip read.
                } else if (vz <= kStickMaxRise && fv.stickable) {
                    /// @note G4/M8: walked off support while not launching, ground within the probe: stick to it.
                    if (LogBudget()) LOG_INFO("ground states: stick {:.3f} down in {} (vz {:.2f})", fv.stickDz, Name(t.mode), vz);
                    Stick(c, fv.stickDz);
                    t.mode = Mode::Grounded;
                } else {
                    EnterAirborne(c, t, AirEntry::WalkedOff, fv, vz);
                    grounded = false;
                }

                if (grounded) {
                    t.groundSupport = fv.ground == ch::Ground::Grounded ? fv.support : 1.0f;
                    if (want == hk::HK_CHARACTER_JUMPING) {
                        Launch(c, t, AirEntry::Jumped, fv, dt);
                    } else {
                        if (ch::Supported(fv)) SetSupportFrom(c, fv);
                        GroundStep(c, t, fv, dt);
                    }
                } else {
                    AirStep(c, t, dt, true);
                }
            } else {
                const bool coyote = t.entry == AirEntry::WalkedOff && t.airTime <= kCoyoteTime && !(c.m_flags & ch::kJumping);
                if (fv.ground == ch::Ground::Grounded && vz <= 0.0f) {
                    /// @note G3: lands on any ground while not rising; steep ground lands too, and slides.
                    EnterGrounded(c, t, Mode::Grounded, "landed", fv, vz);
                    if (ch::Supported(fv)) SetSupportFrom(c, fv);
                    GroundStep(c, t, fv, dt);
                } else if (want == hk::HK_CHARACTER_ON_GROUND && (fv.supportHits > 0 || fv.stickable)) {
                    /// @note G2: a forced OnGround only onto support within probe depth.
                    if (fv.supportHits == 0) Stick(c, fv.stickDz);
                    EnterGrounded(c, t, Mode::Grounded, "forced", fv, vz);
                    GroundStep(c, t, fv, dt);
                } else if (want == hk::HK_CHARACTER_JUMPING && coyote) {
                    if (LogBudget()) LOG_INFO("ground states: coyote jump {:.3f} s after walking off", t.airTime);
                    Launch(c, t, AirEntry::Jumped, fv, dt);
                } else {
                    if (want == hk::HK_CHARACTER_ON_GROUND || want == hk::HK_CHARACTER_JUMPING) {
                        if (LogBudget()) LOG_INFO("ground states: dropped request {} in Airborne ({})", want, Name(t.entry));
                    }
                    AirStep(c, t, dt, true);
                }
            }

            /// @note G2 contract: kCanJump from CB's rule (Grounded on any ground, or inside the coyote window).
            const bool canJumpNow = t.mode == Mode::Grounded
                                 || (t.mode == Mode::Airborne && t.entry == AirEntry::WalkedOff && t.airTime <= kCoyoteTime
                                     && !(c.m_flags & ch::kJumping));
            if (canJumpNow) c.m_flags |= ch::kCanJump;
            else            c.m_flags &= ~ch::kCanJump;

            /// @note Grounded with no hits at the feet is the stick-to-floor case: it stands on the walkable floor it
            ///       was snapped to.
            ch::FloorVerdict decided = fv;
            if (t.mode == Mode::Airborne) {
                decided.ground  = ch::Ground::Airborne;
                decided.support = 0.0f;
            } else if (fv.ground == ch::Ground::Airborne) {
                decided.ground  = ch::Ground::Grounded;
                decided.support = 1.0f;
            }
            ch::PublishVerdict(c, decided);
            ch::PublishSlide(c, t.sx, t.sy);
            if (ch::hud::On() && hull.valid) PublishHud(c, t, hull, fv);
            t.lastState = static_cast<int>(c.m_context.m_currentState);
        }

        /// @brief Whether CB's ground states run `controller` this step.
        bool CbRuns(hk::bhkCharacterController* controller)
        {
            static const bool on = core::debug::FlagOn("char_ground_states");
            if (!on || !controller || !ch::IsManaged(*controller)) return false;
            /// @note The schema shim calls these through their pins; a runtime without one leaves the player to vanilla.
            static const bool pinned = eng::Available(eng::bhkWorld_GetGravity) && eng::Available(eng::hkpCharacterMovementUtil_CalculateMovement);
            if (!pinned) return false;
            if (controller->m_flags & kChaseOrRagdoll) return false;
            return controller->GetHavokWorld() != nullptr;
        }

        template <int Id>
        void WrapperSimulate(const void* /*self*/, hk::bhkCharacterController* controller)
        {
            const Wrapper& w = g_wrappers[Id];
            if (ch::IsPlayer(controller)) {
                const std::uint64_t n = g_playerCalls[Id].fetch_add(1, std::memory_order_relaxed) + 1;
                if ((n & (n - 1)) == 0) LOG_INFO("state wrapper: player Simulate in state {} ran {} times through CB", Id, n);
                const int previous = g_lastPlayerState.exchange(Id, std::memory_order_relaxed);
                if (previous != Id) LOG_INFO("state wrapper: player state {} -> {}", previous, Id);
            }
            if (CbRuns(controller)) return CbSimulate(Id, *controller);
            if (ch::IsPlayer(controller)) {
                /// @note The player left CB's states for a step (chase/ragdoll); the next CB step resyncs.
                std::scoped_lock lock(g_trackLock);
                auto it = g_tracks.find(static_cast<const void*>(controller));
                if (it != g_tracks.end()) it->second.init = false;
            }
            w.vanillaSimulate(w.vanilla, controller);
        }

        constexpr std::array<SimulateFn, kWrapped> kWrapperSimulate = { &WrapperSimulate<0>, &WrapperSimulate<1>, &WrapperSimulate<2> };

        /// @pre `vanilla` is the manager's registered state for `id` (0 to 2).
        void Build(int id, const void* vanilla)
        {
            Wrapper& w = g_wrappers[id];
            const auto* vanillaTable = *static_cast<const void* const* const*>(vanilla);
            std::memcpy(w.bytes, vanilla, kStateSize[id]);
            for (int s = -1; s < kSlots; s++) w.vtable[s + 1] = vanillaTable[s];
            w.vanillaSimulate           = reinterpret_cast<SimulateFn>(vanillaTable[kSimulateSlot]);
            w.vtable[kSimulateSlot + 1] = reinterpret_cast<const void*>(kWrapperSimulate[id]);
            const void* table = w.vtable.data() + 1;
            std::memcpy(w.bytes, &table, sizeof table);
            const std::uint16_t memSizeAndFlags = 0;
            const std::int16_t  referenceCount  = 0x4000;
            std::memcpy(w.bytes + 0x08, &memSizeAndFlags, sizeof memSizeAndFlags);
            std::memcpy(w.bytes + 0x0a, &referenceCount, sizeof referenceCount);
            w.vanilla = vanilla;
            w.ready.store(true, std::memory_order_release);
            LOG_INFO("state wrapper: state {} wraps vanilla {:#x} (vtable {:#x}, Simulate {:#x})", id, reinterpret_cast<std::uintptr_t>(vanilla),
                     reinterpret_cast<std::uintptr_t>(vanillaTable), reinterpret_cast<std::uintptr_t>(vanillaTable[kSimulateSlot]));
        }

        hk::hkpCharacterState* OnGetState(GetState::Next next, const hk::hkpCharacterStateManager* manager, hk::hkpCharacterStateType type)
        {
            hk::hkpCharacterState* vanilla = next(manager, type);
            const int id = static_cast<int>(type);
            if (!vanilla || id < 0 || id >= kWrapped) return vanilla;
            Wrapper& w = g_wrappers[id];
            if (!w.ready.load(std::memory_order_acquire) || w.vanilla != vanilla) {
                std::scoped_lock lock(g_buildLock);
                if (!w.ready.load(std::memory_order_relaxed)) Build(id, vanilla);
                else if (w.vanilla != vanilla) {
                    LOG_WARN("state wrapper: state {} changed under the wrapper ({:#x} -> {:#x}); passing vanilla through", id,
                             reinterpret_cast<std::uintptr_t>(w.vanilla), reinterpret_cast<std::uintptr_t>(vanilla));
                    return vanilla;
                }
            }
            return reinterpret_cast<hk::hkpCharacterState*>(w.bytes);
        }

        [[maybe_unused]] const bool s_subscribed =
            GetState::Subscribe(&OnGetState, platform::hooks::Priority::Normal, "CharStateWrapper", "char_state_wrapper");

    }  // namespace

}  // namespace CB::feature
