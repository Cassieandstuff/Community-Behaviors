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
