#include "PCH.h"

#include "CharSweep.h"
#include "CharLoad.h"

#include <Physics/Collide/Agent/Collidable/hkpCollidable.h>

#include <memory>
#include <vector>

namespace CB::feature::controller {

    namespace ph = hky::physics;

    SweepHit Sweep(::bhkWorld* world, const ::hkpCollidable* collidable, const ::hkVector4& to, float startTolerance,
                   float ignoreAboveNormalZ, std::uint64_t ignoreLayers)
    {
        if (!collidable) return {};
        return SweepFrom(world, collidable, collidable->getTransform().getTranslation(), to, startTolerance, ignoreAboveNormalZ,
                         ignoreLayers);
    }

    SweepHit SweepFrom(::bhkWorld* world, const ::hkpCollidable* collidable, const ::hkVector4& from, const ::hkVector4& to,
                       float startTolerance, float ignoreAboveNormalZ, std::uint64_t ignoreLayers, const HitFilter* filter)
    {
        if (!collidable) return {};
        CountLoad(LoadCounter::Sweeps);
        if (filter)
            return platform::physics::SweepNative(world, collidable, from, to, startTolerance, ignoreAboveNormalZ, ignoreLayers, true, filter);

        constexpr float                  k = ph::kHavokToGame;
        const platform::physics::GameWorld game(world);
        ph::QueryFilter                  q;
        q.ignoreLayers   = ignoreLayers;
        q.maxNormalZ     = ignoreAboveNormalZ;
        q.rejectLeaving  = true;
        q.startTolerance = startTolerance * k;
        ph::Transform start;
        start.position = { from(0) * k, from(1) * k, from(2) * k };
        std::vector<ph::Hit> hits;
        game.CastShape(std::make_shared<platform::physics::LiveShape>(collidable), start, { to(0) * k, to(1) * k, to(2) * k }, q, hits);
        if (hits.empty()) return {};

        const ph::Hit& h = hits.front();
        SweepHit       out;
        out.hit      = true;
        out.fraction = h.fraction;
        ::hkVector4 at, normal;
        at.set(h.point.x / k, h.point.y / k, h.point.z / k, 0.0f);
        normal.set(h.normal.x, h.normal.y, h.normal.z, 0.0f);
        out.contact.setPosition(at);
        out.contact.setSeparatingNormal(normal, h.fraction);
        out.body = reinterpret_cast<const ::hkpCollidable*>(h.userData);
        return out;
    }

}  // namespace CB::feature::controller
