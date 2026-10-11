#pragma once
// AnimDataFromBehavior — animationdata DeriveClipInputs from a behavior graph's clip generators (the merged
// SchemaObject graph the runtime compiles).
//
// This covers the CLIP-GENERATOR half of a DeriveClipInput (name, animationName, playback speed, crop, and the
// clip generator's OWN triggers). The animation ANNOTATION triggers (merged from each animation's annotation
// track) and the roster are supplied by the caller — the graph reader does not read animation files.

#include <compile/AnimDataDeriver.h>   // animdata::DeriveClipInput, ClipGenerator, DeriveClipList
#include <codec/serialization/packfile/IHavokObject.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace CB::engine::compile {

    /// @brief One DeriveClipInput per hkbClipGenerator reachable from a merged graph's `root` (the reflective path's
    ///        SchemaObject graph), trigger event ids resolved through the graph's eventNames; in clip-name order.
    std::vector<CB::engine::animdata::DeriveClipInput> DeriveClipInputsFromGraph(
        const std::shared_ptr<CB::engine::codec::IHavokObject>& root);

}  // namespace CB::engine::compile
