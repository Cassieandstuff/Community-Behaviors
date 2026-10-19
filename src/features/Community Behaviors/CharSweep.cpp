#include "PCH.h"

#include "CharSweep.h"

#include <core/hooks/hookslib.h>

#include <cfloat>
#include <cstddef>

namespace CB::feature::character {

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
        };

        void* CollectorDtor(ClosestCollector* self, unsigned) { return self; }

        void CollectorAdd(ClosestCollector* self, const CdPoint& point)
        {
            const float d = point.m_contact.getDistance();
            if (d >= self->m_earlyOutDistance) return;
            const hk::hkVector4& n = point.m_contact.m_separatingNormal;
            if (n(0) * self->m_dx + n(1) * self->m_dy + n(2) * self->m_dz >= 0.0f) return;
            if (n(2) > self->m_ignoreAboveNormalZ) return;
            self->m_earlyOutDistance = d;
            const hk::hkpCdBody* root = point.m_cdBodyB;
            while (root && root->getParent()) root = root->getParent();
            if (root && self->m_ignoreLayers) {
                const unsigned layer = static_cast<const hk::hkpCollidable*>(root)->getCollisionFilterInfo() & 0x7f;
                if (layer < 64 && ((self->m_ignoreLayers >> layer) & 1u)) return;
            }
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

        /// @brief The world's read lock, through the pinned out-of-line entry points (the schema's bhkWorldLock would call
        ///        them as members, which the plugin does not link).
        class ReadLock {
        public:
            explicit ReadLock(hk::bhkWorld* world) : m_world(world)
            {
                static const auto enter = reinterpret_cast<void (*)(hk::bhkWorld*)>(core::hooks::engine::Resolve(core::hooks::engine::bhkWorld_HavokWorldEnterRead));
                if (m_world && enter) enter(m_world);
            }
            ~ReadLock()
            {
                static const auto exit = reinterpret_cast<void (*)(hk::bhkWorld*)>(core::hooks::engine::Resolve(core::hooks::engine::bhkWorld_HavokWorldExitRead));
                if (m_world && exit) exit(m_world);
            }
            ReadLock(const ReadLock&)            = delete;
            ReadLock& operator=(const ReadLock&) = delete;

        private:
            hk::bhkWorld* m_world;
        };

    }  // namespace

    SweepHit Sweep(hk::bhkWorld* world, const hk::hkpCollidable* collidable, const hk::hkVector4& to, float startTolerance,
                   float ignoreAboveNormalZ, std::uint64_t ignoreLayers)
    {
        using LinearCastFn = void (*)(const hk::hkpWorld*, const hk::hkpCollidable*, const LinearCastInput&, ClosestCollector&, void*);
        static const auto linearCast = reinterpret_cast<LinearCastFn>(core::hooks::engine::Resolve(core::hooks::engine::hkpWorld_LinearCast));
        if (!world || !collidable || !linearCast) return {};
        const hk::hkpWorld* havokWorld = world->GetWorld2();
        if (!havokWorld) return {};

        LinearCastInput input{};
        input.m_to                  = to;
        input.m_maxExtraPenetration = FLT_EPSILON;
        input.m_startPointTolerance = startTolerance;
        ClosestCollector collector{ kCollectorVtable };
        const hk::hkVector4& from = collidable->getTransform().getTranslation();
        collector.m_dx                 = to(0) - from(0);
        collector.m_dy                 = to(1) - from(1);
        collector.m_dz                 = to(2) - from(2);
        collector.m_ignoreAboveNormalZ = ignoreAboveNormalZ;
        collector.m_ignoreLayers       = ignoreLayers;

        ReadLock lock(world);
        linearCast(havokWorld, collidable, input, collector, nullptr);
        return collector.m_best;
    }

}  // namespace CB::feature::character
