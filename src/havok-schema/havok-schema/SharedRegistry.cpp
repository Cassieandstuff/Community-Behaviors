// SharedRegistry — the ONE process-wide, lazily-loaded Havok/ schema registry shared by every
// schema-driven compiler. Relocated out of havok-core's SchemaCompilerState so the version-stamp
// gate (all schema:: types) lives beside the types it checks, and so havok-anim can reach the same
// registry without a havok-core dependency. havok-core's SchemaCompilerState now delegates here.

#include "havok-schema/HavokSchema.h"

#include <cstdlib>
#include <string>

namespace havok::schema {

namespace {
    std::string g_sharedDir;
    std::string g_sharedError;   // why the shared registry isn't serving (load fail / version mismatch)

    // The schema version THIS build was written against — the editor<->compiler contract stamp
    // (see Havok/SCHEMA.yaml + the schema-version-stamp scheme). The compiler refuses a Havok/ tree
    // it can't safely read (outdated OR ahead of this build) instead of compiling against a
    // mismatched contract — a refused tree is handled exactly like any schema-load failure.
    constexpr const char* kExpectedSchemaVersion = "1.0.0-rc.1";
}

void SetSharedSchemaDir(const std::string& dir) { g_sharedDir = dir; }

const std::string& SharedRegistryError() { return g_sharedError; }

SchemaRegistry* SharedRegistry() {
    static SchemaRegistry reg;
    static int state = 0;   // 0=unloaded, 1=ok, 2=fail  (load attempted at most once)
    if (state == 0) {
        std::string dir = g_sharedDir;
        if (dir.empty()) { if (const char* e = std::getenv("SCT_HAVOK_SCHEMA_DIR")) dir = e; }
        std::string err;
        if (dir.empty() || !reg.LoadDir(dir, err)) {
            g_sharedError = dir.empty() ? "no schema directory configured"
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
                g_sharedError = "SCHEMA STAMP UNREADABLE — Havok/SCHEMA.yaml is present but its "
                                "schema_version could not be read (malformed or missing); a foreign "
                                "or corrupt schema tree may have overwritten Community Behaviors' "
                                "own. This build speaks " + std::string(kExpectedSchemaVersion);
                state = 2;
            }
        } else {
            // A PRESENT stamp that doesn't match is an integrity event, not degradation: since CB
            // owns the version, a differing stamp means a foreign tool's schema tree won the MO2
            // overwrite. Refuse the schema path and shout. RC = exact match.
            const auto current  = SchemaVersion::Parse(kExpectedSchemaVersion);
            const auto authored = SchemaVersion::Parse(reg.SchemaVersionString());
            std::string why;
            if (CheckSchemaCompat(authored, current, why) != SchemaCompat::Ok) {
                g_sharedError = "SCHEMA VERSION MISMATCH — " + why +
                                "; a foreign schema tree may have overwritten Community Behaviors' "
                                "own. This build speaks " + std::string(kExpectedSchemaVersion);
                state = 2;
            } else {
                state = 1;
            }
        }
    }
    return state == 1 ? &reg : nullptr;
}

}  // namespace havok::schema
