// gates — the havok-core-FREE correctness gates, pulled out of havok_core_cli.cpp (org-pass firesale
// phase 4b). A build-time DRIVER over Havok/core only (NO typed havok-core): these gates validate the
// schema pipeline against VANILLA references or by schema-self round-trip, never against the typed
// oracle — so they outlive havok-core's deletion. Seeded with emit-check (the headline emit gate); the
// rest of the free cluster (setdata/animdata/motion round-trips, decompose/compose, derive-checks,
// field/tree diff) migrates here the same way as havok-core is cut.
//
//   gates emit-check <file.hkx> <Havok-dir> <template.xml> <vanbase-ref-dir>
//       Schema-decompile a behavior binary and byte-diff every emitted .hky file against a vanilla
//       reference tree. This is schema-emit-vs-VANILLA (not vs typed), the base-master fidelity gate.

#include <codec/serialization/HavokFile.h>                    // havok::sct::ReadHavokFile (byte<->disk)
#include <codec/serialization/packfile/PackFileDeserializer.h> // PackFileDeserializer + BinaryReaderEx
#include <codec/serialization/HavokIo.h>                      // havok::io::MakeSchemaFactory
#include <interface/reflection/HavokSchema.h>                 // havok::schema::SchemaRegistry
#include <decompile/BehaviorDecompile.h>                      // Identity / AssignIdentity / EmitHky / EmitFullBaseScaffolding

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

// emit-check: schema-decompile <file.hkx> to a .hky tree and byte-diff each emitted file against the
// vanilla reference tree <vanbase-ref-dir>, per category. Zero-diff = the schema emitter reproduces
// vanilla byte-for-byte (the base-master fidelity gate). Wholly schema/Havok-core: no typed oracle.
int doEmitCheck(const std::string& in, const std::string& schemaDir, const std::string& xmlFile,
                const std::string& refDir) {
    namespace fs = std::filesystem;
    if (schemaDir.empty() || xmlFile.empty() || refDir.empty()) {
        std::printf("usage: gates emit-check <file.hkx> <Havok-dir> <template.xml> <vanbase-ref-dir>\n"); return 1;
    }
    std::vector<std::uint8_t> bytes; std::string err;
    if (!havok::sct::ReadHavokFile(in, bytes, &err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
    havok::schema::SchemaRegistry reg;
    if (!reg.LoadDir(schemaDir, err)) { std::printf("ERROR loading schema: %s\n", err.c_str()); return 1; }
    std::string xmlText;
    { std::ifstream xf(xmlFile, std::ios::binary); std::stringstream ss; ss << xf.rdbuf(); xmlText = ss.str(); }

    havok::PackFileDeserializer des;
    des.ObjectFactory = havok::io::MakeSchemaFactory(reg);
    try { havok::BinaryReaderEx br(false, true, bytes); des.Deserialize(br); }
    catch (const std::exception& e) { std::printf("READ FAIL: %s\n", e.what()); return 1; }

    const havok::model::Identity ident = havok::model::AssignIdentity(des, reg, xmlText);
    const fs::path outDir = fs::temp_directory_path() / "sct_emitcheck";
    std::error_code ec; fs::remove_all(outDir, ec);
    if (!havok::model::EmitHky(ident, reg, outDir.string(), err)) { std::printf("EMIT FAIL: %s\n", err.c_str()); return 1; }
    // Full-base scaffolding (behavior.yaml + data/graphdata.yaml) — the whole-graph pieces EmitHky omits.
    if (!havok::model::EmitFullBaseScaffolding(ident, outDir.string(), err)) { std::printf("SCAFFOLD FAIL: %s\n", err.c_str()); return 1; }

    // per-category diff vs the reference tree
    auto readFile = [](const fs::path& p) { std::ifstream f(p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); };
    std::map<std::string, std::pair<int,int>> tally;   // category -> {match, total}
    int shown = 0;

    // Whole-graph scaffolding files (single files, not numeric-stemmed node dirs): byte-compare directly.
    for (const char* rel : {"behavior.yaml", "data/graphdata.yaml"}) {
        const fs::path rf = fs::path(refDir) / rel;
        if (!fs::is_regular_file(rf, ec)) continue;
        auto& t = tally["(scaffold)"]; ++t.second;
        const std::string ref = readFile(rf), mine = readFile(outDir / rel);
        if (ref == mine) { ++t.first; }
        else if (shown++ < 6) {
            std::printf("  DIFF %s:\n", rel);
            std::size_t rp = 0, mp = 0; int ln = 1;
            while (rp < ref.size() && mp < mine.size()) {
                std::size_t re = ref.find('\n', rp), me = mine.find('\n', mp);
                std::string rl = ref.substr(rp, re-rp), ml = mine.substr(mp, me-mp);
                if (rl != ml) { std::printf("    L%d ref: %s\n    L%d io:  %s\n", ln, rl.c_str(), ln, ml.c_str()); break; }
                rp = re+1; mp = me+1; ++ln;
            }
        }
    }
    for (const char* cat : {"clips","states","transitions","generators","selectors","references","tagging","modifiers"}) {
        const fs::path rc = fs::path(refDir) / cat;
        if (!fs::is_directory(rc, ec)) continue;
        for (const auto& e : fs::directory_iterator(rc, ec)) {
            if (!e.is_regular_file()) continue;
            const std::string stem = e.path().stem().string();
            if (!std::all_of(stem.begin(), stem.end(), [](unsigned char c){ return std::isdigit(c); })) continue;
            auto& t = tally[cat]; ++t.second;
            const std::string ref = readFile(e.path());
            const std::string mine = readFile(outDir / cat / e.path().filename());
            if (ref == mine) { ++t.first; }
            else if (shown++ < 6) {
                std::printf("  DIFF %s/%s:\n", cat, e.path().filename().string().c_str());
                std::size_t rp = 0, mp = 0; int ln = 1;
                while (rp < ref.size() && mp < mine.size()) {
                    std::size_t re = ref.find('\n', rp), me = mine.find('\n', mp);
                    std::string rl = ref.substr(rp, re-rp), ml = mine.substr(mp, me-mp);
                    if (rl != ml) { std::printf("    L%d ref: %s\n    L%d io:  %s\n", ln, rl.c_str(), ln, ml.c_str()); break; }
                    rp = re+1; mp = me+1; ++ln;
                }
            }
        }
    }
    std::printf("emit-check:\n");
    int gm=0, gt=0;
    for (const auto& [cat, t] : tally) { std::printf("  %-12s %d/%d\n", cat.c_str(), t.first, t.second); gm+=t.first; gt+=t.second; }
    std::printf("  TOTAL        %d/%d files byte-identical\n", gm, gt);
    return gm == gt ? 0 : 1;
}

void usage() {
    std::printf("usage:\n"
                "  gates emit-check <file.hkx> <Havok-dir> <template.xml> <vanbase-ref-dir>\n");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 2; }
    const std::string verb = argv[1];
    std::vector<std::string> pos;
    for (int i = 2; i < argc; ++i) pos.emplace_back(argv[i]);

    if (verb == "emit-check") {
        return doEmitCheck(pos.size() > 0 ? pos[0] : "", pos.size() > 1 ? pos[1] : "",
                           pos.size() > 2 ? pos[2] : "", pos.size() > 3 ? pos[3] : "");
    }
    usage();
    return 2;
}
