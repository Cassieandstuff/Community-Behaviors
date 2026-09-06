// BehaviorCompiler / Validate (SCT shell) implementation.

#include "havok/sct/BehaviorCompiler.h"
#include "havok/sct/HavokFile.h"

#include "havok/classes/Classes.h"
#include "havok/core/PackFileDeserializer.h"
#include "havok/core/PackFileSerializer.h"
#include "havok/model/BehaviorBuilder.h"
#include "havok/model/yaml/YamlBehaviorLoader.h"   // wire the merge classifier to the schema

#include "SchemaCompilerState.h"   // shared toggle + registry (also used by Character/Project compilers)

#include <havok-model/HavokModel.h>
#include <havok-schema/HavokSchema.h>

#include <atomic>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>

namespace havok::sct {

namespace {
    // Behavior-serve counters (schema vs typed). The toggle + schema registry themselves now live in
    // SchemaCompilerState (shared with the Character/Project compilers).
    std::atomic<std::size_t> g_schemaServed{0}, g_typedServed{0};
}

void SetSchemaCompiler(bool enabled, const std::string& schemaDir) {
    SetSchemaCompileState(enabled, schemaDir);   // the shared toggle + schema dir
    // Wire the load-order merge classifier to the SAME schema, so an array's compose/guarded
    // policy is read from its `merge:` tag (schema = single source of truth, shared with the
    // converter merge — they can't drift). SchemaCompileRegistry() force-loads the tree here; a
    // nullptr (disabled, or the load failed) leaves the merger on its built-in compose/guarded name
    // fallback, i.e. identical to pre-tag behaviour.
    havok::model::YamlBehaviorLoader::SetSchemaRegistry(enabled ? SchemaCompileRegistry() : nullptr);
}
bool SchemaCompilerReady() { return SchemaCompileEnabled() && SchemaCompileRegistry() != nullptr; }

// Why the schema path isn't serving (schema-load failure OR editor<->compiler version mismatch),
// for the startup log. The gate itself lives in the SHARED SchemaCompilerState loader so every sct
// compiler (behavior/character/project/animation/skeleton) honours the same contract; this is the
// public accessor consumers (Plugin.cpp) already call, delegated to that one source of truth.
const std::string& SchemaCompilerError() { return SchemaCompileError(); }
void SchemaCompilerStats(std::size_t& s, std::size_t& t) { s = g_schemaServed.load(); t = g_typedServed.load(); }

CompileResult CompileBehavior(const model::BehaviorData& data, const HKXHeader& header) {
    CompileResult r;
    try {
        // Stage 4: run the discrete bindings-resolve pass on a mutable copy — this copy
        // IS the index-resolved intermediate both compilers emit from. Behavior-preserving
        // (see ResolveBehaviorBindings); the typed builder still holds a const ref, unchanged.
        model::BehaviorData resolved = data;
        model::ResolveBehaviorBindings(resolved);

        // Data-driven path (opt-in): assemble via the schema builder. Proven byte-identical to the
        // typed path offline. Any failure falls through to the typed builder — enabling it can only
        // ever match or fall back, never serve a worse graph than before.
        if (SchemaCompileEnabled()) {
            if (schema::SchemaRegistry* reg = SchemaCompileRegistry()) {
                try {
                    if (auto sroot = model::AssembleGraph(resolved, *reg)) {
                        PackFileSerializer ser; BinaryWriterEx bw;
                        ser.Serialize(sroot, bw, header);
                        r.bytes = bw.Data();
                        r.ok    = true;
                        g_schemaServed.fetch_add(1);
                        return r;
                    }
                } catch (const std::exception&) { /* fall through to typed */ }
            }
        }

        model::BehaviorBuilder builder(resolved);
        auto root = builder.Build();
        if (!root) { r.error = "BehaviorBuilder produced a null root"; return r; }
        PackFileSerializer ser;
        BinaryWriterEx bw;
        ser.Serialize(root, bw, header);
        r.bytes = bw.Data();
        r.ok    = true;
        g_typedServed.fetch_add(1);
    } catch (const std::exception& e) {
        r.ok = false;
        r.error = e.what();
        r.bytes.clear();
    }
    return r;
}

CompileResult CompileBehaviorToFile(const model::BehaviorData&   data,
                                    const std::filesystem::path& outPath,
                                    bool                         validate,
                                    const HKXHeader&             header) {
    CompileResult r = CompileBehavior(data, header);
    if (!r.ok) return r;
    if (validate) {
        const ValidationReport vr = ValidatePackfile(r.bytes);
        if (!vr.ok) { r.ok = false; r.error = "validation failed: " + vr.error; return r; }
    }
    std::string err;
    if (!WriteHavokFile(outPath, r.bytes, &err)) { r.ok = false; r.error = err; }
    return r;
}

ValidationReport ValidatePackfile(const std::vector<std::uint8_t>& bytes) {
    ValidationReport vr;
    if (bytes.size() < 64) { vr.error = "too small to be a packfile"; return vr; }
    try {
        BinaryReaderEx       br(bytes);
        PackFileDeserializer des;
        auto root = std::dynamic_pointer_cast<hkRootLevelContainer>(des.Deserialize(br));
        if (!root)                          { vr.error = "root is not hkRootLevelContainer"; return vr; }
        if (root->m_namedVariants.empty())  { vr.error = "no named variants"; return vr; }
        auto bg = std::dynamic_pointer_cast<hkbBehaviorGraph>(root->m_namedVariants[0].m_variant);
        if (!bg)                            { vr.error = "first variant is not hkbBehaviorGraph"; return vr; }
        // A null rootGenerator is valid — some shipped vanilla graphs are stubs (e.g.
        // the creature *_lod behaviors: goatbehavior_lod, frostbitespiderbehavior_lod).
        if (!bg->m_data)                    { vr.error = "behavior graph has no data"; return vr; }
        vr.graphName = bg->m_name;

        // Semantic invariants over the deserialized graph — the byte-level oracle for the
        // "silently blank node that crashes later" class of bug that used to be found only
        // by in-game forensics. Queried by class (no full re-walk); objects are already
        // cached from the Deserialize above. Both are HARD failures so a broken bundle fails
        // the offline gate instead of reaching the engine.
        havok::BinaryReaderEx dr(des._header.Endian == 0, des._header.PointerSize == 8, des.DataSectionBytes());

        // (1) An RBG with empty behaviorName references nothing at runtime -> the actor's
        //     sub-behavior is missing -> T-pose. (BR-7: SkyParkour/DMCO shipped this because
        //     PatchConverter::populate() had no case for the class and emitted it blank.)
        for (const auto& o : des.ConstructAllOfClass(dr, "hkbBehaviorReferenceGenerator")) {
            auto rbg = std::dynamic_pointer_cast<hkbBehaviorReferenceGenerator>(o);
            if (rbg && rbg->m_behaviorName.empty()) {
                vr.error = "hkbBehaviorReferenceGenerator '" + rbg->m_name +
                           "' has an empty behaviorName (references nothing at runtime)";
                return vr;
            }
        }
        // (2) A state-tagging generator with a null wrapped child: char-setup virtual-calls
        //     that child and null-derefs. (BR-10: Paraglider's BSiStateTaggingGenerator was
        //     emitted blank -> m_pDefaultGenerator null -> the char-setup crash.)
        for (const auto& o : des.ConstructAllOfClass(dr, "BSiStateTaggingGenerator")) {
            auto tg = std::dynamic_pointer_cast<BSiStateTaggingGenerator>(o);
            if (tg && !tg->m_pDefaultGenerator) {
                vr.error = "BSiStateTaggingGenerator '" + tg->m_name +
                           "' has a null pDefaultGenerator (char-setup would null-deref)";
                return vr;
            }
        }

        vr.ok        = true;
    } catch (const std::exception& e) {
        vr.ok    = false;
        vr.error = std::string("deserialize threw: ") + e.what();
    }
    return vr;
}

} // namespace havok::sct
