#include "PCH.h"

#include "CharSweep.h"

#include <cfloat>
#include <cstddef>
#include <cstring>

namespace CB::feature::controller {

    namespace {

        namespace hk = CB::schema;

        /// @brief hkpCdPoint as vanilla's hkpAllCdPointCollector::addCdPoint (0x140b1d930) reads it.
        /// @note 0x40 bytes: the contact is copied whole, the unwelded normal skipped, and each body walked up m_parent
        ///       (+0x18) to its root collidable with its shape key read at +0x08.
        struct CdPoint {
            hk::hkContactPoint   m_contact;          ///< +0x00
            hk::hkVector4        m_unweldedNormal;   ///< +0x20
            const hk::hkpCdBody* m_cdBodyA;          ///< +0x30 — the swept collidable's side
            const hk::hkpCdBody* m_cdBodyB;          ///< +0x38 — the hit body's side
        };
        static_assert(sizeof(CdPoint) == 0x40 && offsetof(CdPoint, m_cdBodyA) == 0x30 && offsetof(CdPoint, m_cdBodyB) == 0x38);

        /// @brief hkpLinearCastInput as hkpWorldLinearCaster (0x140bea990) reads it: m_to, then the float it keeps aside,
        ///        then the tolerance it widens the shape's AABB by.
        struct LinearCastInput {
            hk::hkVector4 m_to;                    ///< +0x00
            float         m_maxExtraPenetration;   ///< +0x10
            float         m_startPointTolerance;   ///< +0x14
            std::uint8_t  m_pad18[8];
        };
        static_assert(sizeof(LinearCastInput) == 0x20 && offsetof(LinearCastInput, m_startPointTolerance) == 0x14);

        /// @brief A closest-hit cd-point collector with vanilla's collector vtable shape.
        /// @note Vanilla's hkpAllCdPointCollector vtable (0x1417ed228) is three slots: scalar deleting destructor,
        ///       addCdPoint, and a VIRTUAL reset (0x1403f8c00, which the schema header declares non-virtual), so this
        ///       class builds its own three-slot table instead of deriving from the schema's.
        /// @note A linear cast reports a hit's path fraction in the contact's distance (w of the separating normal);
        ///       the caster skips hits beyond m_earlyOutDistance, so the collector keeps the smallest and lowers it.
        struct ClosestCollector {
            const void* const* m_vtable;
            float              m_earlyOutDistance = FLT_MAX;   ///< +0x08, read by the caster
            SweepHit           m_best;
            float              m_dx = 0, m_dy = 0, m_dz = 0;   ///< the path, for the moving-away test
            float              m_ignoreAboveNormalZ = 2.0f;
            std::uint64_t      m_ignoreLayers       = 0;
            const hk::hkpCollidable* m_self         = nullptr;   ///< a stand-in's original, never a hit (SweepFrom)
        };

        void* CollectorDtor(ClosestCollector* self, unsigned) { return self; }

        /// @note Skipped hits never lower m_earlyOutDistance: a nearer ignored body (another character, the swept
        ///       phantom itself) must not hide a real hit beyond it.
        void CollectorAdd(ClosestCollector* self, const CdPoint& point)
        {
            const float d = point.m_contact.getDistance();
            if (d >= self->m_earlyOutDistance) return;
            const hk::hkVector4& n = point.m_contact.m_separatingNormal;
            if (n(0) * self->m_dx + n(1) * self->m_dy + n(2) * self->m_dz >= 0.0f) return;
            if (n(2) > self->m_ignoreAboveNormalZ) return;
            const hk::hkpCdBody* root = point.m_cdBodyB;
            while (root && root->getParent()) root = root->getParent();
            if (root && root == self->m_self) return;
            if (root && self->m_ignoreLayers) {
                const unsigned layer = static_cast<const hk::hkpCollidable*>(root)->getCollisionFilterInfo() & 0x7f;
                if (layer < 64 && ((self->m_ignoreLayers >> layer) & 1u)) return;
            }
            self->m_earlyOutDistance = d;
            self->m_best.hit      = true;
            self->m_best.fraction = d;
            self->m_best.contact  = point.m_contact;
            self->m_best.body     = static_cast<const hk::hkpCollidable*>(root);
        }

        void CollectorReset(ClosestCollector* self)
        {
            self->m_earlyOutDistance = FLT_MAX;
            self->m_best             = SweepHit{};
        }

        const void* const kCollectorVtable[3] = { reinterpret_cast<const void*>(&CollectorDtor), reinterpret_cast<const void*>(&CollectorAdd),
                                                   reinterpret_cast<const void*>(&CollectorReset) };

    }  // namespace

    namespace {

        /// @pre `castA` is `self`'s stand-in (SweepFrom) or the collidable itself (then `self` is null); `from` is castA's
        ///      translation.
        SweepHit Cast(hk::bhkWorld* world, const hk::hkpCollidable* castA, const hk::hkpCollidable* self, const hk::hkVector4& from,
                      const hk::hkVector4& to, float startTolerance, float ignoreAboveNormalZ, std::uint64_t ignoreLayers)
        {
            if (!world || !castA) return {};
            const hk::hkpWorld* havokWorld = world->GetWorld2();
            if (!havokWorld) return {};

            LinearCastInput input{};
            input.m_to                  = to;
            input.m_maxExtraPenetration = FLT_EPSILON;
            input.m_startPointTolerance = startTolerance;
            ClosestCollector collector{ kCollectorVtable };
            collector.m_dx                 = to(0) - from(0);
            collector.m_dy                 = to(1) - from(1);
            collector.m_dz                 = to(2) - from(2);
            collector.m_ignoreAboveNormalZ = ignoreAboveNormalZ;
            collector.m_ignoreLayers       = ignoreLayers;
            collector.m_self               = self;

            const hk::bhkWorldLock lock(world, 0);
            havokWorld->linearCast(castA, reinterpret_cast<const hk::hkpLinearCastInput&>(input), reinterpret_cast<hk::hkpCdPointCollector&>(collector),
                                   nullptr);
            return collector.m_best;
        }

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
            alignas(16) std::uint8_t        m_bytes[kMaxOwnerBytes + sizeof(hk::hkpCollidable)];
            alignas(16) hk::hkTransform     m_transform;
            hk::hkpCollidable*              m_collidable = nullptr;

            StandIn(const hk::hkpCollidable& original, const hk::hkVector4& from)
            {
                std::memcpy(static_cast<void*>(&m_transform), &original.getTransform(), sizeof(m_transform));
                m_transform.m_translation = from;
                const std::ptrdiff_t ownerBytes = original.m_ownerOffset < 0 ? -static_cast<std::ptrdiff_t>(original.m_ownerOffset) : 0;
                std::uint8_t* const  at         = m_bytes + kMaxOwnerBytes;
                std::memcpy(at - ownerBytes, reinterpret_cast<const std::uint8_t*>(&original) - ownerBytes, ownerBytes + sizeof(hk::hkpCollidable));
                m_collidable           = reinterpret_cast<hk::hkpCollidable*>(at);
                m_collidable->m_motion = &m_transform;
            }
            StandIn(const StandIn&)            = delete;
            StandIn& operator=(const StandIn&) = delete;
        };
        static_assert(StandIn::kMaxOwnerBytes % 16 == 0 && sizeof(hk::hkpCollidable) == 0x70);

    }  // namespace

    SweepHit Sweep(hk::bhkWorld* world, const hk::hkpCollidable* collidable, const hk::hkVector4& to, float startTolerance,
                   float ignoreAboveNormalZ, std::uint64_t ignoreLayers)
    {
        if (!collidable) return {};
        return Cast(world, collidable, nullptr, collidable->getTransform().getTranslation(), to, startTolerance, ignoreAboveNormalZ,
                    ignoreLayers);
    }

    SweepHit SweepFrom(hk::bhkWorld* world, const hk::hkpCollidable* collidable, const hk::hkVector4& from, const hk::hkVector4& to,
                       float startTolerance, float ignoreAboveNormalZ, std::uint64_t ignoreLayers)
    {
        if (!collidable || !collidable->m_motion || !collidable->getShape() || collidable->getParent()) return {};
        const StandIn standIn(*collidable, from);
        return Cast(world, standIn.m_collidable, collidable, from, to, startTolerance, ignoreAboveNormalZ, ignoreLayers);
    }

}  // namespace CB::feature::controller
