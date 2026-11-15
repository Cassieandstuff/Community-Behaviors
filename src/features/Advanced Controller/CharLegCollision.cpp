#include "PCH.h"

#include "CharGround.h"

#include <platform/hooks/Override.h>

#include <Common/Base/hkBase.h>
#include <Physics/Dynamics/Entity/hkpRigidBody.h>
#include <Physics/Dynamics/Motion/hkpMotion.h>
#include <Physics/Dynamics/World/hkpWorld.h>
#include <BSHavok/bhkCharProxyController.h>
#include <BSHavok/bhkRigidBody.h>
#include <BSHavok/bhkWorld.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <string_view>
#include <vector>

/// @file
/// @brief The player's leg bones push floor clutter (char_leg_collision, `[Character] bLegCollision`): while the player
///        is on foot, the thigh, calf and foot bodies of the ragdoll go on BipedNoCC (layer 33) and into the Havok world,
///        still keyframed, so the animation sweeps them through buckets, baskets and props.
/// @note  Vanilla's own path, narrowed to the legs (SkyrimSE 1.6.1170): FUN_14065d590 gives an actor whose race has
///        kAllowRagdollCollision (flag bit 18) BipedNoCC over its whole 3D, and bhkRigidBody's add to the world
///        (0x140e928a0) keeps a keyframed biped body out unless its layer is 0x21. In the world, each bone's
///        bhkBlendCollisionObject (blend strength 1) drives its body to the animated node every update (0x140ec7090).
///        BipedNoCC's row (0x140eb6fb0) is clutter, weapon, projectile, spell, props, CharController and ConeProjectile:
///        no static or terrain, so a dynamic body on it falls through the floor.
/// @note  The bodies keep the player's system group, so the player's own proxy, the dynamic mover's body and every query
///        cast with the hull's filter info ignore them; NPC controllers, arrows and spells meet them.
/// @note  Ragdoll hand-back: bhkRigidBody::SetMotionType (0x140e92b20) re-filters the body in the world (it calls
///        hkpRigidBody::setMotionType with a full filter check), so the hook below puts an owned body back on its own
///        layer just before vanilla makes it dynamic.

namespace CB::feature::controller::legs {

    namespace {

        using Integrate = platform::hooks::Override<&::bhkCharProxyController::Unk_0B>;
        using SetMotion = platform::hooks::Override<&::bhkRigidBody::SetMotionType>;

        constexpr ::hkUint32 kLayerMask = 0x7f;
        constexpr ::hkUint32 kBiped     = 8;
        constexpr ::hkUint32 kBipedNoCC = 33;
        constexpr ::hkUint32 kChaseOrRagdoll = 0x1800;   ///< m_flags: kChaseBip | kFollowRagdoll, vanilla's to run

        /// @brief The bones whose bodies kick, by their node names in the vanilla and XPMSSE skeletons.
        /// @note Compared byte-exact (CB never folds case); the foot names end in a space inside the brackets.
        constexpr std::array<std::string_view, 6> kBones = {
            "NPC L Thigh [LThg]", "NPC R Thigh [RThg]", "NPC L Calf [LClf]",
            "NPC R Calf [RClf]",  "NPC L Foot [Lft ]",  "NPC R Foot [Rft ]",
        };
        constexpr std::size_t kCount = kBones.size();

        std::atomic<std::uint32_t> g_logs{ 0 };
        bool LogBudget() { return g_logs.fetch_add(1, std::memory_order_relaxed) < 200; }

        /// @brief The player's leg bodies.
        /// @invariant Only the controller's step (the game thread) writes `root` and `wrapper` and publishes `owner`;
        ///            the motion hook (any thread) reads only `before` and `ours`, after taking `owner`. Whoever takes
        ///            `owner` from a body back to null (a compare-exchange) restores it.
        struct Slot {
            RE::NiPointer<RE::NiAVObject>                         root;       ///< held, so a new 3D never reuses its address
            std::array<RE::NiPointer<RE::bhkWorldObject>, kCount> wrapper;    ///< keeps each body alive while held
            std::array<std::atomic<::hkUint32>, kCount>           before{};   ///< its filter info before CB changed it
            std::array<std::atomic<::hkUint32>, kCount>           ours{};     ///< the filter info CB wrote
            std::array<std::atomic<::bhkRigidBody*>, kCount>      owner{};    ///< set while CB owns the body
            std::uint32_t                                         rescan = 0;
            std::uint32_t                                         tries  = 0;   ///< rescans of this 3D so far
        };

        Slot& S()
        {
            static Slot s;
            return s;
        }

        /// @brief Body `i`'s Havok body, read from its wrapper each time (vanilla may rebuild what a wrapper holds).
        ::hkpRigidBody* Body(const Slot& s, std::size_t i)
        {
            const RE::bhkWorldObject* wo = s.wrapper[i].get();
            return wo ? reinterpret_cast<::hkpRigidBody*>(wo->referencedObject.get()) : nullptr;
        }

        ::bhkRigidBody* Wrapper(const Slot& s, std::size_t i) { return reinterpret_cast<::bhkRigidBody*>(s.wrapper[i].get()); }

        /// @brief Whether the player is on foot and animated: not dead, ragdolled, knocked, mounted, swimming, sitting,
        ///        flying, or left to vanilla's chase/ragdoll states.
        bool OnFoot(RE::PlayerCharacter& player, const ::bhkCharacterController& ctrl)
        {
            if (ctrl.m_flags & kChaseOrRagdoll) return false;
            const auto* state = player.AsActorState();
            return !player.IsDead() && !player.IsInRagdollState() && !player.IsOnMount() && !state->IsSwimming() &&
                   state->GetSitSleepState() == RE::SIT_SLEEP_STATE::kNormal && state->GetKnockState() == RE::KNOCK_STATE_ENUM::kNormal &&
                   state->GetFlyState() == RE::FLY_STATE::kNone;
        }

        /// @brief Puts `body` back on `before`'s layer in place, keeping whatever else vanilla set since.
        void RestoreLayer(::hkpRigidBody* body, ::hkUint32 before)
        {
            const ::hkUint32 now = body->getCollisionFilterInfo();
            body->setCollisionFilterInfo((now & ~kLayerMask) | (before & kLayerMask));
        }

        /// @brief Writes `info` on a body that may sit in a world, and has that world re-run its filter.
        /// @pre The game thread, outside the physics step. The bhkWorld lock is re-entrant for its writer, so
        ///      UpdateCollisionFilter's own lock nests inside this one.
        void Refilter(::bhkRigidBody* bhk, ::hkpRigidBody* body, ::hkUint32 info)
        {
            ::hkpWorld* hk = body->getWorld();
            if (!hk) {
                body->setCollisionFilterInfo(info);
                return;
            }
            const ::bhkWorldLock wl(hk, 1);
            body->setCollisionFilterInfo(info);
            bhk->UpdateCollisionFilter();
        }

        /// @brief Gives body `i` back: out of the world when it is still keyframed there, on its own layer.
        /// @pre Called on the game thread, outside the physics step, with no world lock held.
        void Release(Slot& s, std::size_t i)
        {
            ::bhkRigidBody* bhk = s.owner[i].load(std::memory_order_acquire);
            if (!bhk || !s.owner[i].compare_exchange_strong(bhk, nullptr, std::memory_order_acq_rel)) return;
            ::hkpRigidBody*  body = Body(s, i);
            const ::hkUint32 ours = s.ours[i].load(std::memory_order_relaxed);
            /// @note Vanilla re-layered it since (a mount gives the rider BipedNoCC under the horse's group, death gives
            ///       DeadBip): it is vanilla's now, left as it is.
            if (!body || body->getCollisionFilterInfo() != ours) return;
            const ::hkUint32 back = (ours & ~kLayerMask) | (s.before[i].load(std::memory_order_relaxed) & kLayerMask);
            ::hkpWorld*      hk   = body->getWorld();
            if (hk && body->getMotionType() == ::hkpMotion::MOTION_KEYFRAMED) {
                const ::bhkWorldLock wl(hk, 1);
                ::hkpEntity* e = body;
                hk->removeEntityBatch(&e, 1);
                body->setCollisionFilterInfo(back);
            } else {
                Refilter(bhk, body, back);
            }
        }

        void ReleaseAll(Slot& s, const char* why)
        {
            bool any = false;
            for (std::size_t i = 0; i < kCount; ++i) {
                any |= s.owner[i].load(std::memory_order_acquire) != nullptr;
                Release(s, i);
            }
            if (any && LogBudget()) LOG_INFO("leg collision: legs handed back ({})", why);
        }

        /// @brief Finds the leg bodies under `root`, by exact node name, into the slots still empty.
        /// @param fresh a new 3D: the old legs are handed back and every slot is emptied first.
        void Scan(Slot& s, RE::NiAVObject* root, bool fresh)
        {
            if (fresh) {
                ReleaseAll(s, "the player's 3D changed");
                s.root.reset(root);
                s.rescan = 0, s.tries = 0;
                for (auto& w : s.wrapper) w.reset();
            }
            if (!root) return;
            std::vector<RE::NiAVObject*> stack{ root };
            while (!stack.empty()) {
                RE::NiAVObject* node = stack.back();
                stack.pop_back();
                if (!node) continue;
                const char* name = node->name.c_str();
                for (std::size_t i = 0; name && i < kCount; ++i) {
                    if (s.wrapper[i] || std::string_view(name) != kBones[i]) continue;
                    auto* co = node->GetCollisionObject();
                    RE::bhkWorldObject* wo = co ? co->body.get() : nullptr;
                    if (!wo || !wo->AsBhkRigidBody() || !wo->referencedObject.get()) continue;
                    s.wrapper[i] = RE::NiPointer<RE::bhkWorldObject>(wo);
                }
                if (auto* n = node->AsNode()) {
                    for (auto& child : n->GetChildren()) stack.push_back(child.get());
                }
            }
            std::size_t found = 0;
            for (const auto& w : s.wrapper) found += w ? 1 : 0;
            if (fresh && LogBudget()) LOG_INFO("leg collision: {} of {} leg bodies found in the player's 3D", found, kCount);
        }

        /// @brief Puts body `i` on BipedNoCC, keyframed, in the player's world: added when it sits outside, re-filtered
        ///        in place when vanilla left it in a world on Biped (a ragdoll blend that ended, a controller rebuild).
        /// @note `owner` is published before the layer is written, so a motion change in between still reaches the hook.
        void Engage(Slot& s, std::size_t i, ::hkpWorld* hk)
        {
            ::hkpRigidBody* body = Body(s, i);
            if (!body || body->getMotionType() != ::hkpMotion::MOTION_KEYFRAMED) return;
            const ::hkUint32 info = body->getCollisionFilterInfo();
            if ((info & kLayerMask) != kBiped) return;
            ::bhkRigidBody*  bhk  = Wrapper(s, i);
            const ::hkUint32 ours = (info & ~kLayerMask) | kBipedNoCC;
            s.before[i].store(info, std::memory_order_relaxed);
            s.ours[i].store(ours, std::memory_order_relaxed);
            ::bhkRigidBody* none = nullptr;
            if (!s.owner[i].compare_exchange_strong(none, bhk, std::memory_order_acq_rel)) return;
            if (body->getWorld()) {
                Refilter(bhk, body, ours);
                return;
            }
            body->setCollisionFilterInfo(ours);
            const ::bhkWorldLock wl(hk, 1);
            ::hkpEntity* e = body;
            hk->addEntityBatch(&e, 1, ::HK_ENTITY_ACTIVATION_DO_ACTIVATE);
        }

        /// @brief Drops ownership of body `i` when vanilla took it out of the world, changed its filter, or made it
        ///        anything but keyframed (a route past bhkRigidBody::SetMotionType, which the hook does not see).
        /// @note A body vanilla moved into another world on CB's layer (a cell change) stays owned: the motion hook must
        ///       still see it.
        void Check(Slot& s, std::size_t i)
        {
            ::bhkRigidBody* bhk = s.owner[i].load(std::memory_order_acquire);
            if (!bhk) return;
            ::hkpRigidBody*  body      = Body(s, i);
            const ::hkUint32 ours      = s.ours[i].load(std::memory_order_relaxed);
            const bool       keyframed = body && body->getMotionType() == ::hkpMotion::MOTION_KEYFRAMED;
            if (keyframed && body->getWorld() && body->getCollisionFilterInfo() == ours) return;
            if (!s.owner[i].compare_exchange_strong(bhk, nullptr, std::memory_order_acq_rel)) return;
            if (!body || body->getCollisionFilterInfo() != ours) return;
            /// @note Out of the world (MoveHavok taking the 3D out) or no longer keyframed, on CB's layer: put back where
            ///       vanilla expects it, so a dynamic leg meets the floor and the next step can take a keyframed one again.
            Refilter(bhk, body, (ours & ~kLayerMask) | (s.before[i].load(std::memory_order_relaxed) & kLayerMask));
        }

        void Step(::bhkCharacterController& ctrl)
        {
            Slot& s      = S();
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) return;
            RE::NiAVObject* root = player->Get3D(false);
            if (root != s.root.get()) Scan(s, root, true);
            ::bhkWorld* world = ctrl.GetHavokWorld();
            ::hkpWorld* hk    = world ? world->GetWorld2() : nullptr;
            if (!root || !hk || !OnFoot(*player, ctrl)) {
                ReleaseAll(s, "the player left foot");
                return;
            }
            /// @note A 3D whose collision was not built yet when it was first seen is scanned again about every second, ten
            ///       times at most (a skeleton may simply have no body on a bone).
            bool missing = false;
            for (const auto& w : s.wrapper) missing |= !w;
            if (missing && s.tries < 10 && ++s.rescan >= 60) {
                s.rescan = 0, ++s.tries;
                Scan(s, root, false);
            }
            for (std::size_t i = 0; i < kCount; ++i) {
                Check(s, i);
                if (!s.owner[i].load(std::memory_order_acquire)) Engage(s, i, hk);
            }
        }

        /// @note Runs after vanilla's integrate, on the controller's update: outside the physics step, where CB's other
        ///       controller code takes the world lock too.
        bool OnIntegrate(Integrate::Next next, ::bhkCharacterController* self)
        {
            const bool result = next(self);
            if (self && IsPlayer(self)) Step(*self);
            return result;
        }

        /// @note May run on any thread; it never blocks. Only a body CB owns, leaving keyframed, is touched: its layer
        ///       goes back before vanilla's call re-filters it, so a ragdolled leg lands on the floor.
        /// @note Vanilla may then refuse the change (it returns false for some keyframed bodies); the body stays keyframed
        ///       on Biped and the next step takes it again.
        bool OnSetMotion(SetMotion::Next next, ::bhkRigidBody* self, ::hkUint32 type)
        {
            if (self && type != static_cast<::hkUint32>(::hkpMotion::MOTION_KEYFRAMED)) {
                Slot& s = S();
                for (std::size_t i = 0; i < kCount; ++i) {
                    ::bhkRigidBody* own = self;
                    if (s.owner[i].load(std::memory_order_acquire) != self) continue;
                    if (!s.owner[i].compare_exchange_strong(own, nullptr, std::memory_order_acq_rel)) continue;
                    /// @note The body from `self`, not the slot: the game thread may be rescanning the slot.
                    auto* body = reinterpret_cast<::hkpRigidBody*>(self->m_referencedObject);
                    if (body && body->getCollisionFilterInfo() == s.ours[i].load(std::memory_order_relaxed))
                        RestoreLayer(body, s.before[i].load(std::memory_order_relaxed));
                }
            }
            return next(self, type);
        }

        [[maybe_unused]] const bool s_integrate =
            Integrate::Subscribe(&OnIntegrate, platform::hooks::Priority::Normal, "CharLegCollision", "char_leg_collision");
        [[maybe_unused]] const bool s_motion =
            SetMotion::Subscribe(&OnSetMotion, platform::hooks::Priority::Normal, "CharLegCollision", "char_leg_collision");

    }  // namespace

}  // namespace CB::feature::controller::legs
