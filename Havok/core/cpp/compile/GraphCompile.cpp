#include <compile/GraphCompile.h>

#include <codec/serialization/packfile/PackFileSerializer.h>   // PackFileSerializer + BinaryWriterEx

#include <havok-model/HavokModel.h>    // model::AssembleGraph/AssembleCharacter/AssembleProject + ResolveBehaviorBindings
#include <interface/BehaviorData.h>  // model::BehaviorData full def (copied for the resolve pass)
#include <havok-schema/HavokSchema.h>  // schema::SharedRegistry

#include <atomic>
#include <exception>
#include <memory>

namespace CB::core::compile {
namespace {

std::atomic<std::size_t> g_compiled{0};   // graphs+characters served this process (schema-only telemetry)

// hkbBehaviorGraph::nextUniqueID (the runtime node-id minter) is a SIGNED 16-bit counter (RE'd), so a
// graph at/over this many nodes mints negative ids -> the id collides with Havok's -1="none" sentinel
// space, corrupting the graph and (via the baked animationsetdata count) SAVES. Refuse before that.
constexpr std::size_t kMaxGraphNodes = 0x7FFF;   // 32767

// Count the hkbNode-derived graph nodes (each gets a nextUniqueID at activation). Excludes the owned
// data arrays (expression/eventRange/boneIndex — hkReferencedObjects, not hkbNodes) and the roster.
std::size_t behaviorNodeCount(const havok::model::BehaviorData& d) {
    return d.clips.size() + d.blenders.size() + d.selectors.size() + d.stateMachines.size()
         + d.states.size() + d.transitionEffects.size() + d.modifierGenerators.size()
         + d.isActiveModifiers.size() + d.stateTaggingGenerators.size() + d.behaviorReferences.size()
         + d.gamebryoSequences.size() + d.modifierLists.size() + d.cyclicBlendGenerators.size()
         + d.eventDrivenModifiers.size() + d.eventEveryNModifiers.size() + d.genericModifiers.size()
         + d.footIkControlsModifiers.size() + d.evaluateExpressionModifiers.size()
         + d.interpValueModifiers.size() + d.eventsFromRangeModifiers.size() + d.boneSwitchGenerators.size()
         + d.synchronizedClips.size() + d.offsetAnimGenerators.size() + d.poseMatchingGenerators.size()
         + d.referencePoseGenerators.size() + d.iStateManagerModifiers.size() + d.footIkModifiers.size();
}

// Serialize a schema-assembled root to packfile bytes with the given header.
CompileResult serializeRoot(const std::shared_ptr<havok::IHavokObject>& sroot, const havok::HKXHeader& header) {
    CompileResult r;
    havok::PackFileSerializer ser;
    havok::BinaryWriterEx     bw;
    ser.Serialize(sroot, bw, header);
    r.bytes = bw.Data();
    r.ok    = true;
    return r;
}

} // namespace

CompileResult CompileBehavior(const havok::model::BehaviorData& data, const havok::HKXHeader& header) {
    CompileResult r;
    try {
        // Graph-budget guard: a merged graph at/over the engine's ~32767 node ceiling would corrupt at
        // runtime — refuse loudly (r.ok stays false -> the Resolver serves vanilla and logs the reason)
        // rather than serve a graph that mints negative node ids.
        if (const std::size_t n = behaviorNodeCount(data); n >= kMaxGraphNodes) {
            r.error = "merged graph has " + std::to_string(n) + " nodes, at/over the engine's " +
                      std::to_string(kMaxGraphNodes) + "-node ceiling (hkbBehaviorGraph::nextUniqueID is a "
                      "signed 16-bit counter) — refusing to serve (would mint negative node ids and corrupt "
                      "saves). Trim the mods contributing to this graph.";
            return r;
        }

        havok::schema::SchemaRegistry* reg = havok::schema::SharedRegistry();
        if (!reg) { r.error = "schema registry unavailable (" + havok::schema::SharedRegistryError() + ")"; return r; }

        // Behavior-preserving bindings resolve on a mutable copy — the index-resolved intermediate the
        // schema assembler emits from (identical prep to the retired facade).
        havok::model::BehaviorData resolved = data;
        havok::model::ResolveBehaviorBindings(resolved);

        auto sroot = havok::model::AssembleGraph(resolved, *reg);
        if (!sroot) { r.error = "AssembleGraph returned null (schema compile failed)"; return r; }
        auto out = serializeRoot(sroot, header);
        if (out.ok) g_compiled.fetch_add(1);
        return out;
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    return r;
}

CompileResult CompileCharacter(const havok::model::CharacterData& data, const havok::HKXHeader& header) {
    CompileResult r;
    try {
        havok::schema::SchemaRegistry* reg = havok::schema::SharedRegistry();
        if (!reg) { r.error = "schema registry unavailable (" + havok::schema::SharedRegistryError() + ")"; return r; }

        auto sroot = havok::model::AssembleCharacter(data, *reg);
        if (!sroot) { r.error = "AssembleCharacter returned null (schema compile failed)"; return r; }
        auto out = serializeRoot(sroot, header);
        if (out.ok) g_compiled.fetch_add(1);
        return out;
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    return r;
}

CompileResult BuildProject(const havok::model::ProjectSpec& spec, const havok::HKXHeader& header) {
    CompileResult r;
    try {
        havok::schema::SchemaRegistry* reg = havok::schema::SharedRegistry();
        if (!reg) { r.error = "schema registry unavailable (" + havok::schema::SharedRegistryError() + ")"; return r; }

        auto sroot = havok::model::AssembleProject(spec, *reg);
        if (!sroot) { r.error = "AssembleProject returned null (schema compile failed)"; return r; }
        return serializeRoot(sroot, header);
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    return r;
}

std::size_t CompiledCount() { return g_compiled.load(); }

} // namespace CB::core::compile
