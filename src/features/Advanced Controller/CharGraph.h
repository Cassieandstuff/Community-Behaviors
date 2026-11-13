#pragma once

#include <platform/physics/GameGraph.h>

#include <cstdint>
#include <string_view>
#include <variant>

/// @file
/// @brief The controller's graph-variable reads and writes, through the game's hky::runtime::IBehaviorRuntime backend
///        (platform::physics::GameGraph): one call per variable, typed as the game's own Get/SetGraphVariable* were.
/// @note  `actor` is a raw actor pointer or a smart one (NiPointer); it must not be null.
/// @note  A failed read leaves `out` as it was.

namespace CB::feature::controller {

    /// @brief The actor behind `actor`, as the backend takes it (the game's graph calls read through a const one too).
    template <class A>
    RE::Actor* GraphActor(const A& actor)
    {
        return const_cast<RE::Actor*>(static_cast<const RE::Actor*>(&*actor));
    }

    template <class A>
    bool GetGraphBool(const A& actor, std::string_view name, bool& out)
    {
        hky::runtime::Variable v = false;
        if (!platform::physics::GameGraph(GraphActor(actor)).GetVariable(name, v)) return false;
        out = std::get<bool>(v);
        return true;
    }

    template <class A>
    bool GetGraphInt(const A& actor, std::string_view name, std::int32_t& out)
    {
        hky::runtime::Variable v = std::int32_t{ 0 };
        if (!platform::physics::GameGraph(GraphActor(actor)).GetVariable(name, v)) return false;
        out = std::get<std::int32_t>(v);
        return true;
    }

    template <class A>
    bool GetGraphFloat(const A& actor, std::string_view name, float& out)
    {
        hky::runtime::Variable v = 0.0f;
        if (!platform::physics::GameGraph(GraphActor(actor)).GetVariable(name, v)) return false;
        out = std::get<float>(v);
        return true;
    }

    template <class A>
    bool GetGraphPoint(const A& actor, std::string_view name, RE::NiPoint3& out)
    {
        hky::runtime::Variable v = hky::runtime::Float4{};
        if (!platform::physics::GameGraph(GraphActor(actor)).GetVariable(name, v)) return false;
        const auto& f = std::get<hky::runtime::Float4>(v);
        out           = RE::NiPoint3{ f.x, f.y, f.z };
        return true;
    }

    template <class A>
    bool SetGraphBool(const A& actor, std::string_view name, bool value)
    {
        return platform::physics::GameGraph(GraphActor(actor)).SetVariable(name, hky::runtime::Variable{ value });
    }

    template <class A>
    bool SetGraphInt(const A& actor, std::string_view name, std::int32_t value)
    {
        return platform::physics::GameGraph(GraphActor(actor)).SetVariable(name, hky::runtime::Variable{ value });
    }

    template <class A>
    bool SetGraphFloat(const A& actor, std::string_view name, float value)
    {
        return platform::physics::GameGraph(GraphActor(actor)).SetVariable(name, hky::runtime::Variable{ value });
    }

    template <class A>
    bool SetGraphPoint(const A& actor, std::string_view name, const RE::NiPoint3& value)
    {
        return platform::physics::GameGraph(GraphActor(actor)).SetVariable(name, hky::runtime::Variable{ hky::runtime::Float4{ value.x, value.y, value.z, 0.0f } });
    }

}  // namespace CB::feature::controller
