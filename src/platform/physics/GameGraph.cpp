#include "PCH.h"

#include "platform/physics/GameGraph.h"

#include <hky/interface/GraphTrace.h>

#include <cstdint>
#include <string>
#include <variant>

namespace CB::platform::physics {

    namespace {

        /// @note BSFixedString's view constructor reads up to a terminator, so a view becomes a string first.
        RE::BSFixedString Pooled(std::string_view name) { return RE::BSFixedString(std::string(name).c_str()); }

    }  // namespace

    bool GameGraph::GetVariable(std::string_view name, hky::runtime::Variable& out) const
    {
        if (!m_actor) return false;
        const RE::BSFixedString n = Pooled(name);
        if (auto* b = std::get_if<bool>(&out)) return m_actor->GetGraphVariableBool(n, *b);
        if (auto* i = std::get_if<std::int32_t>(&out)) return m_actor->GetGraphVariableInt(n, *i);
        if (auto* f = std::get_if<float>(&out)) return m_actor->GetGraphVariableFloat(n, *f);
        if (auto* v = std::get_if<hky::runtime::Float4>(&out)) {
            RE::NiPoint3 p;
            if (!m_actor->GetGraphVariableNiPoint3(n, p)) return false;
            *v = { p.x, p.y, p.z, 0.0f };
            return true;
        }
        return false;
    }

    bool GameGraph::SetVariable(std::string_view name, const hky::runtime::Variable& value)
    {
        if (!m_actor) return false;
        const RE::BSFixedString n = Pooled(name);
        if (const auto* b = std::get_if<bool>(&value)) return m_actor->SetGraphVariableBool(n, *b);
        if (const auto* i = std::get_if<std::int32_t>(&value)) return m_actor->SetGraphVariableInt(n, *i);
        if (const auto* f = std::get_if<float>(&value)) return m_actor->SetGraphVariableFloat(n, *f);
        if (const auto* v = std::get_if<hky::runtime::Float4>(&value)) {
            RE::NiPoint3 p{ v->x, v->y, v->z };
            return m_actor->SetGraphVariableNiPoint3(n, p);
        }
        return false;
    }

    bool GameGraph::SendEvent(std::string_view name)
    {
        return m_actor && m_actor->NotifyAnimationGraph(Pooled(name));
    }

    const hky::trace::Frame& GameGraph::LastUpdate() const
    {
        static const hky::trace::Frame kNone{};
        return kNone;
    }

}  // namespace CB::platform::physics
