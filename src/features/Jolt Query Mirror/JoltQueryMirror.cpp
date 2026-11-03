#if defined(CB_WITH_JOLT)

#include "PCH.h"

#include <BSJolt/bhkJoltHavokShapes.h>
#include <BSJolt/bhkJoltQueryMirror.h>
#include <BSJolt/bhkTripwire.h>

#include <platform/hooks/hookslib.h>

#include <Physics/Dynamics/Entity/hkpRigidBody.h>
#include <Physics/Dynamics/World/hkpWorld.h>
#include <BSHavok/bhkCollisionFilter.h>
#include <BSHavok/bhkPickData.h>
#include <BSHavok/bhkWorld.h>

#include "core/debug/Settings.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

/// @file
/// @brief Step 2 of the Jolt takeover (the SDK's Docs/jolt-takeover.md), observe-only: mirrors each world's fixed bodies
///        into Jolt and compares every bhkWorld::PickObject against the same ray in Jolt. Nothing it computes reaches
///        the game. Built only with CB_WITH_JOLT, and inert unless [JoltMirror] bEnable is set.
/// @note jolt_tripwire separately page-guards a sample of the rigid bodies added to a world and reports which code
///       outside the physics image ranges touches one. Guarding every body hung the first load: each guard also traps
///       its neighbours on the page, so most of Havok's heap faulted. Moving bodies are favored, since they are what
///       gameplay code reads. Development runs only.
/// @note Fixed bodies moved after being added aren't followed, so a moved one shows up as a mismatch.

namespace CB::feature::joltmirror {

    namespace {

        namespace hooks = CB::platform::hooks;

        using AddEntity    = hooks::Override<&::hkpWorld::addEntity>;
        using RemoveEntity = hooks::Override<&::hkpWorld::removeEntity>;
        using AddBatch     = hooks::Override<&::hkpWorld::addEntityBatch>;
        using RemoveBatch  = hooks::Override<&::hkpWorld::removeEntityBatch>;
        using PickObject   = hooks::Override<&::bhkWorld::PickObject>;

        constexpr int kMaxMismatchLogs = 200;
        constexpr auto kSummaryPeriod  = std::chrono::seconds(60);
        constexpr std::size_t kMaxWatchedMoving = 192;
        constexpr std::size_t kMaxWatchedFixed  = 64;

        // The ledger's physics image ranges (Tools/cbmatch/physics-ledger): rightful owners of a rigid body's memory.
        constexpr std::uintptr_t kPhysicsRanges[][2] = { { 0xa88000, 0xc20000 }, { 0xe80000, 0xf40000 } };

        bool MirrorOn()
        {
            static const bool on = core::debug::FlagOn("jolt_query_mirror");
            return on;
        }

        bool TripwireOn()
        {
            static const bool on = core::debug::FlagOn("jolt_tripwire");
            return on;
        }

        std::mutex                                                               s_lock;
        std::unordered_map<const ::hkpWorld*, std::unique_ptr<bhkJoltQueryMirror>> s_mirrors;
        bhkJoltQueryComparison                                                   s_comparison;
        std::atomic<std::uint64_t>                                               s_picksSkipped{ 0 };
        std::atomic<std::uint64_t>                                               s_bodies{ 0 };
        std::atomic<std::uint64_t>                                               s_triangles{ 0 };
        std::atomic<std::uint64_t>                                               s_unsupported{ 0 };
        std::atomic<std::uint64_t>                                               s_mixedFilters{ 0 };
        std::atomic<std::uint64_t>                                               s_convertMicros{ 0 };
        std::atomic<int>                                                         s_mismatchLogs{ 0 };

        std::mutex                                     s_watchLock;
        std::unordered_map<const ::hkpEntity*, bool>   s_watched;   ///< entity -> is fixed
        std::size_t                                    s_numWatchedMoving = 0;
        std::size_t                                    s_numWatchedFixed  = 0;

        void WatchSample(::hkpEntity* entity)
        {
            const bool fixed = static_cast<const ::hkpRigidBody*>(entity)->getMotionType() == ::hkpMotion::MOTION_FIXED;
            std::scoped_lock lock(s_watchLock);
            std::size_t& count = fixed ? s_numWatchedFixed : s_numWatchedMoving;
            if (count >= (fixed ? kMaxWatchedFixed : kMaxWatchedMoving) || s_watched.count(entity)) return;
            if (bhkTripwire::Watch(entity, sizeof(::hkpRigidBody), "hkpRigidBody")) {
                s_watched.emplace(entity, fixed);
                ++count;
            }
        }

        void UnwatchSample(::hkpEntity* entity)
        {
            std::scoped_lock lock(s_watchLock);
            const auto it = s_watched.find(entity);
            if (it == s_watched.end()) return;
            bhkTripwire::Unwatch(entity);
            --(it->second ? s_numWatchedFixed : s_numWatchedMoving);
            s_watched.erase(it);
        }

        void LogTripwire();

        void EnsureRuntime()
        {
            static std::once_flag once;
            std::call_once(once, [] {
                bhkJoltRuntime::Initialize();
                LOG_INFO("JoltMirror: runtime up, {} workers", bhkJoltRuntime::GetWorkerCount());
                if (TripwireOn()) {
                    bhkTripwire::Install();
                    const std::uintptr_t base = REL::Module::get().base();
                    for (const auto& range : kPhysicsRanges) {
                        bhkTripwire::AllowCode(reinterpret_cast<const void*>(base + range[0]), range[1] - range[0]);
                    }
                    // Its own thread: a stuck or slow load never reaches a pick, and that's when the report matters.
                    std::thread([] {
                        for (;;) {
                            std::this_thread::sleep_for(kSummaryPeriod);
                            LogTripwire();
                        }
                    }).detach();
                }
            });
        }

        /// @note Copied from the live filter, at the offsets bhkCollisionRules documents.
        bhkCollisionRules ReadRules()
        {
            bhkCollisionRules rules{};
            const auto* filter = reinterpret_cast<const std::uint8_t*>(::bhkCollisionFilter::GetSingleton());
            if (filter) {
                std::memcpy(rules.m_bonePairs, filter + 0x50, sizeof(rules.m_bonePairs));
                std::memcpy(rules.m_layerMasks, filter + 0x1d0, sizeof(rules.m_layerMasks));
                std::memcpy(&rules.m_crossGroupLayers, filter + 0x3d0, sizeof(rules.m_crossGroupLayers));
            } else {
                LOG_WARN("JoltMirror: no collision filter yet; rays will disagree");
            }
            return rules;
        }

        bhkJoltQueryMirror& MirrorOf(const ::hkpWorld* world)
        {
            auto& mirror = s_mirrors[world];
            if (!mirror) {
                mirror = std::make_unique<bhkJoltQueryMirror>(ReadRules());
                LOG_INFO("JoltMirror: new world {:x}", reinterpret_cast<std::uintptr_t>(world));
            }
            return *mirror;
        }

        void MirrorBody(const ::hkpWorld* world, const ::hkpEntity* entity)
        {
            const auto* body = static_cast<const ::hkpRigidBody*>(entity);
            if (body->getMotionType() != ::hkpMotion::MOTION_FIXED) return;

            const auto start = std::chrono::steady_clock::now();
            bhkJoltHavokShapes::Stats stats;
            const JPH::RefConst<JPH::Shape> shape =
                bhkJoltHavokShapes::Convert(reinterpret_cast<const ::hkpShape*>(body->getCollidable()->getShape()), stats);
            s_convertMicros += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
            s_triangles += stats.m_triangles;
            s_unsupported += stats.m_unsupported;
            s_mixedFilters += stats.m_mixedChildFilters;
            if (!shape) return;

            // hkTransform: three rotation columns, then the translation, four floats each.
            const auto* m = reinterpret_cast<const float*>(&body->getTransform());
            const JPH::Mat44 matrix(JPH::Vec4(m[0], m[1], m[2], 0.0f), JPH::Vec4(m[4], m[5], m[6], 0.0f), JPH::Vec4(m[8], m[9], m[10], 0.0f),
                                    JPH::Vec4(m[12], m[13], m[14], 1.0f));
            const std::uint32_t filter = body->getCollidable()->getCollisionFilterInfo();

            std::scoped_lock lock(s_lock);
            if (MirrorOf(world).AddStatic(reinterpret_cast<std::uint64_t>(entity), shape, matrix.GetTranslation(), matrix.GetQuaternion().Normalized(), filter)) {
                ++s_bodies;
            }
        }

        void Added(const ::hkpWorld* world, ::hkpEntity* entity)
        {
            if (!entity) return;
            EnsureRuntime();
            if (MirrorOn()) MirrorBody(world, entity);
            if (TripwireOn()) WatchSample(entity);
        }

        void Removing(const ::hkpWorld* world, ::hkpEntity* entity)
        {
            if (!entity) return;
            if (TripwireOn()) UnwatchSample(entity);
            if (!MirrorOn()) return;
            std::scoped_lock lock(s_lock);
            const auto it = s_mirrors.find(world);
            if (it != s_mirrors.end() && it->second->Remove(reinterpret_cast<std::uint64_t>(entity))) {
                --s_bodies;
            }
        }

        void LogSummary()
        {
            static std::atomic<std::int64_t> s_next{ 0 };
            const std::int64_t now = std::chrono::steady_clock::now().time_since_epoch().count();
            std::int64_t next = s_next.load();
            if (now < next || !s_next.compare_exchange_strong(next, now + std::chrono::steady_clock::duration(kSummaryPeriod).count())) return;

            using R = bhkJoltQueryComparison::Result;
            const auto count = [](R r) { return s_comparison.m_counts[int(r)].load(); };
            LOG_INFO("JoltMirror: agree {} | distance {} | jolt missed {} | jolt extra {} | inconclusive {} | skipped {}", count(R::Agree),
                     count(R::FractionMismatch), count(R::JoltMissed), count(R::JoltExtra), count(R::Inconclusive), s_picksSkipped.load());
            LOG_INFO("JoltMirror: {} bodies, {} triangles, {} unsupported shapes, {} mixed child filters, {} ms converting", s_bodies.load(),
                     s_triangles.load(), s_unsupported.load(), s_mixedFilters.load(), s_convertMicros.load() / 1000);
        }

        void LogTripwire()
        {
            std::vector<bhkTripwire::Hit> hits(8192);
            const std::size_t n = bhkTripwire::GetHits(hits.data(), hits.size());
            const std::uintptr_t base = REL::Module::get().base();
            std::size_t moving, fixed;
            {
                std::scoped_lock lock(s_watchLock);
                moving = s_numWatchedMoving;
                fixed  = s_numWatchedFixed;
            }
            LOG_INFO("JoltMirror: tripwire watching {} moving and {} fixed bodies; {} distinct readers, {} dropped", moving, fixed, n, bhkTripwire::GetDroppedHits());
            for (std::size_t i = 0; i < std::min(n, hits.size()); ++i) {
                LOG_INFO("JoltMirror: tripwire rva {:x} +{:x} {} x{}", reinterpret_cast<std::uintptr_t>(hits[i].m_code) - base, hits[i].m_offset,
                         hits[i].m_write ? "write" : "read", hits[i].m_count);
            }
        }

        void Compare(::bhkWorld* world, const ::bhkPickData& pick)
        {
            // A pick over its time budget, or one feeding a collector, leaves no comparable result in the output.
            if (pick.m_unkC0 || pick.m_rayHitCollectorA0 || pick.m_rayHitCollectorA8 || pick.m_rayHitCollectorB0 || pick.m_rayHitCollectorB8) {
                ++s_picksSkipped;
                return;
            }
            const ::hkpWorld* havokWorld = world->GetWorld2();
            const ::hkVector4& f = pick.m_rayInput.m_from;
            const ::hkVector4& t = pick.m_rayInput.m_to;
            const JPH::Vec3 from(f(0), f(1), f(2));
            const JPH::Vec3 to(t(0), t(1), t(2));

            bhkJoltQueryMirror::RayHit hit;
            {
                std::scoped_lock lock(s_lock);
                const auto it = s_mirrors.find(havokWorld);
                if (it == s_mirrors.end()) {
                    ++s_picksSkipped;
                    return;
                }
                hit = it->second->CastRay(from, to, pick.m_rayInput.m_filterInfo);
            }

            const ::hkpCollidable* root = pick.m_rayOutput.m_rootCollidable;
            const auto* owner = root ? static_cast<const ::hkpRigidBody*>(root->getOwner()) : nullptr;
            bhkJoltQueryComparison::Ray ray;
            ray.m_havokHit = root != nullptr;
            ray.m_havokFraction = pick.m_rayOutput.m_hitFraction;
            ray.m_havokHitStatic = owner && owner->getMotionType() == ::hkpMotion::MOTION_FIXED;
            ray.m_length = (to - from).Length();

            const auto result = s_comparison.Record(ray, hit);
            if (result != bhkJoltQueryComparison::Result::Agree && result != bhkJoltQueryComparison::Result::Inconclusive &&
                s_mismatchLogs.fetch_add(1) < kMaxMismatchLogs) {
                LOG_INFO("JoltMirror: mismatch {} from ({:.2f} {:.2f} {:.2f}) to ({:.2f} {:.2f} {:.2f}) filter {:x}: havok {:.4f} body {:x}, jolt {} {:.4f} body {:x}",
                         int(result), from.GetX(), from.GetY(), from.GetZ(), to.GetX(), to.GetY(), to.GetZ(), pick.m_rayInput.m_filterInfo,
                         ray.m_havokFraction, reinterpret_cast<std::uintptr_t>(owner), hit.m_hit, hit.m_fraction, hit.m_key);
            }
        }

        ::hkpEntity* OnAddEntity(AddEntity::Next next, ::hkpWorld* world, ::hkpEntity* entity, ::hkpEntityActivation activation)
        {
            ::hkpEntity* result = next(world, entity, activation);
            Added(world, entity);
            return result;
        }

        ::hkBool OnRemoveEntity(RemoveEntity::Next next, ::hkpWorld* world, ::hkpEntity* entity)
        {
            Removing(world, entity);
            return next(world, entity);
        }

        void OnAddBatch(AddBatch::Next next, ::hkpWorld* world, ::hkpEntity* const* entities, int count, ::hkpEntityActivation activation)
        {
            next(world, entities, count, activation);
            for (int i = 0; i < count; ++i) Added(world, entities[i]);
        }

        void OnRemoveBatch(RemoveBatch::Next next, ::hkpWorld* world, ::hkpEntity* const* entities, int count)
        {
            for (int i = 0; i < count; ++i) Removing(world, entities[i]);
            next(world, entities, count);
        }

        bool OnPickObject(PickObject::Next next, ::bhkWorld* world, ::bhkPickData& pick)
        {
            const bool result = next(world, pick);
            if (MirrorOn()) {
                Compare(world, pick);
                LogSummary();
            }
            return result;
        }

        [[maybe_unused]] const bool s_addEntity    = AddEntity::Subscribe(&OnAddEntity, hooks::Priority::Normal, "JoltQueryMirror");
        [[maybe_unused]] const bool s_removeEntity = RemoveEntity::Subscribe(&OnRemoveEntity, hooks::Priority::Normal, "JoltQueryMirror");
        [[maybe_unused]] const bool s_addBatch     = AddBatch::Subscribe(&OnAddBatch, hooks::Priority::Normal, "JoltQueryMirror");
        [[maybe_unused]] const bool s_removeBatch  = RemoveBatch::Subscribe(&OnRemoveBatch, hooks::Priority::Normal, "JoltQueryMirror");
        [[maybe_unused]] const bool s_pickObject   = PickObject::Subscribe(&OnPickObject, hooks::Priority::Normal, "JoltQueryMirror");

    }  // namespace

}  // namespace CB::feature::joltmirror

#endif  // CB_WITH_JOLT
