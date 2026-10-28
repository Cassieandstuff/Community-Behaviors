#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// @file
/// @brief CB::engine::trace — one actor's behavior-graph path for one update, as plain data: which nodes ran and how much
///        each counted, each state machine's current state, the transitions in progress and the events raised. Every
///        object is named by where it sits in the packfile the runtime loaded (the file and the object's offset in its
///        data section), so a reader resolves it to a FormID through that file's ObjectOrigin table (SerializeGraph).
/// @note  Filled by a runtime's collector (Behavior Kit's live graph; CB's in-game tracer), read by whoever draws it. It
///        holds no pointers into a runtime, so a frame can be kept, copied and compared after the graph moved on.

namespace CB::engine::trace {

    /// @brief An object of a loaded behavior file: the file (an index into Frame::files) and the object's offset in that
    ///        packfile's data section, as SerializeGraph reported it.
    struct NodeRef {
        std::uint32_t file   = 0xFFFFFFFFu;
        std::uint32_t offset = 0xFFFFFFFFu;
        [[nodiscard]] bool Valid() const { return file != 0xFFFFFFFFu; }
        friend bool operator==(const NodeRef&, const NodeRef&) = default;
    };

    /// @brief One wire that carried the update: `node` ran under `parent` (invalid for the root generator), and counts
    ///        for `weight` of the output, cumulative from the root (a child of a blender child weighted 0.3 at full own
    ///        weight counts 0.3).
    struct Visit {
        NodeRef node, parent;
        float   weight = 1.0f;
    };

    /// @brief A state machine's current state.
    struct State {
        NodeRef      machine;
        NodeRef      state;      ///< the state's StateInfo object
        std::int32_t stateId = -1;
    };

    /// @brief A transition in progress (or begun this update) in a state machine.
    struct Transition {
        NodeRef      machine;
        NodeRef      from, to;            ///< StateInfo objects
        NodeRef      transitions;         ///< the TransitionInfoArray holding it: the from state's, or for a wildcard the
                                          ///< wildcard array of the machine that owns it (this one, or a global one's)
        std::int32_t index    = -1;       ///< its index in that array
        bool         wildcard = false;
        float        progress = 0.0f;     ///< 0 at the start of the blend, 1 at its end
        bool         began    = false;    ///< it fired this update
    };

    /// @brief An event the graph raised or handled this update, with the node that raised it when known.
    struct Event {
        std::string name;
        NodeRef     sender;
    };

    struct Frame {
        std::vector<std::string> files;   ///< the loaded behavior files' Data paths, as the runtime read them
        std::vector<Visit>       visits;
        std::vector<State>       states;
        std::vector<Transition>  transitions;
        std::vector<Event>       events;
        double                   time = 0.0;   ///< seconds since the actor's graph started
    };

    /// @brief Where a serialized object came from: its offset in the packfile's data section and its unit FormID.
    struct ObjectOrigin {
        std::uint32_t offset = 0;
        std::string   formId;      ///< the unit's FormID text for a file node; empty for an object the unit keeps inline
        std::string   className;
        std::string   owner;       ///< for an inline object: the FormID of the file node that holds it (the first, in write
                                   ///< order, to reference it); empty for a file node
    };

}  // namespace CB::engine::trace
