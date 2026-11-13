#include "PCH.h"

#include "CharGraph.h"

#include "CharGround.h"
#include "CharLoad.h"

#include <hky/locomotion/Floor.h>
#include <platform/physics/GameWorld.h>

#include <Common/Base/hkBase.h>
#include <Common/Base/Types/Geometry/Aabb/hkAabb.h>
#include <Physics/Collide/Shape/Compound/Collection/List/hkpListShape.h>
#include <Physics/Collide/Shape/Convex/Capsule/hkpCapsuleShape.h>
#include <Physics/Dynamics/Phantom/hkpCachingShapePhantom.h>
#include <Physics/Utilities/CharacterControl/StateMachine/Util/hkpCharacterMovementUtil.h>

#include <algorithm>
#include <core/debug/Settings.h>
#include <platform/profile/Profile.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace CB::feature::controller {

    namespace {

        std::atomic<bool> g_playerGraphMoves{ false };

        /// @brief hkpShape::getAabb, vtable slot 7 (the schema still calls it unk_07).
        /// @note Proven twice in the exe: bhkCharacterControllerCinfo (0x140e9cdf0) and hkpWorldLinearCaster
        ///       (0x140bea990) call a shape's +0x38 with (transform, tolerance, out AABB).
        void GetAabb(const ::hkpShape* shape, const ::hkTransform& transform, ::hkAabb& out)
        {
            using Fn      = void (*)(const ::hkpShape*, const ::hkTransform&, float, ::hkAabb&);
            const auto fn = reinterpret_cast<Fn>((*reinterpret_cast<void* const* const*>(shape))[7]);
            fn(shape, transform, 0.0f, out);
        }

        /// @brief A body this much longer than it is wide is read as a long body.
        constexpr float kLongBodyRatio = 1.3f;

        /// @brief Reads a long body's length axis, centre and width from `shape`'s own box rotated by `world`'s rotation.
        /// @note  The identity is built from scratch: hkTransform::getIdentity reads a vanilla global the plugin does not
        ///        link (the schema link trap).
        void ReadLongBody(const ::hkpShape* shape, const ::hkTransform& world, Hull& h)
        {
            alignas(16) ::hkTransform identity;
            identity.getColumn(0) = ::hkVector4(1.0f, 0.0f, 0.0f, 0.0f);
            identity.getColumn(1) = ::hkVector4(0.0f, 1.0f, 0.0f, 0.0f);
            identity.getColumn(2) = ::hkVector4(0.0f, 0.0f, 1.0f, 0.0f);
            identity.getTranslation()     = ::hkVector4(0.0f, 0.0f, 0.0f, 0.0f);
            ::hkAabb local;
            GetAabb(shape, identity, local);
            const float ex = local.m_max(0) - local.m_min(0), ey = local.m_max(1) - local.m_min(1);
            if ((std::max)(ex, ey) < kLongBodyRatio * (std::min)(ex, ey)) return;
            ::hkTransform t   = world;
            const bool      onX = ex >= ey;
            const float     ax = t.getColumn(0)(0) * (onX ? 1.0f : 0.0f) + t.getColumn(1)(0) * (onX ? 0.0f : 1.0f);
            const float     ay = t.getColumn(0)(1) * (onX ? 1.0f : 0.0f) + t.getColumn(1)(1) * (onX ? 0.0f : 1.0f);
            const float     len = std::sqrt(ax * ax + ay * ay);
            if (len < 1.0e-4f) return;
            const float lx = (local.m_min(0) + local.m_max(0)) * 0.5f, ly = (local.m_min(1) + local.m_max(1)) * 0.5f;
            const float lz = (local.m_min(2) + local.m_max(2)) * 0.5f;
            h.longBody   = true;
            h.fx         = ax / len;
            h.fy         = ay / len;
            h.cx         = t.getTranslation()(0) + t.getColumn(0)(0) * lx + t.getColumn(1)(0) * ly + t.getColumn(2)(0) * lz;
            h.cy         = t.getTranslation()(1) + t.getColumn(0)(1) * lx + t.getColumn(1)(1) * ly + t.getColumn(2)(1) * lz;
            h.radius     = (std::min)(ex, ey) * 0.5f;
            h.halfLength = (std::max)(ex, ey) * 0.5f;
        }

        double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

        /// @brief The ridden horse as last published: its controller, and when.
        struct Ridden {
            std::atomic<const void*> ctrl{ nullptr };
            std::atomic<double>      at{ -1.0e9 };
        };
        Ridden            g_ridden;
        constexpr double  kRiddenStale = 0.5;
        std::atomic<bool> g_horseGraphMoves{ false };

        /// @brief The steepest walkable face for the ridden horse: 55 deg against vanilla's 47 (spec S12: it climbs more
        ///        than the player, still slides past its limit). A starting value, to tune.
        constexpr float kHorseWalkableZ = 0.57357644f;

        /// @brief One driven NPC, by its controller.
        struct NpcEntry {
            RE::ActorHandle     actor;
            std::atomic<double> at{ -1.0e9 };
            std::atomic<bool>   graphMoves{ false };
        };
        /// @brief Every NPC a frame driver has held. Physics callbacks read it many times a step, so readers share.
        /// @note Entries stay: IsManagedNpc goes by the time, and a reused controller address takes the new actor's handle.
        ///       Bounded by the controller addresses the allocator hands out.
        std::shared_mutex                           g_npcLock;
        std::unordered_map<const void*, NpcEntry>   g_npcs;
        std::atomic<bool>                           g_anyNpc{ false };
        constexpr double                            kNpcStale = 0.5;

    }  // namespace

    void PublishNpc(RE::Actor& npc)
    {
        const void* ctrl = static_cast<const void*>(npc.GetCharController());
        if (!ctrl) return;
        const RE::ActorHandle handle = npc.GetHandle();
        {
            std::shared_lock read(g_npcLock);
            if (const auto it = g_npcs.find(ctrl); it != g_npcs.end() && it->second.actor == handle) {
                it->second.at.store(Now(), std::memory_order_release);
                return;
            }
        }
        std::unique_lock write(g_npcLock);
        NpcEntry& entry = g_npcs[ctrl];
        entry.actor     = handle;
        entry.graphMoves.store(false, std::memory_order_relaxed);
        entry.at.store(Now(), std::memory_order_release);
        g_anyNpc.store(true, std::memory_order_release);
    }

    void UnpublishNpc(RE::Actor& npc)
    {
        const void* ctrl = static_cast<const void*>(npc.GetCharController());
        if (!ctrl || !g_anyNpc.load(std::memory_order_acquire)) return;
        std::shared_lock read(g_npcLock);
        if (const auto it = g_npcs.find(ctrl); it != g_npcs.end()) it->second.at.store(-1.0e9, std::memory_order_release);
    }

    bool IsManagedNpc(const ::bhkCharacterController* ctrl)
    {
        if (!ctrl || !g_anyNpc.load(std::memory_order_acquire)) return false;
        std::shared_lock read(g_npcLock);
        const auto       it = g_npcs.find(static_cast<const void*>(ctrl));
        return it != g_npcs.end() && Now() - it->second.at.load(std::memory_order_acquire) <= kNpcStale;
    }

    bool WasManagedNpc(const ::bhkCharacterController* ctrl)
    {
        if (!ctrl || !g_anyNpc.load(std::memory_order_acquire)) return false;
        std::shared_lock read(g_npcLock);
        return g_npcs.contains(static_cast<const void*>(ctrl));
    }

    void NpcCounts(std::size_t& managed, std::size_t& entries)
    {
        std::shared_lock read(g_npcLock);
        const double     now = Now();
        managed              = 0;
        entries              = g_npcs.size();
        for (const auto& [ctrl, e] : g_npcs)
            if (now - e.at.load(std::memory_order_acquire) <= kNpcStale) ++managed;
    }

    void PublishRiddenHorse(RE::Actor* horse)
    {
        const void* ctrl = horse ? static_cast<const void*>(horse->GetCharController()) : nullptr;
        g_ridden.ctrl.store(ctrl, std::memory_order_relaxed);
        g_ridden.at.store(ctrl ? Now() : -1.0e9, std::memory_order_release);
    }

    bool IsRiddenHorse(const ::bhkCharacterController* ctrl)
    {
        if (!ctrl || g_ridden.ctrl.load(std::memory_order_relaxed) != static_cast<const void*>(ctrl)) return false;
        return Now() - g_ridden.at.load(std::memory_order_acquire) <= kRiddenStale;
    }

    bool Riding()
    {
        return g_ridden.ctrl.load(std::memory_order_relaxed) && Now() - g_ridden.at.load(std::memory_order_acquire) <= kRiddenStale;
    }

    RE::NiPointer<RE::Actor> ManagedActor(const ::bhkCharacterController& ctrl)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return {};
        if (IsPlayer(&ctrl)) return RE::NiPointer<RE::Actor>(player);
        if (IsManagedNpc(&ctrl)) {
            std::shared_lock read(g_npcLock);
            const auto       it = g_npcs.find(static_cast<const void*>(&ctrl));
            return it != g_npcs.end() ? it->second.actor.get() : RE::NiPointer<RE::Actor>{};
        }
        if (!IsRiddenHorse(&ctrl)) return {};
        RE::NiPointer<RE::Actor> mount;
        if (!player->GetMount(mount) || !mount || static_cast<const void*>(mount->GetCharController()) != static_cast<const void*>(&ctrl)) return {};
        return mount;
    }

    float WalkableZ(const ::bhkCharacterController& ctrl) { return IsRiddenHorse(&ctrl) ? kHorseWalkableZ : ctrl.m_zNormal; }

    bool IsPlayer(const ::bhkCharacterController* ctrl)
    {
        auto*       player = RE::PlayerCharacter::GetSingleton();
        const auto* own    = player ? player->GetCharController() : nullptr;
        return own && static_cast<const void*>(ctrl) == static_cast<const void*>(own);
    }

    bool IsProxy(const ::bhkCharacterController& ctrl)
    {
        static const std::uintptr_t vtable = REL::Relocation<std::uintptr_t>(RE::VTABLE_bhkCharProxyController[1]).address();
        return *reinterpret_cast<const std::uintptr_t*>(&ctrl) == vtable;
    }

    const char* GraphMovesActor(RE::Actor* actor)
    {
        static const RE::BSFixedString animationDriven("bAnimationDriven");
        static const RE::BSFixedString isSynced("bIsSynced");
        if (!actor) return "";
        bool        on   = false;
        const char* flag = "";
        if (GetGraphBool(actor, animationDriven, on) && on) flag = "bAnimationDriven";
        else if (GetGraphBool(actor, isSynced, on) && on) flag = "bIsSynced";
        if (actor == RE::PlayerCharacter::GetSingleton()) g_playerGraphMoves.store(flag[0] != 0, std::memory_order_relaxed);
        else if (IsRiddenHorse(reinterpret_cast<const ::bhkCharacterController*>(actor->GetCharController())))
            g_horseGraphMoves.store(flag[0] != 0, std::memory_order_relaxed);
        else if (g_anyNpc.load(std::memory_order_acquire)) {
            std::shared_lock read(g_npcLock);
            if (const auto it = g_npcs.find(static_cast<const void*>(actor->GetCharController())); it != g_npcs.end())
                it->second.graphMoves.store(flag[0] != 0, std::memory_order_relaxed);
        }
        return flag;
    }

    bool GraphMovesPublished(const ::bhkCharacterController& ctrl)
    {
        if (IsPlayer(&ctrl)) return g_playerGraphMoves.load(std::memory_order_relaxed);
        if (IsRiddenHorse(&ctrl)) return g_horseGraphMoves.load(std::memory_order_relaxed);
        if (!g_anyNpc.load(std::memory_order_acquire)) return false;
        std::shared_lock read(g_npcLock);
        const auto       it = g_npcs.find(static_cast<const void*>(&ctrl));
        return it != g_npcs.end() && it->second.graphMoves.load(std::memory_order_relaxed);
    }

    const char* Name(Ground g) { return g == Ground::Grounded ? "Grounded" : "Airborne"; }

    Hull GetHull(::bhkCharacterController& ctrl)
    {
        Hull h;
        auto&                       proxyCtrl = static_cast<::bhkCharProxyController&>(ctrl);
        ::bhkCachingShapePhantom* bp        = proxyCtrl.m_shapePhantom.get();
        ::hkpCachingShapePhantom* phantom   = bp ? bp->GetPhantom() : nullptr;
        h.world                               = ctrl.GetHavokWorld();
        if (!h.world || !phantom) return h;
        h.collidable = phantom->getCollidable();
        const ::hkpShape* shape = h.collidable->getShape();
        if (!shape) return h;
        ::hkAabb aabb;
        GetAabb(shape, h.collidable->getTransform(), aabb);
        h.lift   = Lift(ctrl);
        h.feetZ  = aabb.m_min(2) - h.lift;
        h.topZ   = aabb.m_max(2);
        h.cx     = (aabb.m_min(0) + aabb.m_max(0)) * 0.5f;
        h.cy     = (aabb.m_min(1) + aabb.m_max(1)) * 0.5f;
        h.radius = (std::min)(aabb.m_max(0) - aabb.m_min(0), aabb.m_max(1) - aabb.m_min(1)) * 0.5f;
        h.valid  = h.radius > 0.0f;
        if (h.valid && !IsPlayer(&ctrl)) ReadLongBody(shape, h.collidable->getTransform(), h);
        return h;
    }

    namespace {

        /// @note List children carry no transform of their own (hkpListShape), so every capsule is in the shape's frame.
        void CollectCapsules(const ::hkpShape* shape, const ::hkTransform& world, HullShape& out, int depth)
        {
            if (!shape || depth > 4) return;
            if (shape->getType() == ::HK_SHAPE_CAPSULE) {
                if (out.count >= static_cast<int>(out.capsules.size())) {
                    out.other = true;
                    return;
                }
                const auto*        capsule = static_cast<const ::hkpCapsuleShape*>(shape);
                HullShape::Capsule& c      = out.capsules[out.count++];
                ::hkVector4      a, b;
                a._setTransformedPos(world, capsule->getVertex(0));
                b._setTransformedPos(world, capsule->getVertex(1));
                for (int i = 0; i < 3; i++) c.a[i] = a(i), c.b[i] = b(i);
                c.radius = capsule->getRadius();
                return;
            }
            /// @note Only enabled children collide: the player's list holds a disabled clone of the skeleton's own capsule
            ///       (FUN_140ea4020 disables it as it builds the list), which drawing would pass off as part of the hull.
            if (shape->getType() == ::HK_SHAPE_LIST) {
                const auto* list = static_cast<const ::hkpListShape*>(shape);
                for (int i = 0; i < list->m_childInfo.getSize(); i++)
                    if (list->m_enabledChildren[(i >> 5) & 7] & (1u << (i & 31)))
                        CollectCapsules(list->m_childInfo[i].m_shape, world, out, depth + 1);
                return;
            }
            out.other = true;
        }

    }  // namespace

    HullShape ReadHullShape(const Hull& hull)
    {
        HullShape out;
        if (!hull.valid || !hull.collidable) return out;
        const ::hkpShape* shape = hull.collidable->getShape();
        if (!shape) return out;
        const ::hkTransform& world = hull.collidable->getTransform();
        CollectCapsules(shape, world, out, 0);
        ::hkAabb aabb;
        GetAabb(shape, world, aabb);
        for (int i = 0; i < 3; i++) out.min[i] = aabb.m_min(i), out.max[i] = aabb.m_max(i);
        return out;
    }

    namespace {

        namespace lc = hky::locomotion;

        lc::FloorBody BodyOf(const Hull& h)
        {
            lc::FloorBody b;
            b.feetZ = h.feetZ, b.cx = h.cx, b.cy = h.cy, b.radius = h.radius;
            b.longBody = h.longBody, b.fx = h.fx, b.fy = h.fy, b.halfLength = h.halfLength, b.lift = h.lift;
            return b;
        }

        lc::FloorTunables TunablesOf(const FloorTunables& t) { return { t.perchFraction, t.footTolerance, t.wallNormalZ }; }

        FloorHit FromSdk(const lc::FloorHit& h)
        {
            FloorHit o;
            o.cast = h.cast, o.hit = h.hit, o.dz = h.dz, o.nx = h.nx, o.ny = h.ny, o.nz = h.nz, o.layer = h.layer;
            o.body = reinterpret_cast<const ::hkpCollidable*>(h.body);
            return o;
        }

        lc::FloorHit ToSdk(const FloorHit& h)
        {
            lc::FloorHit o;
            o.cast = h.cast, o.hit = h.hit, o.dz = h.dz, o.nx = h.nx, o.ny = h.ny, o.nz = h.nz, o.layer = h.layer;
            o.body = reinterpret_cast<std::uint64_t>(h.body);
            return o;
        }

        FloorVerdict FromSdk(const lc::FloorVerdict& v)
        {
            FloorVerdict o;
            o.ground    = v.ground == lc::Ground::Grounded ? Ground::Grounded : Ground::Airborne;
            o.support   = v.support;
            o.stickable = v.stickable;
            o.stickDz   = v.stickDz;
            o.groundNx = v.groundNx, o.groundNy = v.groundNy, o.groundNz = v.groundNz;
            o.supportBody  = reinterpret_cast<const ::hkpCollidable*>(v.supportBody);
            o.supportHits  = v.supportHits;
            o.walkableHits = v.walkableHits;
            o.longBody     = v.longBody;
            for (std::size_t i = 0; i < o.hits.size(); i++) o.hits[i] = FromSdk(v.hits[i]), o.hindHits[i] = FromSdk(v.hindHits[i]);
            return o;
        }

        lc::FloorVerdict ToSdk(const FloorVerdict& v)
        {
            lc::FloorVerdict o;
            o.ground    = v.ground == Ground::Grounded ? lc::Ground::Grounded : lc::Ground::Airborne;
            o.support   = v.support;
            o.stickable = v.stickable;
            o.stickDz   = v.stickDz;
            o.groundNx = v.groundNx, o.groundNy = v.groundNy, o.groundNz = v.groundNz;
            o.supportBody  = reinterpret_cast<std::uint64_t>(v.supportBody);
            o.supportHits  = v.supportHits;
            o.walkableHits = v.walkableHits;
            o.longBody     = v.longBody;
            for (std::size_t i = 0; i < o.hits.size(); i++) o.hits[i] = ToSdk(v.hits[i]), o.hindHits[i] = ToSdk(v.hindHits[i]);
            return o;
        }

        std::atomic<std::uint64_t> g_footprints{ 0 }, g_rings{ 0 };

        /// @brief Counts footprints and the ones that cast their ring, logged at every power of two.
        void CountFootprint(bool ring)
        {
            const std::uint64_t rings = ring ? g_rings.fetch_add(1, std::memory_order_relaxed) + 1 : g_rings.load(std::memory_order_relaxed);
            const std::uint64_t n     = g_footprints.fetch_add(1, std::memory_order_relaxed) + 1;
            CountLoad(LoadCounter::Rays, ring ? 9 : 1);
            if ((n & (n - 1)) == 0 && n >= 1024)
                LOG_INFO("floor query: {} footprints, {} cast the ring ({:.1f}%), {} rays in all", n, rings, 100.0 * static_cast<double>(rings) / n,
                         n + 8 * rings);
        }

    }  // namespace

    void FloorRayXY(const Hull& hull, int i, const FloorTunables& t, float& x, float& y, bool hind)
    {
        lc::FloorRayXY(BodyOf(hull), i, TunablesOf(t), x, y, hind);
    }

    FloorVerdict QueryFloor(::bhkCharacterController& ctrl, const Hull& hull, const FloorTunables& t)
    {
        return QueryFloorAt(ctrl, hull, 0.0f, 0.0f, 0.0f, t);
    }

    /// @note hky::locomotion's floor query, cast through the game's world (platform::physics::GameWorld) with the
    ///       controller's own collision filter.
    FloorVerdict QueryFloorAt(::bhkCharacterController& ctrl, const Hull& hull, float dx, float dy, float dz, const FloorTunables& t)
    {
        if (!hull.valid) return FloorVerdict{};
        CB_ZONE(zone, "floor query");
        const platform::physics::GameWorld world(hull.world);
        const ::CFilter                    filter = ctrl.GetCollisionFilterInfo();
        lc::FloorQuery                     q;
        q.world          = &world;
        q.filter.native  = filter.m_filter;
        q.stepHeight     = ctrl.m_stepHeight;
        q.walkableZ      = WalkableZ(ctrl);
        q.characterLayer = kCharControllerLayer;
        q.support        = [&ctrl](float groundNz) { return Support(ctrl, groundNz); };
        const lc::FloorVerdict v = lc::QueryFloorAt(q, BodyOf(hull), dx, dy, dz, TunablesOf(t));
        CountFootprint(v.ringCast);
        if (v.longBody) CountFootprint(v.hindRingCast);
        return FromSdk(v);
    }

    bool PogoFloor(const ::bhkCharacterController& ctrl, const FloorVerdict& v, float& dz, int* ray)
    {
        return lc::PogoFloor(WalkableZ(ctrl), ToSdk(v), dz, ray, kCharControllerLayer);
    }

    bool PogoOn()
    {
        static const bool on = core::debug::FlagOn("char_pogo") && core::debug::FlagOn("char_ground_states");
        return on;
    }

    namespace {

        /// @brief calculateMovement's output for a request of `forward` along the tangent and `side` along the side.
        void Steer(const ::bhkCharacterController& c, float forward, float side, float& dx, float& dy)
        {
            alignas(16) std::array<std::uint8_t, sizeof(::hkpCharacterMovementUtil::hkpMovementUtilInput)> storage{};
            auto& in = *reinterpret_cast<::hkpCharacterMovementUtil::hkpMovementUtilInput*>(storage.data());
            in.m_forward = c.m_forward;
            in.m_up.set(0.0f, 0.0f, 1.0f, 0.0f);
            in.m_surfaceNormal.set(0.0f, 0.0f, 1.0f, 0.0f);
            in.m_desiredVelocity.set(forward, side, 0.0f, 0.0f);
            in.m_gain             = 1.0f;
            in.m_maxVelocityDelta = 1.0e6f;
            ::hkVector4 out;
            out.set(0.0f, 0.0f, 0.0f, 0.0f);
            ::hkpCharacterMovementUtil::calculateMovement(in, out);
            dx = out(0), dy = out(1);
        }

    }  // namespace

    void DesiredHorizontal(const ::bhkCharacterController& ctrl, float& dx, float& dy)
    {
        Steer(ctrl, ctrl.m_velocityMod(1), ctrl.m_velocityMod(0), dx, dy);
    }

    bool MovementAxes(const ::bhkCharacterController& ctrl, float& forwardX, float& forwardY, float& sideX, float& sideY)
    {
        Steer(ctrl, 1.0f, 0.0f, forwardX, forwardY);
        Steer(ctrl, 0.0f, 1.0f, sideX, sideY);
        const float f = forwardX * forwardX + forwardY * forwardY, s = sideX * sideX + sideY * sideY;
        if (f < 1.0e-6f || s < 1.0e-6f) return false;
        const float fi = 1.0f / std::sqrt(f), si = 1.0f / std::sqrt(s);
        forwardX *= fi, forwardY *= fi, sideX *= si, sideY *= si;
        return true;
    }

    float SettingFloat(std::string_view key, float def)
    {
        const std::string s   = core::debug::Value("Character", key);
        char*             end = nullptr;
        const float       f   = s.empty() ? def : std::strtof(s.c_str(), &end);
        return s.empty() || end == s.c_str() ? def : f;
    }

    float IniFloat(const char* name, float def)
    {
        auto*        ini = RE::INISettingCollection::GetSingleton();
        RE::Setting* s   = ini ? ini->GetSetting(name) : nullptr;
        return s ? s->GetFloat() : def;
    }

    float Support(const ::bhkCharacterController& ctrl, float groundNz)
    {
        constexpr float      kDegToRad = 0.017453292f;
        static const float start = std::clamp(SettingFloat("fSlidePullStart", 5.0f), 0.0f, 45.0f) * kDegToRad;
        static const float full  = std::clamp(SettingFloat("fSlidePullFull", 10.0f), 0.5f, 45.0f) * kDegToRad;
        const float        maxSlope = std::acos(std::clamp(WalkableZ(ctrl), -1.0f, 1.0f));
        static const bool  logged   = [&] {
            LOG_INFO("slide: support eases from 1 at {:.1f} deg to 0 at {:.1f} deg (max slope {:.1f} deg)", (maxSlope - start) / kDegToRad,
                     (maxSlope + full) / kDegToRad, maxSlope / kDegToRad);
            return true;
        }();
        (void)logged;
        const float slope = std::acos(std::clamp(groundNz, -1.0f, 1.0f));
        const float lo = maxSlope - start, hi = maxSlope + full;
        if (slope <= lo) return 1.0f;
        if (slope >= hi) return 0.0f;
        const float u = (slope - lo) / (hi - lo);
        return 1.0f - u * u * (3.0f - 2.0f * u);
    }

    namespace {
        /// @brief Each managed controller's latest verdict and ground note (the player's, the ridden horse's).
        /// @note Keyed by the controller's address; a horse's controller is freed when it unloads, so past a handful of
        ///       entries every one but the player's is dropped (they refill on their next step).
        struct Published {
            bool         haveVerdict = false, haveNote = false;
            FloorVerdict verdict;
            GroundNote   note;
        };
        std::mutex                                     g_publishLock;
        std::unordered_map<const void*, Published>     g_published;
        /// @note Room for high process's 128 actors with the player and a horse; a stale controller's slot goes when it fills.
        constexpr std::size_t                          kMaxPublished = 256;

        /// @pre g_publishLock is held.
        Published& SlotOf(const ::bhkCharacterController& ctrl)
        {
            if (g_published.size() >= kMaxPublished && !g_published.contains(&ctrl)) {
                std::erase_if(g_published, [](const auto& kv) { return !IsPlayer(static_cast<const ::bhkCharacterController*>(kv.first)); });
            }
            return g_published[&ctrl];
        }
    }  // namespace

    void PublishGroundNote(const ::bhkCharacterController& ctrl, const GroundNote& note)
    {
        std::scoped_lock lock(g_publishLock);
        Published&       slot = SlotOf(ctrl);
        slot.note             = note;
        slot.haveNote         = true;
    }

    bool PublishedGroundNote(const ::bhkCharacterController& ctrl, GroundNote& out)
    {
        std::scoped_lock lock(g_publishLock);
        const auto       it = g_published.find(&ctrl);
        if (it == g_published.end() || !it->second.haveNote) return false;
        out = it->second.note;
        return true;
    }

    void PublishVerdict(const ::bhkCharacterController& ctrl, const FloorVerdict& v)
    {
        std::scoped_lock lock(g_publishLock);
        Published&       slot = SlotOf(ctrl);
        slot.verdict          = v;
        slot.haveVerdict      = true;
    }

    bool LatestVerdict(FloorVerdict& out)
    {
        auto*       player = RE::PlayerCharacter::GetSingleton();
        const void* own    = player ? static_cast<const void*>(player->GetCharController()) : nullptr;
        if (!own) return false;
        std::scoped_lock lock(g_publishLock);
        const auto       it = g_published.find(own);
        if (it == g_published.end() || !it->second.haveVerdict) return false;
        out = it->second.verdict;
        return true;
    }

    float SlopeSpeedScale(const FloorVerdict& g, float dirX, float dirY)
    {
        static const hky::locomotion::Tuning tuning{ (std::max)(0.0f, SettingFloat("fSlopeSpeedDown", 1.0f)),
                                                           (std::max)(0.0f, SettingFloat("fSlopeSpeedUp", 0.5f)) };
        static const bool logged = [] {
            LOG_INFO("slope speed: downhill x(1 + {:.2f} sin) up to 1.5, uphill x(1 - {:.2f} sin) down to 0.7", tuning.slopeSpeedDown,
                     tuning.slopeSpeedUp);
            return true;
        }();
        (void)logged;
        return hky::locomotion::SlopeSpeedScale(SurfaceOf(g), dirX, dirY, tuning);
    }

    bool PublishedVerdict(const ::bhkCharacterController& ctrl, FloorVerdict& out)
    {
        std::scoped_lock lock(g_publishLock);
        const auto       it = g_published.find(&ctrl);
        if (it == g_published.end() || !it->second.haveVerdict) return false;
        out = it->second.verdict;
        return true;
    }

}  // namespace CB::feature::controller
