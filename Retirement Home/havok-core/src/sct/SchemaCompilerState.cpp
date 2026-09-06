// SchemaCompilerState — shared data-driven-compiler toggle + schema registry (see the header).
// Extracted from BehaviorCompiler.cpp so CompileCharacter / BuildProject share the one switch.

#include "SchemaCompilerState.h"

#include <havok-schema/HavokSchema.h>

#include <cstdlib>
#include <string>

namespace havok::sct {

namespace {
    bool        g_schemaEnabled = false;
    std::string g_schemaDir;
    std::string g_schemaError;   // why the schema path isn't serving (load fail / version mismatch)

    // The schema version THIS build of the compiler was written against — the editor<->compiler
    // contract stamp (see Havok/SCHEMA.yaml + the schema-version-stamp scheme). The compiler refuses
    // a Havok/ tree it can't safely read (outdated OR ahead of this build) instead of compiling
    // against a mismatched contract. A refused tree is handled exactly like any schema-load failure:
    // the data-driven path stays off and every sct compiler uses its typed builder — same "never
    // serve a worse graph" discipline as the existing fallback — logged loudly at startup.
    constexpr const char* kExpectedSchemaVersion = "1.0.0-rc.1";
}

schema::SchemaRegistry* SchemaCompileRegistry() {
    static schema::SchemaRegistry reg;
    static int state = 0;   // 0=unloaded, 1=ok, 2=fail  (load attempted at most once)
    if (state == 0) {
        std::string dir = g_schemaDir;
        if (dir.empty()) { if (const char* e = std::getenv("SCT_HAVOK_SCHEMA_DIR")) dir = e; }
        std::string err;
        if (dir.empty() || !reg.LoadDir(dir, err)) {
            g_schemaError = dir.empty() ? "no schema directory configured"
                                        : ("schema tree failed to load: " + err);
            state = 2;
        } else if (reg.SchemaVersionString().empty()) {
            if (!reg.SchemaStampPresent()) {
                // ABSENT stamp = lenient. CB OWNS the schema version, so an unstamped tree is CB's
                // own (pre-stamp, or a partial deploy), never a foreign one — allow it silently.
                state = 1;
            } else {
                // A SCHEMA.yaml IS present but yielded no readable version (malformed YAML, or the
                // schema_version key missing). That is an integrity concern — a corrupt or foreign
                // stamp — NOT an unstamped tree, so refuse rather than silently trust it.
                g_schemaError = "SCHEMA STAMP UNREADABLE — Havok/SCHEMA.yaml is present but its "
                                "schema_version could not be read (malformed or missing); a foreign "
                                "or corrupt schema tree may have overwritten Community Behaviors' "
                                "own. Served via typed fallback. This build speaks " +
                                std::string(kExpectedSchemaVersion);
                state = 2;
            }
        } else {
            // A PRESENT stamp that doesn't match is an integrity event, not degradation: since CB
            // owns the version, a differing stamp means a foreign tool's schema tree won the MO2
            // overwrite. Refuse the schema path (→ typed fallback, byte-identical) and shout.
            // RC = exact match. (When the typed builder retires this refusal becomes a hard fail
            // for free — there is no other path to fall back to.)
            const auto current  = schema::SchemaVersion::Parse(kExpectedSchemaVersion);
            const auto authored = schema::SchemaVersion::Parse(reg.SchemaVersionString());
            std::string why;
            if (schema::CheckSchemaCompat(authored, current, why) != schema::SchemaCompat::Ok) {
                g_schemaError = "SCHEMA VERSION MISMATCH — " + why +
                                "; a foreign schema tree may have overwritten Community Behaviors' "
                                "own. Served via typed fallback. This build speaks " +
                                std::string(kExpectedSchemaVersion);
                state = 2;
            } else {
                state = 1;
            }
        }
    }
    return state == 1 ? &reg : nullptr;
}

bool SchemaCompileEnabled() { return g_schemaEnabled; }

const std::string& SchemaCompileError() { return g_schemaError; }

void SetSchemaCompileState(bool enabled, const std::string& schemaDir) {
    g_schemaEnabled = enabled;
    g_schemaDir     = schemaDir;
}

}  // namespace havok::sct
