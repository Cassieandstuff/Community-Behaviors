/// @file
/// @brief The ONE process-wide, lazily-loaded schema registry (see HavokSchema.h); the version-stamp gate lives
///        here, beside the schema:: types it checks.

#include <interface/reflection/HavokSchema.h>
#include <interface/reflection/SchemaVersion.gen.h>

#include <cstdlib>
#include <string>

namespace CB::engine::schema {

namespace {
    std::string g_sharedDir;
    std::string g_sharedError;   // why the shared registry isn't serving (load fail / version mismatch)
}

void SetSharedSchemaDir(const std::string& dir) { g_sharedDir = dir; }

std::string SharedSchemaDir() {
    if (!g_sharedDir.empty()) return g_sharedDir;
    const char* e = std::getenv("SCT_HAVOK_SCHEMA_DIR");
    return e ? e : "";
}

const std::string& SharedRegistryError() { return g_sharedError; }

SchemaRegistry* SharedRegistry() {
    static SchemaRegistry reg;
    static int state = 0;   // 0=unloaded, 1=ok, 2=fail  (load attempted at most once)
    if (state == 0) {
        const std::string dir = SharedSchemaDir();
        std::string err;
        if (dir.empty() || !reg.LoadDir(dir, err)) {
            g_sharedError = dir.empty() ? "no schema directory configured"
                                        : ("schema tree failed to load: " + err);
            state = 2;
        } else if (reg.SchemaVersionString().empty()) {
            if (!reg.SchemaStampPresent()) {
                // ABSENT stamp = lenient. CB OWNS the schema version, so an unstamped tree is CB's
                // own (the source schema/ loaded directly — cmake/Schema.cmake stamps only the shipped
                // copies), never a foreign one — allow it silently.
                state = 1;
            } else {
                // A SCHEMA.yaml IS present but yielded no readable version (malformed YAML, or the
                // schema_version key missing). That is an integrity concern — a corrupt or foreign
                // stamp — NOT an unstamped tree, so refuse rather than silently trust it.
                g_sharedError = "SCHEMA STAMP UNREADABLE — the schema tree's SCHEMA.yaml is present but its "
                                "schema_version could not be read (malformed or missing); a foreign "
                                "or corrupt schema tree may have overwritten Community Behaviors' "
                                "own. This build speaks " + std::string(kSchemaVersion);
                state = 2;
            }
        } else {
            // A PRESENT stamp that doesn't match is an integrity event, not degradation: since CB
            // owns the version, a differing stamp means a foreign tool's schema tree won the MO2
            // overwrite. Refuse the schema path and shout. RC = exact match.
            const auto current  = SchemaVersion::Parse(kSchemaVersion);
            const auto authored = SchemaVersion::Parse(reg.SchemaVersionString());
            std::string why;
            if (CheckSchemaCompat(authored, current, why) != SchemaCompat::Ok) {
                g_sharedError = "SCHEMA VERSION MISMATCH — " + why +
                                "; a foreign schema tree may have overwritten Community Behaviors' "
                                "own. This build speaks " + std::string(kSchemaVersion);
                state = 2;
            } else {
                state = 1;
            }
        }
    }
    return state == 1 ? &reg : nullptr;
}

}  // namespace CB::engine::schema
