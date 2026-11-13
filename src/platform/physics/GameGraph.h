#pragma once

#include <hky/runtime/BehaviorRuntime.h>

#include <string_view>

namespace RE {
    class Actor;
}

/// @file
/// @brief CB's in-game backend of hky::runtime::IBehaviorRuntime: one actor's animation graphs, through the game's own
///        graph-variable and event calls (IAnimationGraphManagerHolder).
/// @note  A variable's type is the one the caller holds in the Variable it passes (GetVariable) or sets (SetVariable):
///        bool, int32, float, or Float4 for a vector (the game's NiPoint3: x, y, z; w reads 0 and is never written).
/// @note  LastUpdate is empty: the game keeps no trace of a graph's update.
/// @note  Names go to the game's string pool as written; the pool's own matching applies (it folds case).

namespace CB::platform::physics {

class GameGraph final : public hky::runtime::IBehaviorRuntime {
public:
    /// @param actor not owned; null answers false to everything.
    explicit GameGraph(RE::Actor* actor) : m_actor(actor) {}

    bool GetVariable(std::string_view name, hky::runtime::Variable& out) const override;
    bool SetVariable(std::string_view name, const hky::runtime::Variable& value) override;
    bool SendEvent(std::string_view name) override;
    [[nodiscard]] const hky::trace::Frame& LastUpdate() const override;

private:
    RE::Actor* m_actor;
};

}  // namespace CB::platform::physics
