#include "PCH.h"

#include "platform/physics/GameWorld.h"

#include <Physics/Collide/Agent/Collidable/hkpCollidable.h>
#include <Physics/Collide/Agent/Query/hkpCdPointCollector.h>
#include <Physics/Collide/Shape/hkpShape.h>
#include <BSHavok/bhkPickData.h>
#include <BSHavok/bhkShape.h>

#include <algorithm>
#include <cfloat>
#include <cstddef>
#include <cstring>
#include <memory>

namespace CB::platform::physics {

    namespace {

        namespace ph = hky::physics;
        constexpr float kToGame   = ph::kHavokToGame;
        constexpr float kToHavok  = 1.0f / ph::kHavokToGame;

        ::hkVector4 Havok(const ph::Vec3& v)
        {
            ::hkVector4 out;
            out.set(v.x * kToHavok, v.y * kToHavok, v.z * kToHavok, 0.0f);
            return out;
        }

        ph::Vec3 Game(const ::hkVector4& v) { return { v(0) * kToGame, v(1) * kToGame, v(2) * kToGame }; }

        unsigned LayerOf(const ::hkpCollidable* c) { return c ? (c->getCollisionFilterInfo() & 0x7f) : 0u; }

        bool LayerIgnored(std::uint64_t ignoreLayers, unsigned layer) { return layer < 64 && ((ignoreLayers >> layer) & 1u); }

        /// @brief The world's read lock, held for the scope; the wait for it reported to the query's timer.
        /// @note  bhkWorldLock is a no-op unless the game runs with bUseWorldLock:HAVOK, as is vanilla's own lock.
        class TimedReadLock {
        public:
            TimedReadLock(::bhkWorld* world, QueryTimer& timer) : m_from(QueryClock::now()), m_lock(world, 0)
            {
                timer.LockWaited(QueryClock::now() - m_from);
            }
            TimedReadLock(const TimedReadLock&)            = delete;
            TimedReadLock& operator=(const TimedReadLock&) = delete;

        private:
            QueryClock::time_point m_from;   ///< declared before m_lock: read before the lock is taken
            ::bhkWorldLock         m_lock;
        };

        /// @brief The first thing a sweep met (Havok units).
        struct NativeSweepHit {
            bool                   hit      = false;
            float                  fraction = 1.0f;   ///< of the path, 0 to 1
            ::hkContactPoint       contact{};         ///< on the hit body; normal points away from it, w the fraction
            const ::hkpCollidable* body = nullptr;    ///< the hit root collidable
        };

        /// @brief The game-units hit a sweep contact makes, as CastShape reports it and `accept` sees it.
        ph::Hit HitOf(const ::hkContactPoint& contact, float fraction, const ::hkpCollidable* body)
        {
            ph::Hit hit;
            hit.fraction         = fraction;
            hit.point            = Game(contact.getPosition());
            const ::hkVector4& n = contact.getSeparatingNormal();
            hit.normal           = { n(0), n(1), n(2) };
            hit.layer            = static_cast<std::uint8_t>(LayerOf(body));
            hit.userData         = reinterpret_cast<std::uint64_t>(body);
            return hit;
        }

        /// @brief hkpCdPoint as vanilla's hkpAllCdPointCollector::addCdPoint (0x140b1d930) reads it.
        /// @note 0x40 bytes: the contact is copied whole, the unwelded normal skipped, and each body walked up m_parent
        ///       (+0x18) to its root collidable with its shape key read at +0x08.
        struct CdPoint {
            ::hkContactPoint   m_contact;          ///< +0x00
            ::hkVector4        m_unweldedNormal;   ///< +0x20
            const ::hkpCdBody* m_cdBodyA;          ///< +0x30 — the swept collidable's side
            const ::hkpCdBody* m_cdBodyB;          ///< +0x38 — the hit body's side
        };
        static_assert(sizeof(CdPoint) == 0x40 && offsetof(CdPoint, m_cdBodyA) == 0x30 && offsetof(CdPoint, m_cdBodyB) == 0x38);

        /// @brief hkpLinearCastInput as hkpWorldLinearCaster (0x140bea990) reads it: m_to, then the float it keeps aside,
        ///        then the tolerance it widens the shape's AABB by.
        struct LinearCastInput {
            ::hkVector4  m_to;                    ///< +0x00
            float        m_maxExtraPenetration;   ///< +0x10
            float        m_startPointTolerance;   ///< +0x14
            std::uint8_t m_pad18[8];
        };
        static_assert(sizeof(LinearCastInput) == 0x20 && offsetof(LinearCastInput, m_startPointTolerance) == 0x14);

        /// @brief A closest-hit cd-point collector with vanilla's collector vtable shape.
        /// @note Vanilla's hkpAllCdPointCollector vtable (0x1417ed228) is three slots: scalar deleting destructor,
        ///       addCdPoint, and a VIRTUAL reset (0x1403f8c00, which the schema header declares non-virtual), so this
        ///       class builds its own three-slot table instead of deriving from the schema's.
        /// @note A linear cast reports a hit's path fraction in the contact's distance (w of the separating normal);
        ///       the caster skips hits beyond m_earlyOutDistance, so the collector keeps the smallest and lowers it.
        struct ClosestCollector {
            const void* const*     m_vtable;
            float                  m_earlyOutDistance = FLT_MAX;   ///< +0x08, read by the caster
            NativeSweepHit         m_best;
            float                  m_dx = 0, m_dy = 0, m_dz = 0;   ///< the path, for the moving-away test
            bool                   m_rejectLeaving      = true;
            float                  m_ignoreAboveNormalZ = 2.0f;
            std::uint64_t          m_ignoreLayers       = 0;
            const ::hkpCollidable* m_self               = nullptr;   ///< the stand-in's original, never a hit
            const ph::HitAccept*   m_accept             = nullptr;
        };

        void* CollectorDtor(ClosestCollector* self, unsigned) { return self; }

        /// @note Skipped hits never lower m_earlyOutDistance: a nearer ignored body (another character, the swept
        ///       phantom itself) must not hide a real hit beyond it.
        void CollectorAdd(ClosestCollector* self, const CdPoint& point)
        {
            const float d = point.m_contact.getDistance();
            if (d >= self->m_earlyOutDistance) return;
            const ::hkVector4& n = point.m_contact.getSeparatingNormal();
            if (self->m_rejectLeaving && n(0) * self->m_dx + n(1) * self->m_dy + n(2) * self->m_dz >= 0.0f) return;
            if (n(2) > self->m_ignoreAboveNormalZ) return;
            const ::hkpCdBody* root = point.m_cdBodyB;
            while (root && root->getParent()) root = root->getParent();
            if (root && root == self->m_self) return;
            const auto* body = static_cast<const ::hkpCollidable*>(root);
            if (root && self->m_ignoreLayers && LayerIgnored(self->m_ignoreLayers, LayerOf(body))) return;
            if (self->m_accept && self->m_accept->fn && !self->m_accept->Accepts(HitOf(point.m_contact, d, body))) return;
            self->m_earlyOutDistance = d;
            self->m_best.hit      = true;
            self->m_best.fraction = d;
            self->m_best.contact  = point.m_contact;
            self->m_best.body     = body;
        }

        void CollectorReset(ClosestCollector* self)
        {
            self->m_earlyOutDistance = FLT_MAX;
            self->m_best             = NativeSweepHit{};
        }

        const void* const kCollectorVtable[3] = { reinterpret_cast<const void*>(&CollectorDtor), reinterpret_cast<const void*>(&CollectorAdd),
                                                  reinterpret_cast<const void*>(&CollectorReset) };

        /// @brief A stand-in for a root collidable at another translation: a byte copy of the owner's leading bytes and the
        ///        collidable, its m_motion pointed at a local transform (the original's rotation, the new translation).
        /// @note What the cast reads from collA, per the exe: hkpWorldLinearCaster (0x140bea990) reads its shape (+0x00),
        ///       the shape's type (shape +0x18) and m_motion's transform (+0x10; translation at +0x30) for the path and
        ///       the swept AABB; its broad-phase callback (0x140beab60) skips a candidate only when it IS collA (pointer
        ///       compare), then asks the world filter: bhkCollisionFilter's collidable pair test (0x140eb6950) reads only
        ///       each side's m_collisionFilterInfo (+0x2c), and its shape-collection test (0x140eb6980) walks m_parent
        ///       (+0x18) to the root and reads the same +0x2c. None of them reach the owner; the owner bytes are copied
        ///       anyway so m_ownerOffset resolves to a coherent header (vtable, world, user data), never stack garbage.
        /// @warning The pointer compare no longer matches, so the real collidable (still in the broad phase at its own
        ///          spot) becomes a candidate: the collector drops it by identity (m_self).
        struct StandIn {
            static constexpr std::ptrdiff_t kMaxOwnerBytes = 0x80;
            alignas(16) std::uint8_t        m_bytes[kMaxOwnerBytes + sizeof(::hkpCollidable)];
            alignas(16) ::hkTransform       m_transform;
            ::hkpCollidable*                m_collidable = nullptr;

            StandIn(const ::hkpCollidable& original, const ::hkVector4& from)
            {
                std::memcpy(static_cast<void*>(&m_transform), &original.getTransform(), sizeof(m_transform));
                m_transform.getTranslation() = from;
                const std::ptrdiff_t ownerBytes = (std::max)(std::ptrdiff_t{ 0 }, reinterpret_cast<const char*>(&original) -
                                                                                      static_cast<const char*>(original.getOwner()));
                std::uint8_t* const  at         = m_bytes + kMaxOwnerBytes;
                std::memcpy(at - ownerBytes, reinterpret_cast<const std::uint8_t*>(&original) - ownerBytes, ownerBytes + sizeof(::hkpCollidable));
                m_collidable           = reinterpret_cast<::hkpCollidable*>(at);
                m_collidable->m_motion = &m_transform;
            }
            StandIn(const StandIn&)            = delete;
            StandIn& operator=(const StandIn&) = delete;
        };
        static_assert(StandIn::kMaxOwnerBytes % 16 == 0 && sizeof(::hkpCollidable) == 0x70);

        /// @brief Sweeps `collidable`'s shape from `from` to `to` (Havok world space, the collidable's rotation) and returns
        ///        the closest hit: hkpWorld::linearCast over a stand-in of the collidable at `from`, under the world's read
        ///        lock.
        /// @param ignoreAboveNormalZ hits on faces flatter than this (normal z above it) are skipped.
        /// @param ignoreLayers bit n set: hits on collision layer n are skipped.
        /// @param rejectLeaving hits the motion leaves (contact normal . displacement >= 0) are skipped, as Jolt's
        ///        ContactCastCollector does: a horizontal hull sweep otherwise meets the floor it slides over.
        NativeSweepHit SweepNative(::bhkWorld* world, const ::hkpCollidable* collidable, const ::hkVector4& from, const ::hkVector4& to,
                                   float startTolerance, float ignoreAboveNormalZ, std::uint64_t ignoreLayers, bool rejectLeaving,
                                   const ph::HitAccept& accept, QueryTimer& timer)
        {
            if (!world || !collidable || !collidable->m_motion || !collidable->getShape() || collidable->getParent()) return {};
            const ::hkpWorld* havokWorld = world->GetWorld2();
            if (!havokWorld) return {};
            const StandIn standIn(*collidable, from);

            LinearCastInput input{};
            input.m_to                  = to;
            input.m_maxExtraPenetration = FLT_EPSILON;
            input.m_startPointTolerance = startTolerance;
            ClosestCollector collector{ kCollectorVtable };
            collector.m_dx                 = to(0) - from(0);
            collector.m_dy                 = to(1) - from(1);
            collector.m_dz                 = to(2) - from(2);
            collector.m_rejectLeaving      = rejectLeaving;
            collector.m_ignoreAboveNormalZ = ignoreAboveNormalZ;
            collector.m_ignoreLayers       = ignoreLayers;
            collector.m_self               = collidable;
            collector.m_accept             = &accept;

            const TimedReadLock lock(world, timer);
            havokWorld->linearCast(standIn.m_collidable, reinterpret_cast<const ::hkpLinearCastInput&>(input),
                                   reinterpret_cast<::hkpCdPointCollector&>(collector), nullptr);
            return collector.m_best;
        }

        /// @brief The material a ray hit, read as the game reads a contact's in its stairs test
        ///        (bhkCharacterController's IsStairsContact): the mesh shape seen through a MOPP, its child's material at the
        ///        hit's leaf shape key, else the shape wrapper's own.
        /// @note  The leaf key is the last valid entry of the ray's key path, as a contact's m_shapeKeyB is the leaf's.
        std::uint32_t MaterialOf(const ::hkpCollidable* body, const ::hkpWorldRayCastOutput& out)
        {
            const ::hkpShape* root = body ? body->getShape() : nullptr;
            if (!root) return 0;
            ::hkpShapeKey key = HK_INVALID_SHAPE_KEY;
            for (int i = 0; i < ::hkpShapeRayCastOutput::MAX_HIERARCHY_DEPTH && out.m_shapeKeys[i] != HK_INVALID_SHAPE_KEY; ++i) key = out.m_shapeKeys[i];
            if (const ::hkpShape* mesh = ::bhkGetMeshShape(root))
                if (const auto* wrapper = reinterpret_cast<const ::bhkShape*>(mesh->getUserData()))
                    return key != HK_INVALID_SHAPE_KEY ? wrapper->GetMaterialID(key) : wrapper->m_materialID;
            const auto* wrapper = reinterpret_cast<const ::bhkShape*>(root->getUserData());
            return wrapper ? wrapper->m_materialID : 0;
        }

    }  // namespace

    ph::ShapeRef GameWorld::CreateShape(const ph::ShapeDesc&, std::string* error)
    {
        if (error) *error = "the game's world: shapes are the game's own (wrap one in a LiveShape)";
        return nullptr;
    }

    ph::BodyId GameWorld::AddBody(const ph::BodyDesc&, std::string* error)
    {
        if (error) *error = "the game's world: bodies are the game's own";
        return {};
    }

    std::optional<ph::Hit> GameWorld::Ray(const ph::Vec3& from, const ph::Vec3& to, const ph::QueryFilter& filter, QueryTimer& timer) const
    {
        if (!m_world) return std::nullopt;
        ::bhkPickData pick;
        pick.m_rayInput.m_from        = Havok(from);
        pick.m_rayInput.m_to          = Havok(to);
        pick.m_rayInput.m_filterInfo  = filter.native ? static_cast<::hkUint32>(filter.native) : static_cast<::hkUint32>(filter.filter.layer);
        {
            const TimedReadLock lock(m_world, timer);
            m_world->PickObject(pick);
        }
        /// @note PickObject skips a pick when the game's per-frame pick time is spent (bhkPickData +0xC0, m_unkC0): the
        ///       caller reads it as no hit, so the tag counts it.
        if (pick.m_unkC0) timer.Skipped();
        if (!pick.m_rayOutput.hasHit() || !pick.m_rayOutput.m_rootCollidable) return std::nullopt;
        const ::hkpCollidable* body = pick.m_rayOutput.m_rootCollidable;
        const unsigned         layer = LayerOf(body);
        const ::hkVector4&     n     = pick.m_rayOutput.m_normal;
        if (LayerIgnored(filter.ignoreLayers, layer) || n(2) > filter.maxNormalZ) return std::nullopt;
        ph::Hit hit;
        hit.fraction = pick.m_rayOutput.m_hitFraction;
        hit.point    = { from.x + (to.x - from.x) * hit.fraction, from.y + (to.y - from.y) * hit.fraction, from.z + (to.z - from.z) * hit.fraction };
        hit.normal   = { n(0), n(1), n(2) };
        hit.layer    = static_cast<std::uint8_t>(layer);
        hit.userData = reinterpret_cast<std::uint64_t>(body);
        hit.material.id = MaterialOf(body, pick.m_rayOutput);
        if (!filter.accept.Accepts(hit)) return std::nullopt;
        return hit;
    }

    std::optional<ph::Hit> GameWorld::CastRay(const ph::Vec3& from, const ph::Vec3& to, const ph::QueryFilter& filter) const
    {
        CB_ZONE_AT(zone, m_tag->Zone());
        QueryTimer timer(m_tag->Stats());
        auto       hit = Ray(from, to, filter, timer);
        timer.Hit(hit.has_value());
        return hit;
    }

    void GameWorld::CastRayAll(const ph::Vec3& from, const ph::Vec3& to, const ph::QueryFilter& filter, std::vector<ph::Hit>& out) const
    {
        out.clear();
        CB_ZONE_AT(zone, m_tag->Zone());
        QueryTimer timer(m_tag->Stats());
        if (auto hit = Ray(from, to, filter, timer)) out.push_back(*hit);
        timer.Hit(!out.empty());
    }

    void GameWorld::CastShape(const ph::ShapeRef& shape, const ph::Transform& from, const ph::Vec3& to, const ph::QueryFilter& filter,
                              std::vector<ph::Hit>& out) const
    {
        out.clear();
        const auto* live = dynamic_cast<const LiveShape*>(shape.get());
        if (!live || !live->Collidable()) return;
        CB_ZONE_AT(zone, m_tag->Zone());
        QueryTimer           timer(m_tag->Stats());
        const NativeSweepHit s = SweepNative(m_world, live->Collidable(), Havok(from.position), Havok(to), filter.startTolerance * kToHavok,
                                             filter.maxNormalZ, filter.ignoreLayers, filter.rejectLeaving, filter.accept, timer);
        timer.Hit(s.hit);
        if (s.hit) out.push_back(HitOf(s.contact, s.fraction, s.body));
    }

}  // namespace CB::platform::physics
