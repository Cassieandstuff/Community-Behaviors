#pragma once

#include <PCH.h>

#include <cstddef>

class hkbCharacter;

/// @file
/// @brief The game objects a Havok character belongs to: the BShkbAnimationGraph it is embedded in and the actor
///        holding that graph.

namespace CB::platform::game {

    /// @return the graph `character` is embedded in, null for null.
    /// @pre    `character` is a graph's own characterInstance, as hkbContext::m_character always is.
    inline RE::BShkbAnimationGraph* GraphOf(const ::hkbCharacter* character)
    {
        if (!character) return nullptr;
        return reinterpret_cast<RE::BShkbAnimationGraph*>(const_cast<char*>(reinterpret_cast<const char*>(character)) -
                                                          offsetof(RE::BShkbAnimationGraph, characterInstance));
    }

    /// @return the actor whose graph holds `character`, null when it is not an actor's.
    inline RE::Actor* ActorOf(const ::hkbCharacter* character)
    {
        const RE::BShkbAnimationGraph* graph = GraphOf(character);
        RE::Actor*                     actor = graph ? graph->holder : nullptr;
        return actor && actor->Is(RE::FormType::ActorCharacter) ? actor : nullptr;
    }

}  // namespace CB::platform::game
