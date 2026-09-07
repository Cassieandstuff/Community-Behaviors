// SchemaCompilerState — shared data-driven-compiler toggle for havok-core's sct/ compilers.
//
// The lazily-loaded registry + the editor<->compiler version-stamp gate MOVED to havok-schema
// (havok::schema::SharedRegistry), so animation (havok-anim) reaches the same registry without a
// havok-core edge. This file now just (a) keeps the bUseSchema toggle the typed-fallback compilers
// (CompileBehavior/Character/BuildProject) gate on, and (b) forwards the dir + registry + error to
// the shared owner. SetSchemaCompiler (BehaviorCompiler.cpp) remains the public entry.

#include "SchemaCompilerState.h"

#include <havok-schema/HavokSchema.h>

#include <string>

namespace havok::sct {

namespace {
    bool g_schemaEnabled = false;
}

schema::SchemaRegistry* SchemaCompileRegistry() { return schema::SharedRegistry(); }

bool SchemaCompileEnabled() { return g_schemaEnabled; }

const std::string& SchemaCompileError() { return schema::SharedRegistryError(); }

void SetSchemaCompileState(bool enabled, const std::string& schemaDir) {
    g_schemaEnabled = enabled;
    schema::SetSharedSchemaDir(schemaDir);   // load-bearing even when disabled: the registry stays
                                             // loadable so the schema-native anim path can compile.
}

}  // namespace havok::sct
