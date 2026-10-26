#include "PCH.h"

#include "CharGround.h"

#include <core/debug/Settings.h>
#include <platform/hooks/hookslib.h>
#include <platform/hooks/Live.h>
#include <platform/hooks/Override.h>

#include <Physics/Collide/Shape/Compound/Collection/List/hkpListShape.h>
#include <Physics/Collide/Shape/Convex/Capsule/hkpCapsuleShape.h>
#include <Physics/ConstraintSolver/Simplex/hkpSimplexSolver.h>
#include <Physics/Dynamics/Entity/hkpRigidBody.h>
#include <Physics/Dynamics/Phantom/hkpShapePhantom.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharProxyController.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterCollisionHandler.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCollisionFilter.h>
#include <Physics/Utilities/CharacterControl/Bethesda/Setting.h>
#include <Physics/Utilities/CharacterControl/CharacterProxy/hkpCharacterProxy.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>

/// @file
/// @brief Step 4 of the character controller spec: the proxy's constraint planes, replacing Bethesda's
///        processConstraintsCallback for the managed controller (the player's proxy).
/// @note Ported from the matched bhkCharProxyController::processConstraintsCallback (havok/.../Bethesda), with
///       the spec's rules in place of its per-contact walkability, TryStep and wall planes: blocking follows the
///       contact normal only (M1/M2), walkability comes from one floor query per callback, and a steep obstacle above
///       the feet gets one vertical plane per body (Jolt CharacterVirtual's rule). Its gameplay side effects (hurtful
///       bodies, character pushes and collision handler, footstep material, support body, orientation controller, the
///       animated-static and stuck-quad flags) are kept.
/// @warning The callback also runs inside the proxy's checkSupport, which is the call whose bit 9 / m_onlyWallContacts
///          UpdatePhysics reads; the integrate calls only shape the motion.

namespace CB::feature {

    namespace {

        namespace hk  = CB::havok;
        namespace ch  = CB::feature::controller;

        using Process = platform::hooks::Override<&hk::bhkCharProxyController::processConstraintsCallback>;

        constexpr hk::hkUint32 kTryStep          = 1u << 2;
        constexpr hk::hkUint32 kStuckQuad        = 1u << 5;
        constexpr hk::hkUint32 kAnimStaticContact = 0x20000000u;
        constexpr hk::hkUint32 kOtherSneakPass   = 0x08000000u;
        constexpr float        kAntiCreep        = 5.7700005f;   ///< Bethesda's extraDown on walkable contacts
        constexpr float        kSteepMinZ        = 1.0e-3f;
        constexpr float        kPenetrationScale = 1.25f;
        constexpr int          kMaxPoints        = 64;
        constexpr int          kMaxVertical      = 4;            ///< Bethesda's cap; the proxy reserves m_userPlanes (4)

        /// @brief How a contact's plane is treated after the layer rules.
        enum class Kind : std::uint8_t { Block, Zeroed, Character };

        /// @brief Bethesda's flatten, as the matched callback does it: z = 0, then normalize3IfNotZero.
        void Flatten(hk::hkVector4& plane)
        {
            plane(2) = 0.0f;
            plane.normalize3IfNotZero();
        }

        void Zero(hk::hkSurfaceConstraintInfo& c)
        {
            c.m_plane.set(0.0f, 0.0f, 0.0f, 0.0f);
            c.m_velocity.set(0.0f, 0.0f, 0.0f, 0.0f);
        }

        std::atomic<std::uint32_t> g_logged{ 0 };
        constexpr std::uint32_t    kMaxLogged = 4000;

        /// @brief One line per change of (contacts, zeroed, planes added, support contacts, verdict, bits).
        /// @note The player runs on one thread, so the last signature is plain.
        void Log(int contacts, int zeroed, int characters, int vertical, int penetration, int support, const ch::FloorVerdict& v, hk::hkUint32 flags)
        {
            static std::uint64_t last = ~0ull;
            const std::uint64_t  sig  = static_cast<std::uint64_t>(contacts) | (static_cast<std::uint64_t>(zeroed) << 8)
                                    | (static_cast<std::uint64_t>(characters) << 16) | (static_cast<std::uint64_t>(vertical) << 24)
                                    | (static_cast<std::uint64_t>(penetration) << 32) | (static_cast<std::uint64_t>(support) << 40)
                                    | (static_cast<std::uint64_t>(v.ground) << 48) | (static_cast<std::uint64_t>((flags >> 9) & 3) << 56);
            if (sig == last) return;
            last = sig;
            const std::uint32_t n = g_logged.fetch_add(1, std::memory_order_relaxed);
            if (n > kMaxLogged) return;
            if (n == kMaxLogged) {
                LOG_INFO("planes: {} lines logged, stopping", kMaxLogged);
                return;
            }
            LOG_INFO("planes: contacts {} (zeroed {}, characters {}, penetration-dropped {}) vertical planes +{} support contacts {} "
                     "-> {} (support hits {}, walkable {}) n=({:.2f},{:.2f},{:.2f}) bit9 {} canJump {}",
                     contacts, zeroed, characters, penetration, vertical, support, ch::Name(v.ground), v.supportHits, v.walkableHits,
                     v.groundNx, v.groundNy, v.groundNz, (flags >> 9) & 1, (flags >> 10) & 1);
        }

        using CheckSupport = platform::hooks::Override<&hk::bhkCharProxyController::CheckSupportImpl>;

        /// @brief Publishes the raw floor verdict before the proxy's integrate (see ch::PublishVerdict); the ground
        ///        states, when on, publish their decided mode after it each step.
        void OnCheckSupport(CheckSupport::Next next, hk::bhkCharacterController* self)
        {
            if (ch::IsManaged(*self)) {
                /// @note Samples the graph for the planes (ch::GraphMovesPublished), outside the integrate.
                if (const auto actor = ch::ManagedActor(*self)) ch::GraphMovesActor(actor.get());
                const ch::Hull hull = ch::GetHull(*self);
                if (hull.valid) ch::PublishVerdict(*self, ch::QueryFloor(*self, hull, ch::FloorTunables{}));
            }
            next(self);
        }

        /// @brief The spec's constraint planes for the managed controller; any other controller runs Bethesda's.
        void OnProcess(Process::Next next, hk::bhkCharProxyController* self, const hk::hkpCharacterProxy* proxy,
                       const hk::hkArray<hk::hkRootCdPoint>& manifold, hk::hkSimplexSolverInput& input)
        {
            hk::bhkCharacterController& ctrl = *self;
            const int numPoints = (std::min)(input.m_numConstraints, manifold.getSize());
            /// @note While the graph moves the player (a synced clip onto its mark), Bethesda's planes run, its character
            ///       contacts included.
            if (!ch::IsManaged(ctrl) || !proxy || numPoints > kMaxPoints || ch::GraphMovesPublished(ctrl))
                return next(self, proxy, manifold, input);
            const ch::Hull hull = ch::GetHull(ctrl);
            if (!hull.valid) return next(self, proxy, manifold, input);

            const ch::FloorTunables tun;
            ch::FloorVerdict        verdict;
            if (!ch::PublishedVerdict(ctrl, verdict)) return next(self, proxy, manifold, input);
            const bool supported = ch::Supported(verdict);

            ctrl.ResetContactState();
            ctrl.m_supportNorm.set(0.0f, 0.0f, 0.0f, 0.0f);
            ctrl.m_flags &= ~kTryStep;
            if (ctrl.IsFlagSet(16)) ctrl.SetSupportBody(nullptr);
            ctrl.m_supportCount = 0;

            /// @note Bethesda's stuck-quad test (kStuckQuad when a contact is near a 3-shape quadruped list's capsule tops),
            ///       kept as is.
            hk::hkVector4 capsuleTopA, capsuleTopB;
            float         capsuleRadiusSq = 0.0f;
            bool          haveCapsules    = false;
            if ((ctrl.m_flags & 0x80020) == 0x80000) {
                hk::bhkCachingShapePhantom* bp      = self->m_shapePhantom.get();
                const hk::hkpShapePhantom*  phantom = bp ? bp->GetPhantom() : nullptr;
                hk::bhkShape*               s0      = ctrl.m_shapes[0].get();
                const hk::hkpShape*         shape   = s0 ? s0->GetShape() : nullptr;
                if (phantom && shape && shape->getType() == hk::HK_SHAPE_LIST) {
                    const auto* list = static_cast<const hk::hkpListShape*>(shape);
                    if (list->m_childInfo.getSize() == 3) {
                        const hk::hkpShape* a = list->m_childInfo[1].m_shape;
                        const hk::hkpShape* b = list->m_childInfo[2].m_shape;
                        if (a && a->getType() == hk::HK_SHAPE_CAPSULE && b && b->getType() == hk::HK_SHAPE_CAPSULE) {
                            const auto*   ca   = static_cast<const hk::hkpCapsuleShape*>(a);
                            const auto*   cb   = static_cast<const hk::hkpCapsuleShape*>(b);
                            hk::hkVector4 topA = ca->m_vertexA;
                            topA(2) += ca->getRadius();
                            capsuleTopA._setTransformedPos(phantom->getTransform(), topA);
                            hk::hkVector4 topB = cb->m_vertexA;
                            topB(2) += cb->getRadius();
                            capsuleTopB._setTransformedPos(phantom->getTransform(), topB);
                            haveCapsules    = true;
                            capsuleRadiusSq = cb->getRadius() * cb->getRadius() * 0.110889f;
                        }
                    }
                }
            }

            const float keep       = proxy->m_keepDistance;
            const float perch      = hull.radius * tun.perchFraction;
            const float moveLimit  = platform::hooks::Live<&hk::g_fMoveLimitMass>();
            const hk::bhkCollisionFilter* filter = platform::hooks::Live<&hk::bhkCollisionFilter::s_singleton>();

            std::array<Kind, kMaxPoints> kind{};
            bool animStatic = false;
            int  zeroed = 0, characters = 0;
            for (int i = 0; i < numPoints; i++) {
                hk::hkSurfaceConstraintInfo& c     = input.m_constraints[i];
                const hk::hkRootCdPoint&     point = manifold[i];
                hk::hkpRigidBody* const      body  = hk::hkpGetRigidBody(point.m_rootCollidableB);
                const hk::hkUint32           layer = point.m_rootCollidableB->getCollisionFilterInfo() & 0x7f;
                if ((ctrl.m_flags & 0x210000) == 0x210000 && hk::bhkCharacterController::IsHurtfulBody(body))
                    ctrl.ProcessHurtfulBody(body, &point.m_contact);
                animStatic |= (layer == 2);
                /// @note M3: no static friction; the anti-creep extraDown is granted below to supporting contacts only
                ///       (Bethesda's 0.577 on animated statics is dropped with the rest).
                c.m_staticFriction          = 0.0f;
                c.m_extraUpStaticFriction   = 0.0f;
                c.m_extraDownStaticFriction = 0.0f;
                kind[i] = Kind::Block;

                if (body) {
                    if (layer == ch::kCharControllerLayer) {
                        /// @note M9: another character blocks as a flattened plane and never supports.
                        kind[i] = Kind::Character;
                        hk::bhkCharacterController* other = hk::bhkCharacterController::GetControllerFromCollidable(body);
                        if (other) {
                            Flatten(c.m_plane);
                            ctrl.PushCharacter(other, point.m_contact);
                            if (auto* handler = platform::hooks::Live<&hk::bhkCharacterCollisionHandler::s_singleton>())
                                handler->HandleCharacterCollision(&ctrl, other);
                        }
                    } else if (hk::bhkIsDynamicMotionType(body->getMotionType())) {
                        /// @note M10: a light body neither blocks nor supports; a heavy one blocks and Havok's object
                        ///       interaction pushes it. Bethesda's forced up normal (standing on clutter) is gone.
                        if (body->getMass() < moveLimit) {
                            Zero(c);
                            kind[i] = Kind::Zeroed;
                        }
                    } else if (layer != 2 && layer != 4 && layer != 5 && layer != 6) {
                        if (haveCapsules && !ctrl.IsFlagSet(5)) {
                            hk::hkVector4 toA, toB;
                            toA.setSub4(point.m_contact.m_position, capsuleTopA);
                            toB.setSub4(point.m_contact.m_position, capsuleTopB);
                            if (toA.lengthSquared3().getReal() < capsuleRadiusSq || toB.lengthSquared3().getReal() < capsuleRadiusSq)
                                ctrl.m_flags |= kStuckQuad;
                        }
                    }
                } else if (filter && layer < 64 && (filter->m_sensorField & (1ull << layer))) {
                    Zero(c);
                    kind[i] = Kind::Zeroed;
                } else if (layer == 14) {
                    if (ctrl.IsFlagSet(16)) c.m_velocity.set(0.0f, 0.0f, 0.0f, 0.0f);
                } else if (layer == ch::kCharControllerLayer) {
                    hk::bhkCharacterController* other =
                        hk::bhkCharacterController::GetControllerFromCollidable(static_cast<const hk::hkpWorldObject*>(point.m_rootCollidableB->getOwner()));
                    if (other && (other->m_flags & kOtherSneakPass)) {
                        Zero(c);
                        kind[i] = Kind::Zeroed;
                    } else {
                        Flatten(c.m_plane);
                        kind[i] = Kind::Character;
                    }
                }
                if (kind[i] == Kind::Zeroed) ++zeroed;
                if (kind[i] == Kind::Character) ++characters;
            }

            /// @note M11 (Jolt): two penetrating contacts on one body with opposing normals are an internal edge or a
            ///       pinch; the shallower is dropped so the pair cannot lock the character in place.
            int penetration = 0;
            const float deep = -kPenetrationScale * keep;
            for (int i = 0; i < numPoints; i++) {
                if (kind[i] != Kind::Block) continue;
                const hk::hkContactPoint& ci = manifold[i].m_contact;
                if (!(ci.m_separatingNormal(3) < deep)) continue;
                for (int j = i + 1; j < numPoints; j++) {
                    if (kind[j] != Kind::Block || manifold[j].m_rootCollidableB != manifold[i].m_rootCollidableB) continue;
                    const hk::hkContactPoint& cj = manifold[j].m_contact;
                    if (!(cj.m_separatingNormal(3) < deep) || ci.m_separatingNormal.dot3(cj.m_separatingNormal).getReal() >= 0.0f) continue;
                    const int shallow = ci.m_separatingNormal(3) > cj.m_separatingNormal(3) ? i : j;
                    Zero(input.m_constraints[shallow]);
                    kind[shallow] = Kind::Zeroed;
                    ++penetration;
                    if (shallow == i) break;
                }
            }

            /// @note Support is the floor query's verdict; the contacts inside the supporting region (under the feet, by
            ///       position) are the ones it is "on": they carry the anti-creep friction, the footstep material, the
            ///       support body and the orientation controller's notice, as Bethesda's walkable contacts did.
            /// @note The anti-creep friction scales with the verdict's support, so near the max slope it no longer holds
            ///       the body against the slide velocity it is meant to give way to.
            int support = 0;
            if (supported) {
                for (int i = 0; i < numPoints; i++) {
                    if (kind[i] != Kind::Block) continue;
                    const hk::hkRootCdPoint& point = manifold[i];
                    const hk::hkVector4&     pos   = point.m_contact.m_position;
                    if (point.m_contact.m_separatingNormal(2) <= kSteepMinZ || ch::SupportAxisDistSq(hull, pos(0), pos(1)) > perch * perch
                        || std::fabs(pos(2) - hull.feetZ) > tun.footTolerance)
                        continue;
                    if (!ctrl.IsFlagSet(3)) input.m_constraints[i].m_extraDownStaticFriction = kAntiCreep * verdict.support;
                    ++support;
                    if (!ctrl.IsFlagSet(16) && ctrl.m_orientationCtrl) ctrl.m_orientationCtrl->OnSupportContact(point);
                    ctrl.UpdateContactMaterial(point);
                    if (ctrl.IsFlagSet(16)) ctrl.SetSupportBody(hk::hkpGetRigidBody(point.m_rootCollidableB));
                }
                if (!support && ctrl.IsFlagSet(16) && verdict.supportBody)
                    ctrl.SetSupportBody(hk::hkpGetRigidBody(verdict.supportBody));
            }

            /// @note Jolt's steep-slope rule: a face too steep to walk, above the feet, also blocks as a vertical plane
            ///       (one per body, its nearest contact) so the solver cannot climb it; the step query handles stepping.
            ///       Bethesda's flag 11 (no wall planes) still suppresses them.
            int vertical = 0;
            if (!ctrl.IsFlagSet(11)) {
                const int cap = (std::min)(kMaxVertical, static_cast<int>(proxy->m_userPlanes));
                std::array<int, kMaxPoints> pick{};
                int picks = 0;
                for (int i = 0; i < numPoints; i++) {
                    if (kind[i] != Kind::Block) continue;
                    const hk::hkContactPoint& cp = manifold[i].m_contact;
                    const float               nz = cp.m_separatingNormal(2);
                    if (nz <= kSteepMinZ || nz >= ch::WalkableZ(ctrl) || cp.m_position(2) <= hull.feetZ + tun.footTolerance) continue;
                    int k = 0;
                    while (k < picks && manifold[pick[k]].m_rootCollidableB != manifold[i].m_rootCollidableB) ++k;
                    if (k == picks) pick[picks++] = i;
                    else if (cp.m_separatingNormal(3) < manifold[pick[k]].m_contact.m_separatingNormal(3)) pick[k] = i;
                }
                for (int k = 0; k < picks && vertical < cap; k++) {
                    const hk::hkSurfaceConstraintInfo& src = input.m_constraints[pick[k]];
                    hk::hkSurfaceConstraintInfo&       v   = input.m_constraints[input.m_numConstraints];
                    v                                      = src;
                    Flatten(v.m_plane);
                    if (v.m_plane(0) == 0.0f && v.m_plane(1) == 0.0f) continue;
                    v.m_velocity.set(src.m_velocity(0), src.m_velocity(1), 0.0f, 0.0f);
                    v.m_extraDownStaticFriction = 0.0f;
                    input.m_numConstraints++;
                    ++vertical;
                }
            }

            if (animStatic) ctrl.m_flags |= kAnimStaticContact;
            else            ctrl.m_flags &= ~kAnimStaticContact;
            /// @note UpdatePhysics' kSupport = (surface SUPPORTED && !m_onlyWallContacts) || bit 9: supported ground sets bit 9,
            ///       anything else (steep ground included) marks the contacts as walls so a SUPPORTED solve there cannot
            ///       ground.
            if (supported) {
                ctrl.m_flags |= ch::kHasPotentialSupportManifold;
                if (ctrl.m_context.m_currentState == ctrl.m_context.m_previousState) ctrl.m_flags |= ch::kCanJump;
            }
            ctrl.m_onlyWallContacts = !supported;
            ctrl.m_supportCount     = support;
            ctrl.m_supportNorm.set(verdict.groundNx, verdict.groundNy, verdict.groundNz, 0.0f);

            Log(numPoints, zeroed, characters, vertical, penetration, support, verdict, ctrl.m_flags);
        }

        /// @note Last = innermost: observers (the contact probe) still wrap it. It replaces Bethesda's callback, so
        ///       CharGroundModel's strict-support filter (char_support_strict) judges these planes, not Bethesda's.
        [[maybe_unused]] const bool s_subscribed =
            Process::Subscribe(&OnProcess, platform::hooks::Priority::Last, "CharPlanes", "char_planes");
        /// @note First = outermost, so the verdict is ready before vanilla's support query runs the callback.
        [[maybe_unused]] const bool s_cacheSubscribed =
            CheckSupport::Subscribe(&OnCheckSupport, platform::hooks::Priority::First, "CharPlanes", "char_planes");

    }  // namespace

}  // namespace CB::feature
