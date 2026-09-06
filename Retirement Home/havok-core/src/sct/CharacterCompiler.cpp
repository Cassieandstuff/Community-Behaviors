#include "havok/sct/CharacterCompiler.h"

#include "havok/core/PackFileDeserializer.h"
#include "havok/core/PackFileSerializer.h"
#include "havok/model/CharacterBuilder.h"
#include "havok/sct/HavokFile.h"

#include "SchemaCompilerState.h"      // shared data-driven-compiler toggle + schema registry

#include <havok-model/HavokModel.h>   // model::AssembleCharacter (the schema-driven emit)
#include <havok-schema/HavokSchema.h> // schema::SchemaRegistry

#include <exception>
#include <memory>

namespace havok::sct {

CompileResult CompileCharacter(const model::CharacterData& data, const HKXHeader& header) {
    CompileResult r;
    try {
        // Data-driven path (opt-in): assemble via model::AssembleCharacter (Havok/ descriptors),
        // proven byte-identical to the typed CharacterBuilder below. Any failure falls through to
        // typed — enabling the schema compiler can only match or fall back, never serve worse.
        if (SchemaCompileEnabled()) {
            if (schema::SchemaRegistry* reg = SchemaCompileRegistry()) {
                try {
                    if (auto sroot = model::AssembleCharacter(data, *reg)) {
                        PackFileSerializer ser;
                        BinaryWriterEx bw;
                        ser.Serialize(sroot, bw, header);
                        r.bytes = bw.Data();
                        r.ok    = true;
                        return r;
                    }
                } catch (const std::exception&) { /* fall through to the typed builder */ }
            }
        }

        model::CharacterBuilder builder(data);
        auto root = builder.Build();
        if (!root) { r.error = "CharacterBuilder produced a null root"; return r; }
        PackFileSerializer ser;
        BinaryWriterEx bw;
        ser.Serialize(root, bw, header);
        r.bytes = bw.Data();
        r.ok    = true;
    } catch (const std::exception& e) {
        r.ok = false;
        r.error = e.what();
        r.bytes.clear();
    }
    return r;
}

CompileResult CompileCharacterToFile(const model::CharacterData& data,
                                     const std::filesystem::path& outPath,
                                     bool validate, const HKXHeader& header) {
    CompileResult r = CompileCharacter(data, header);
    if (!r.ok) return r;

    if (validate) {
        // Round-trip: the bytes must deserialize back to a well-formed
        // hkRootLevelContainer carrying an hkbCharacterData variant.
        try {
            PackFileDeserializer des;
            BinaryReaderEx br(/*bigEndian*/ false, /*uSizeLong*/ true, r.bytes);
            auto root = std::dynamic_pointer_cast<hkRootLevelContainer>(des.Deserialize(br));
            if (!root || root->m_namedVariants.empty() ||
                !std::dynamic_pointer_cast<hkbCharacterData>(root->m_namedVariants[0].m_variant)) {
                r.ok = false;
                r.error = "character packfile did not validate (no hkbCharacterData root variant)";
                return r;
            }
        } catch (const std::exception& e) {
            r.ok = false;
            r.error = std::string("character packfile did not validate: ") + e.what();
            return r;
        }
    }

    std::string err;
    if (!WriteHavokFile(outPath, r.bytes, &err)) { r.ok = false; r.error = err; }
    return r;
}

} // namespace havok::sct
