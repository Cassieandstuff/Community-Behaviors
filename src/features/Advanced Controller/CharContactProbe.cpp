#include "PCH.h"

#include <core/hooks/hookslib.h>
#include <core/hooks/takeover/Override.h>

#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharProxyController.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharRigidBodyController.h>

#include <Physics/ConstraintSolver/Simplex/hkpSimplexSolver.h>
#include <Physics/Collide/Shape/Convex/Triangle/hkpTriangleShape.h>
#include <Physics/Collide/Shape/hkpShapeContainer.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <vector>

/// @file
/// @brief Observe-only probe on Bethesda's proxy constraint callback, for the player: per contact it logs the contact
///        normal, the hit triangle's face normal (the mesh child at m_shapeKeyB, Bethesda's step-test lookup), its
///        welding info, and which branch Bethesda's callback took, every time the contact set changes.
/// @note Evidence for the Jolt-style constraint rule (steepness from the face normal): whether a ledge edge reports
///       the top face, the side face, or both decides the edge rule. It always calls `next`, so gameplay is vanilla.

namespace CB::feature {

    namespace {

        namespace hk = CB::havok;
        using Process = core::hooks::Override<&hk::bhkCharProxyController::processConstraintsCallback>;

        struct V3 {
            float x = 0, y = 0, z = 0;
        };

        V3    Of(const hk::hkVector4& v) { return { v(0), v(1), v(2) }; }
        V3    Sub(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
        V3    Cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
        float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
        V3    Normalized(V3 a)
        {
            const float l = std::sqrt(Dot(a, a));
            return l > 1e-12f ? V3{ a.x / l, a.y / l, a.z / l } : V3{};
        }
        V3 Rotate(const hk::hkTransform& t, V3 v)
        {
            const V3 c0 = Of(t.m_rotation.m_col0), c1 = Of(t.m_rotation.m_col1), c2 = Of(t.m_rotation.m_col2);
            return { c0.x * v.x + c1.x * v.y + c2.x * v.z, c0.y * v.x + c1.y * v.y + c2.y * v.z, c0.z * v.x + c1.z * v.y + c2.z * v.z };
        }

        /// @brief The hit triangle, read the way Bethesda's step test reads it.
        struct Face {
            int      childType   = -1;   ///< -1 = no mesh / no key
            V3       normal{};           ///< world space, unoriented (winding order)
            unsigned weldingInfo = 0;
            unsigned weldingType = 0;
        };

        Face FaceOf(const hk::hkRootCdPoint& point)
        {
            Face face;
            if (point.m_shapeKeyB == hk::HK_INVALID_SHAPE_KEY) return face;
            const hk::hkpShape* root = point.m_rootCollidableB->getShape();
            const hk::hkpShape* mesh = root ? hk::bhkGetMeshShape(root) : nullptr;
            if (!mesh) return face;
            const hk::hkpShapeContainer* container = mesh->getContainer();
            if (!container) return face;
            hk::hkpShapeBuffer buffer;
            const hk::hkpShape* child = container->getChildShape(point.m_shapeKeyB, buffer);
            if (!child) return face;
            face.childType = child->getType();
            if (face.childType != hk::HK_SHAPE_TRIANGLE) return face;
            const auto* tri = static_cast<const hk::hkpTriangleShape*>(child);
            const V3    a = Of(tri->m_vertexA), b = Of(tri->m_vertexB), c = Of(tri->m_vertexC);
            face.normal      = Normalized(Rotate(point.m_rootCollidableB->getTransform(), Cross(Sub(b, a), Sub(c, a))));
            face.weldingInfo = tri->m_weldingInfo;
            face.weldingType = tri->m_weldingType;
            return face;
        }

        /// @note Coarse on purpose: a contact sliding along one face must not re-log every frame. The state, wanted state
        ///       and support bits are in it, so a state change logs even when the contacts hold still.
        std::uint64_t Signature(const hk::bhkCharacterController& ctrl, const hk::hkArray<hk::hkRootCdPoint>& manifold)
        {
            std::uint64_t h = 1469598103934665603ull ^ static_cast<std::uint64_t>(manifold.getSize());
            auto          mix = [&h](std::uint64_t v) { h = (h ^ v) * 1099511628211ull; };
            mix(static_cast<std::uint64_t>(ctrl.m_context.m_currentState));
            mix(static_cast<std::uint64_t>(ctrl.m_wantState));
            mix(ctrl.m_flags & 0x2300u);
            for (int i = 0; i < manifold.getSize(); i++) {
                const hk::hkRootCdPoint& p = manifold[i];
                mix(reinterpret_cast<std::uintptr_t>(p.m_rootCollidableB));
                mix(p.m_shapeKeyB);
                const V3 n = Of(p.m_contact.m_separatingNormal);
                mix(static_cast<std::uint64_t>(static_cast<std::int64_t>(std::lround(n.x * 20) * 1600 + std::lround(n.y * 20) * 40 + std::lround(n.z * 20))));
            }
            return h;
        }

        bool IsPlayer(const hk::bhkCharProxyController* self)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            const auto* ctrl = player ? player->GetCharController() : nullptr;
            return ctrl && static_cast<const void*>(static_cast<const hk::bhkCharacterController*>(self)) == static_cast<const void*>(ctrl);
        }

        const char* Outcome(float nz, float zNormal, const hk::hkVector4& before, const hk::hkVector4& after)
        {
            const bool same = before(0) == after(0) && before(1) == after(1) && before(2) == after(2) && before(3) == after(3);
            if (after(0) == 0 && after(1) == 0 && after(2) == 0) return "zeroed";
            if (!same && after(2) == 0 && before(2) != 0) return "flattened";
            if (nz < 0) return same ? "downward" : "downward*";
            if (nz >= zNormal) return same ? "support" : "support*";
            return same ? "steep:step-rejected" : "steep:step-accepted";
        }

        /// @note One player: the mutex only guards the signature against a stray second caller.
        std::mutex    g_lock;
        std::uint64_t g_lastSignature = 0;
        std::uint32_t g_logged        = 0;
        /// @brief Every player callback, logged or not: the gap between two logged sets is how many went unlogged.
        std::uint64_t g_calls         = 0;
        constexpr std::uint32_t kMaxLogged = 20000;

        void OnProcess(Process::Next next, hk::bhkCharProxyController* self, const hk::hkpCharacterProxy* proxy,
                       const hk::hkArray<hk::hkRootCdPoint>& manifold, hk::hkSimplexSolverInput& input)
        {
            if (!IsPlayer(self)) return next(self, proxy, manifold, input);

            const int numPoints = (std::min)(input.m_numConstraints, manifold.getSize());
            std::vector<hk::hkVector4> before;
            before.reserve(numPoints);
            for (int i = 0; i < numPoints; i++) before.push_back(input.m_constraints[i].m_plane);
            const int constraintsBefore = input.m_numConstraints;

            next(self, proxy, manifold, input);

            std::scoped_lock lock(g_lock);
            const hk::bhkCharacterController& ctrl = *self;
            ++g_calls;
            const std::uint64_t sig = Signature(ctrl, manifold);
            if (sig == g_lastSignature || g_logged >= kMaxLogged) return;
            g_lastSignature = sig;
            ++g_logged;

            const V3 v = Of(input.m_velocity);
            LOG_INFO("ccprobe #{}: call {} state {} want {} fake {} points {} constraints {}->{} v=({:.2f},{:.2f},{:.2f}) flags {:#x} support {} "
                     "onlyWalls {} supportNorm=({:.2f},{:.2f},{:.2f}) zNormal {:.3f}",
                     g_logged, g_calls, static_cast<int>(ctrl.m_context.m_currentState), static_cast<int>(ctrl.m_wantState), ctrl.m_fakeSupport,
                     manifold.getSize(), constraintsBefore, input.m_numConstraints, v.x, v.y, v.z, ctrl.m_flags, ctrl.m_supportCount,
                     ctrl.m_onlyWallContacts, ctrl.m_supportNorm(0), ctrl.m_supportNorm(1), ctrl.m_supportNorm(2), ctrl.m_zNormal);
            for (int i = 0; i < manifold.getSize(); i++) {
                const hk::hkRootCdPoint& p   = manifold[i];
                const V3                 n   = Of(p.m_contact.m_separatingNormal);
                const V3                 pos = Of(p.m_contact.m_position);
                const Face               f   = FaceOf(p);
                const unsigned           layer = p.m_rootCollidableB->getCollisionFilterInfo() & 0x7f;
                const char* outcome = i < numPoints ? Outcome(n.z, ctrl.m_zNormal, before[i], input.m_constraints[i].m_plane) : "unconstrained";
                LOG_INFO("ccprobe   [{}] layer {} body {:#x} key {:#x} root {} pos=({:.3f},{:.3f},{:.3f}) n=({:.3f},{:.3f},{:.3f}) d {:.4f} "
                         "child {} face=({:.3f},{:.3f},{:.3f}) n.face {:.3f} weld {:#x}/{} -> {}",
                         i, layer, reinterpret_cast<std::uintptr_t>(p.m_rootCollidableB), p.m_shapeKeyB,
                         static_cast<int>(p.m_rootCollidableB->getShape() ? p.m_rootCollidableB->getShape()->getType() : -1), pos.x, pos.y, pos.z,
                         n.x, n.y, n.z, p.m_contact.getDistance(), f.childType, f.normal.x, f.normal.y, f.normal.z, Dot(n, f.normal),
                         f.weldingInfo, f.weldingType, outcome);
            }
            for (int i = constraintsBefore; i < input.m_numConstraints; i++) {
                const hk::hkVector4& pl = input.m_constraints[i].m_plane;
                LOG_INFO("ccprobe   [+{}] vertical plane n=({:.3f},{:.3f},{:.3f}) d {:.4f}", i - constraintsBefore, pl(0), pl(1), pl(2), pl(3));
            }
            if (g_logged == kMaxLogged) LOG_INFO("ccprobe: {} contact sets logged, stopping", kMaxLogged);
        }

        [[maybe_unused]] const bool s_subscribed =
            Process::Subscribe(&OnProcess, core::hooks::Priority::Normal, "CharContactProbe", "char_contact_probe");

    }  // namespace

}  // namespace CB::feature
