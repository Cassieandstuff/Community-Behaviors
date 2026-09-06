// SchemaCompilerState — INTERNAL to havok-core's sct/ compilers.
//
// The ONE data-driven-compiler toggle + the lazily-loaded Havok/ schema registry, shared by every
// sct compiler (CompileBehavior / CompileCharacter / BuildProject) so they flip on a SINGLE switch
// instead of each carrying its own copy. The public entry point is SetSchemaCompiler
// (BehaviorCompiler.cpp), which sets this state and then wires any load-order/merge consumers.
//
// Not a cross-project API: this header lives under src/ (not include/) and is reached by its sct/
// siblings via a relative quote-include. Nothing outside havok-core's sct/ compilers should use it.
#pragma once

#include <string>

namespace havok { namespace schema { class SchemaRegistry; } }

namespace havok::sct {

// Is the data-driven (schema) compile path requested? (The `bUseSchema` toggle.)
bool SchemaCompileEnabled();

// The Havok/ class-schema registry, lazily loaded ONCE from the configured dir (or
// $SCT_HAVOK_SCHEMA_DIR). Returns nullptr when disabled or when the load failed — callers then use
// their typed fallback. Loading here also force-loads the tree, so a caller can trigger the load by
// calling this after SetSchemaCompileState.
schema::SchemaRegistry* SchemaCompileRegistry();

// Set the toggle + schema dir. Called by SetSchemaCompiler (the public entry). The registry itself
// loads lazily on the first SchemaCompileRegistry() call after this.
void SetSchemaCompileState(bool enabled, const std::string& schemaDir);

// Why the schema path isn't serving: a schema-load failure, or the editor<->compiler schema-version
// contract mismatch (the Havok/SCHEMA.yaml stamp is unreadable or incompatible with this build).
// Empty while the schema path is healthy. Populated as a side effect of the first
// SchemaCompileRegistry() load; surfaced to the startup log via SchemaCompilerError().
const std::string& SchemaCompileError();

}  // namespace havok::sct
