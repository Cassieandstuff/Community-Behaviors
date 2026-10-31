#include "PCH.h"

#include "CharGround.h"
#include "CharSweep.h"

#include <platform/hooks/Override.h>

#include <Behavior/Character/hkbCharacter.h>
#include <Physics/Collide/Shape/Compound/Collection/List/hkpListShape.h>
#include <Physics/Collide/Shape/Convex/Capsule/hkpCapsuleShape.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharProxyController.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>

/// @file
/// @brief The floating hull under char_pogo (Box3D's dynamic mover, ported onto the proxy): the managed controller's
///        collision shape rides Lift above its feet, so it clears what lies within step height under it and the ground
///        states' pogo spring carries the feet over it.
/// @note  The player's hull is a list shape (FUN_140ea4020 builds it when the actor's 3D has a collision object): the
///        controller's capsule, enabled, then a clone of the skeleton's collision capsule, which it disables at once
///        (hkpListShape::disableChild, 0x140b235c0). So the capsule edited is the list's one enabled child, and the
///        list's cached box (m_aabbCenter / m_aabbHalfExtents, which its getAabb returns) is recomputed after it.
/// @note  A capsule hull is edited IN PLACE: its bottom moves up to feet + the ride height (m_stepHeight), its top and
///        radius come from the character file (hkbCharacterData::characterControllerInfo, authored by the regen rule
///        capsulefrombound), else they stay the capsule's own. Why in place: the controller builds its capsule per
///        controller (bhkCharacterControllerCinfo, 0x140e9cdf0) and vanilla's own radius resizer (0x140ea0170) already
///        edits it in place; a new shape would need the exe's unpinned bhkCapsuleShape constructor.
/// @note  Any other hull (the horse's list of capsules) floats whole through the position seam instead:
///        GetPositionImpl / SetPositionImpl with the center offset are shifted by the lift, so its top rises with it.
/// @note  Either way the float lasts while the ground states run the controller: an integrate (Unk_0B) of a step whose
///        Simulate did not claim it puts the hull back first, so vanilla's states (Swimming, Climbing, a synced clip,
///        ragdoll) never move a floating hull.

namespace CB::feature {

    namespace {

        namespace hk = CB::havok;
        namespace ch = CB::feature::controller;

        using GetPosition = platform::hooks::Override<&hk::bhkCharProxyController::GetPositionImpl>;
        using SetPosition = platform::hooks::Override<&hk::bhkCharProxyController::SetPositionImpl>;
        using Integrate   = platform::hooks::Override<&hk::bhkCharProxyController::Unk_0B>;

        constexpr float         kSkin       = 0.002f;   ///< kept back from a ceiling a raise meets
        constexpr float         kMinRaise   = 0.002f;   ///< a raise shorter than this is skipped
        constexpr float         kMinChange  = 0.001f;   ///< a capsule edit smaller than this is skipped (Havok units)
        constexpr float         kMinCore    = 0.01f;    ///< the capsule's segment is never shorter than this
        constexpr double        kProfileAge = 2.0;      ///< seconds before the character's capsule is read again
        constexpr std::uint64_t kCharacters = 1ull << ch::kCharControllerLayer;
        /// @brief Havok's default characterControllerInfo capsule (1.7, 0.4): the character file does not author one.
        constexpr std::uint32_t kDefaultHeightBits = 0x3fd9999a, kDefaultRadiusBits = 0x3ecccccd;

        enum class Mode : std::uint8_t { None, Capsule, Offset };

        /// @brief One floating controller.
        /// @note  Two slots: the player's and the ridden horse's. A free slot has a null `ctrl`. Only the thread stepping
        ///        that controller writes a slot's non-atomic members.
        struct Slot {
            std::atomic<const void*> ctrl{ nullptr };
            std::atomic<float>       lift{ 0.0f };
            std::atomic<bool>        claimed{ false };   ///< this step's Simulate was the pogo's
            std::atomic<Mode>        mode{ Mode::None };
            const hk::hkpShape*      hullShape = nullptr;   ///< Mode::Capsule: the collidable's shape when taken
            hk::hkpCapsuleShape*     capsule = nullptr;  ///< Mode::Capsule: the hull's capsule, as the controller built it
            hk::hkpListShape*        list    = nullptr;  ///< the list it is the enabled child of, or null
            hk::hkVector4            origA, origB;       ///< its vertices before CB's edit
            float                    origRadius = 0;
            hk::hkVector4            origListCenter, origListHalf;   ///< the list's cached box before CB's edit
            float                    feet = 0;           ///< the feet's height in the capsule's frame (its original bottom)
            float                    height = 0, radius = 0;   ///< the capsule CB holds it at (Havok units, scaled)
            double                   profileAt = -1.0e9;
        };
        std::array<Slot, 2> g_slots;

        std::atomic<std::uint32_t> g_logs{ 0 };
        bool LogBudget() { return g_logs.fetch_add(1, std::memory_order_relaxed) < 400; }

        double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

        Slot* Find(const void* ctrl)
        {
            for (Slot& s : g_slots)
                if (s.ctrl.load(std::memory_order_acquire) == ctrl) return &s;
            return nullptr;
        }

        Slot* Acquire(const void* ctrl)
        {
            if (Slot* s = Find(ctrl)) return s;
            for (Slot& s : g_slots) {
                const void* expected = nullptr;
                if (s.ctrl.compare_exchange_strong(expected, ctrl, std::memory_order_acq_rel)) return &s;
            }
            return nullptr;
        }

        /// @brief Puts the proxy where it is, so the phantom's broadphase box follows a capsule edited in place.
        /// @note  bhkCharacterProxy::SetPosition clears the proxy's manifold, as a teleport does: only after an edit.
        void RefreshBroadphase(hk::bhkCharacterController& ctrl)
        {
            hk::hkVector4 pos;
            ctrl.GetPositionImpl(pos, false);
            ctrl.SetPositionImpl(pos, false, false);
        }

        float Low(const hk::hkVector4& a, const hk::hkVector4& b) { return (std::min)(a(2), b(2)); }
        float High(const hk::hkVector4& a, const hk::hkVector4& b) { return (std::max)(a(2), b(2)); }

        void WriteCapsule(hk::hkpCapsuleShape& c, float lowZ, float highZ, float radius, const Slot& s)
        {
            const bool aLow = s.origA(2) <= s.origB(2);
            c.m_vertexA     = s.origA;
            c.m_vertexB     = s.origB;
            c.m_vertexA(2)  = aLow ? lowZ : highZ;
            c.m_vertexB(2)  = aLow ? highZ : lowZ;
            c.m_vertexA(3)  = radius;
            c.m_vertexB(3)  = radius;
            c.m_radius      = radius;
        }

        bool ChildEnabled(const hk::hkpListShape& list, int i)
        {
            return (list.m_enabledChildren[(i >> 5) & 7] & (1u << (i & 31))) != 0;
        }

        /// @brief Recomputes a list's cached box from its children's capsules (every child, enabled or not, as vanilla's
        ///        box holds them); false when a child is no capsule, the box left as it was.
        bool RecalcListBox(hk::hkpListShape& list)
        {
            float lo[3] = { 1.0e30f, 1.0e30f, 1.0e30f }, hi[3] = { -1.0e30f, -1.0e30f, -1.0e30f };
            for (int i = 0; i < list.m_childInfo.getSize(); i++) {
                const hk::hkpShape* child = list.m_childInfo[i].m_shape;
                if (!child || child->getType() != hk::HK_SHAPE_CAPSULE) return false;
                const auto* c = static_cast<const hk::hkpCapsuleShape*>(child);
                const float r = c->getRadius();
                for (int k = 0; k < 3; k++) {
                    lo[k] = (std::min)(lo[k], (std::min)(c->m_vertexA(k), c->m_vertexB(k)) - r);
                    hi[k] = (std::max)(hi[k], (std::max)(c->m_vertexA(k), c->m_vertexB(k)) + r);
                }
            }
            list.m_aabbCenter.set(0.5f * (lo[0] + hi[0]), 0.5f * (lo[1] + hi[1]), 0.5f * (lo[2] + hi[2]), 0.0f);
            list.m_aabbHalfExtents.set(0.5f * (hi[0] - lo[0]), 0.5f * (hi[1] - lo[1]), 0.5f * (hi[2] - lo[2]), 0.0f);
            return true;
        }

        /// @brief Restores the capsule (and its list's box) as the controller built it.
        void RestoreCapsule(Slot& s)
        {
            if (!s.capsule) return;
            s.capsule->m_vertexA = s.origA;
            s.capsule->m_vertexB = s.origB;
            s.capsule->m_radius  = s.origRadius;
            if (s.list) {
                s.list->m_aabbCenter      = s.origListCenter;
                s.list->m_aabbHalfExtents = s.origListHalf;
            }
        }

        /// @brief Puts the hull back as vanilla holds it and frees the slot.
        void Drop(hk::bhkCharacterController& ctrl, Slot& slot)
        {
            const float lift = slot.lift.load(std::memory_order_relaxed);
            const Mode  mode = slot.mode.load(std::memory_order_relaxed);
            if (mode == Mode::Capsule) {
                RestoreCapsule(slot);
                slot.lift.store(0.0f, std::memory_order_release);
                RefreshBroadphase(ctrl);
                if (LogBudget()) LOG_INFO("pogo: capsule restored (the ground states let go)");
            } else if (mode == Mode::Offset && lift > 0.0f) {
                hk::hkVector4 pos;
                ctrl.GetPositionImpl(pos, false);
                pos(2) -= lift;
                slot.lift.store(0.0f, std::memory_order_release);
                ctrl.SetPositionImpl(pos, false, false);
                if (LogBudget()) LOG_INFO("pogo: hull lowered {:.3f} onto the feet (the ground states let go)", lift);
            }
            slot.capsule   = nullptr;
            slot.list      = nullptr;
            slot.hullShape = nullptr;
            slot.mode.store(Mode::None, std::memory_order_release);
            slot.profileAt = -1.0e9;
            slot.ctrl.store(nullptr, std::memory_order_release);
        }

        /// @brief The capsule `ctrl`'s character file authors (Havok units, unscaled); false when it holds the default.
        /// @note  Through the actor's active graph: BShkbAnimationGraph::characterInstance -> hkbCharacter::m_setup ->
        ///        m_data, the loaded (merged) character file.
        bool CharacterCapsule(const hk::bhkCharacterController& ctrl, float& height, float& radius, float& scale)
        {
            const auto actor = ch::ManagedActor(ctrl);
            if (!actor) return false;
            scale = actor->GetScale();
            RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;
            if (!actor->GetAnimationGraphManager(manager) || !manager || manager->graphs.empty()) return false;
            const std::uint32_t active = manager->GetRuntimeData().activeGraph;
            const auto&         graph  = manager->graphs[active < manager->graphs.size() ? active : 0];
            if (!graph) return false;
            const auto* character = reinterpret_cast<const hk::hkbCharacter*>(&graph->characterInstance);
            const hk::hkbCharacterSetup* setup = character->m_setup;
            const hk::hkbCharacterData*  data  = setup ? setup->m_data.val() : nullptr;
            if (!data) return false;
            const auto& info = data->m_characterControllerInfo;
            if (std::bit_cast<std::uint32_t>(info.m_capsuleHeight) == kDefaultHeightBits &&
                std::bit_cast<std::uint32_t>(info.m_capsuleRadius) == kDefaultRadiusBits)
                return false;
            if (!(info.m_capsuleHeight > 0.0f) || !(info.m_capsuleRadius > 0.0f)) return false;
            height = info.m_capsuleHeight;
            radius = info.m_capsuleRadius;
            return true;
        }

        /// @brief Takes the hull's capsule for Mode::Capsule: the shape itself, or a list's one enabled child (whose other
        ///        children are capsules too, so its box can be recomputed); false otherwise, with why.
        bool TakeCapsule(Slot& s, const ch::Hull& hull, const char*& why)
        {
            const hk::hkpShape* shape = hull.collidable ? hull.collidable->getShape() : nullptr;
            hk::hkpListShape*   list  = nullptr;
            if (!shape) return why = "no shape", false;
            if (hull.longBody) return why = "a long body", false;
            if (shape->getType() == hk::HK_SHAPE_LIST) {
                list                      = const_cast<hk::hkpListShape*>(static_cast<const hk::hkpListShape*>(shape));
                const hk::hkpShape* found = nullptr;
                for (int i = 0; i < list->m_childInfo.getSize(); i++) {
                    const hk::hkpShape* child = list->m_childInfo[i].m_shape;
                    if (!child || child->getType() != hk::HK_SHAPE_CAPSULE) return why = "a list with a child that is no capsule", false;
                    if (!ChildEnabled(*list, i)) continue;
                    if (found) return why = "a list with more than one enabled child", false;
                    found = child;
                }
                if (!found) return why = "a list with no enabled child", false;
                shape = found;
            } else if (shape->getType() != hk::HK_SHAPE_CAPSULE) {
                return why = "neither a capsule nor a list of capsules", false;
            }
            auto* capsule = const_cast<hk::hkpCapsuleShape*>(static_cast<const hk::hkpCapsuleShape*>(shape));
            const float dx = capsule->m_vertexA(0) - capsule->m_vertexB(0), dy = capsule->m_vertexA(1) - capsule->m_vertexB(1);
            if (dx * dx + dy * dy > 1.0e-6f) return why = "a capsule that is not upright", false;
            s.hullShape  = hull.collidable->getShape();
            s.list       = list;
            if (list) s.origListCenter = list->m_aabbCenter, s.origListHalf = list->m_aabbHalfExtents;
            s.capsule    = capsule;
            s.origA      = capsule->m_vertexA;
            s.origB      = capsule->m_vertexB;
            s.origRadius = capsule->getRadius();
            s.feet       = Low(s.origA, s.origB) - s.origRadius;
            s.height     = High(s.origA, s.origB) + s.origRadius - s.feet;
            s.radius     = s.origRadius;
            s.profileAt  = -1.0e9;
            return true;
        }

        /// @brief Mode::Capsule's step: the capsule's bottom at feet + ride height, its top and radius the character's.
        float HoldCapsule(hk::bhkCharacterController& ctrl, Slot& s, const ch::Hull& hull)
        {
            hk::hkpCapsuleShape& c = *s.capsule;
            if (Now() - s.profileAt > kProfileAge) {
                s.profileAt = Now();
                float h = 0, r = 0, scale = 1;
                const bool authored = CharacterCapsule(ctrl, h, r, scale);
                const float oldH = s.height, oldR = s.radius;
                if (authored) s.height = h * scale, s.radius = r * scale;
                else          s.height = High(s.origA, s.origB) + s.origRadius - s.feet, s.radius = s.origRadius;
                if ((std::fabs(oldH - s.height) > kMinChange || std::fabs(oldR - s.radius) > kMinChange) && LogBudget())
                    LOG_INFO("pogo: capsule {:.3f} tall, radius {:.3f} ({}), the controller's own {:.3f} x {:.3f}", s.height, s.radius,
                             authored ? "the character file's" : "the controller's own", High(s.origA, s.origB) + s.origRadius - s.feet,
                             s.origRadius);
            }
            const float radius = s.radius;
            float       top    = s.feet + s.height;
            /// @note Growing is swept: under a low ceiling the top grows only as far as it fits, and tries again each step.
            const float curTop = High(c.m_vertexA, c.m_vertexB) + c.getRadius();
            if (top > curTop + kMinChange && hull.valid) {
                const float          up  = top - curTop;
                const hk::hkVector4& at  = hull.collidable->getTransform().getTranslation();
                const ch::SweepHit   hit = ch::Sweep(hull.world, hull.collidable, hk::hkVector4(at(0), at(1), at(2) + up, at(3)), 0.0f, 2.0f, kCharacters);
                if (hit.hit) top = curTop + (std::max)(up * hit.fraction - kSkin, 0.0f);
            }
            const float ride  = std::clamp(ctrl.m_stepHeight, 0.0f, (std::max)(0.0f, top - s.feet - 2.0f * radius - kMinCore));
            const float lowZ  = s.feet + ride + radius;
            const float highZ = (std::max)(lowZ + kMinCore, top - radius);
            if (std::fabs(Low(c.m_vertexA, c.m_vertexB) - lowZ) > kMinChange || std::fabs(High(c.m_vertexA, c.m_vertexB) - highZ) > kMinChange ||
                std::fabs(c.getRadius() - radius) > kMinChange) {
                const bool first = s.lift.load(std::memory_order_relaxed) == 0.0f;
                WriteCapsule(c, lowZ, highZ, radius, s);
                if (s.list) RecalcListBox(*s.list);
                s.lift.store(ride, std::memory_order_release);
                RefreshBroadphase(ctrl);
                if (first && LogBudget())
                    LOG_INFO("pogo: capsule floats {:.3f} above the feet, top {:.3f} above them (step height {:.3f})", ride, highZ + radius - s.feet,
                             ctrl.m_stepHeight);
            }
            return s.lift.load(std::memory_order_relaxed);
        }

        /// @brief Mode::Offset's step: the whole hull raised through the position seam.
        float HoldOffset(hk::bhkCharacterController& ctrl, Slot& s, const ch::Hull& hull)
        {
            const float lift = s.lift.load(std::memory_order_relaxed);
            const float up   = ctrl.m_stepHeight - lift;
            if (!(up > kMinRaise) || !hull.valid) return lift;
            /// @note The raise is swept: under a low ceiling the shape floats what fits, and tries for the rest each step.
            const hk::hkVector4& at  = hull.collidable->getTransform().getTranslation();
            const ch::SweepHit   hit = ch::Sweep(hull.world, hull.collidable, hk::hkVector4(at(0), at(1), at(2) + up, at(3)), 0.0f, 2.0f, kCharacters);
            const float          rise = hit.hit ? (std::max)(up * hit.fraction - kSkin, 0.0f) : up;
            if (rise < kMinRaise) return lift;
            hk::hkVector4 pos;
            ctrl.GetPositionImpl(pos, false);
            pos(2) += rise;
            s.lift.store(lift + rise, std::memory_order_release);
            ctrl.SetPositionImpl(pos, false, false);
            if (LogBudget())
                LOG_INFO("pogo: hull floated {:.3f} above the feet (step height {:.3f}{}; not a single capsule, so it floats whole)", lift + rise,
                         ctrl.m_stepHeight, hit.hit ? ", a ceiling stopped it" : "");
            return lift + rise;
        }

        bool OffsetMode(const Slot* s) { return s && s->mode.load(std::memory_order_acquire) == Mode::Offset; }

        void OnGetPosition(GetPosition::Next next, const hk::bhkCharacterController* self, hk::hkVector4& pos, bool applyCenterOffset)
        {
            next(self, pos, applyCenterOffset);
            if (!applyCenterOffset) return;
            if (const Slot* s = Find(self); OffsetMode(s)) pos(2) -= s->lift.load(std::memory_order_acquire);
        }

        void OnSetPosition(SetPosition::Next next, hk::bhkCharacterController* self, const hk::hkVector4& pos, bool applyCenterOffset,
                           bool moveByVelocity)
        {
            const Slot* s    = applyCenterOffset ? Find(self) : nullptr;
            const float lift = OffsetMode(s) ? s->lift.load(std::memory_order_acquire) : 0.0f;
            if (lift == 0.0f) return next(self, pos, applyCenterOffset, moveByVelocity);
            hk::hkVector4 raised = pos;
            raised(2) += lift;
            next(self, raised, applyCenterOffset, moveByVelocity);
        }

        bool OnIntegrate(Integrate::Next next, hk::bhkCharacterController* self)
        {
            if (Slot* s = self ? Find(self) : nullptr)
                if (!s->claimed.exchange(false, std::memory_order_acq_rel)) Drop(*self, *s);
            return next(self);
        }

        /// @note Priority::First on the integrate: outermost, so the hull is back before the step query or anything else
        ///       sees the step.
        [[maybe_unused]] const bool s_get = GetPosition::Subscribe(&OnGetPosition, platform::hooks::Priority::Normal, "CharPogo", "char_pogo");
        [[maybe_unused]] const bool s_set = SetPosition::Subscribe(&OnSetPosition, platform::hooks::Priority::Normal, "CharPogo", "char_pogo");
        [[maybe_unused]] const bool s_integrate =
            Integrate::Subscribe(&OnIntegrate, platform::hooks::Priority::First, "CharPogo", "char_pogo");

    }  // namespace

    namespace controller {

        float Lift(const hk::bhkCharacterController& ctrl)
        {
            const Slot* s = Find(&ctrl);
            return s ? s->lift.load(std::memory_order_acquire) : 0.0f;
        }

        float RaiseLift(hk::bhkCharacterController& ctrl, const Hull& hull)
        {
            Slot* s = Acquire(&ctrl);
            if (!s) return 0.0f;
            s->claimed.store(true, std::memory_order_release);
            /// @note The controller swapped its shape (its second slot, the console's ToggleCharControllerShape): the edit
            ///       goes back onto the capsule it was made on, and the new shape is taken afresh.
            if (s->mode.load(std::memory_order_relaxed) == Mode::Capsule && hull.collidable && hull.collidable->getShape() != s->hullShape) {
                RestoreCapsule(*s);
                s->capsule   = nullptr;
                s->list      = nullptr;
                s->hullShape = nullptr;
                s->lift.store(0.0f, std::memory_order_release);
                s->mode.store(Mode::None, std::memory_order_release);
                if (LogBudget()) LOG_INFO("pogo: the controller changed its shape; the old capsule is restored");
            }
            if (s->mode.load(std::memory_order_relaxed) == Mode::None) {
                const char* why   = "";
                const bool  taken = TakeCapsule(*s, hull, why);
                s->mode.store(taken ? Mode::Capsule : Mode::Offset, std::memory_order_release);
                if (LogBudget())
                    LOG_INFO("pogo: the hull is {}", taken ? (s->list ? std::string("a list shape; its enabled capsule is edited in place")
                                                                      : std::string("a capsule; edited in place"))
                                                           : std::string(why) + ", so it floats whole");
            }
            return s->mode.load(std::memory_order_relaxed) == Mode::Capsule ? HoldCapsule(ctrl, *s, hull) : HoldOffset(ctrl, *s, hull);
        }

    }  // namespace controller

}  // namespace CB::feature
