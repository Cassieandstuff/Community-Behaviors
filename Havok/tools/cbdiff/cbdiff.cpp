// cbdiff — semantic diff of CB's compiled output against a PROVEN reference (Pandora, layered over
// vanilla). Pandora is the in-game-proven oracle; any place CB diverges (that isn't a deliberate CB
// feature) is a CB bug.
//
// Modes:
//   cbdiff adsf <ref.txt> <cb.txt>                        animationdatasinglefile.txt — per-clip diff
//   cbdiff asd  <ref.txt> <cb.txt>                        animationsetdatasinglefile.txt — per-set diff
//   cbdiff asdcrc <ref.txt> <cb.txt> <proj> <set>         dump a set's CB-only CRCs
//
// (Behavior-graph diffs: decompile both sides to reflective units and diff the trees — node files are keyed by
// FormID, so pair them by (class, name).)

#include <interface/AnimationData.h>              // CB::core::animdata::ParseSingleFile
#include <codec/format/AnimationSetData.h>        // CB::core::animsetdata::ParseSingleFile
#include <interface/reflection/HavokSchema.h>     // CB::core::schema::SetSharedSchemaDir (arm the decompiler)

#include <RymlInclude.h>                          // the sanctioned rapidyaml include (C++20+ shim)

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ryml = c4::yml;
namespace fs   = std::filesystem;

namespace {

int g_diffs = 0;

std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) { std::fprintf(stderr, "cbdiff: cannot open '%s'\n", p.c_str()); return {}; }
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}
std::vector<std::uint8_t> readBytes(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
std::string lower(std::string s) { for (char& c : s) c = static_cast<char>(std::tolower((unsigned char)c)); return s; }
std::string join(const std::vector<std::string>& v, const char* sep = " | ") {
    std::string o; for (std::size_t i = 0; i < v.size(); ++i) { if (i) o += sep; o += v[i]; } return o;
}
std::string sv(c4::csubstr s) { return std::string(s.str, s.len); }

// Top-level `key: value` scalar (value unquoted) — quick extraction without a full parse.
std::string field(const std::string& text, const std::string& key) {
    std::istringstream in(text); std::string line; const std::string pfx = key + ":";
    while (std::getline(in, line)) {
        if (line.rfind(pfx, 0) != 0) continue;
        std::string v = line.substr(pfx.size());
        while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
        while (!v.empty() && (v.back() == '\r' || v.back() == '\n')) v.pop_back();
        if (v.size() >= 2 && v.front() == '\'' && v.back() == '\'') v = v.substr(1, v.size() - 2);
        return v;
    }
    return {};
}

// ── animationdatasinglefile.txt ──────────────────────────────────────────────────────────────────
int diffAdsf(const std::string& refPath, const std::string& cbPath) {
    const auto refSF = CB::core::animdata::ParseSingleFile(readFile(refPath));
    const auto cbSF  = CB::core::animdata::ParseSingleFile(readFile(cbPath));
    std::printf("== adsf: %zu ref project(s) vs %zu cb project(s) ==\n", refSF.projects.size(), cbSF.projects.size());
    std::unordered_map<std::string, const CB::core::animdata::Project*> cbByName;
    for (const auto& p : cbSF.projects) cbByName[lower(p.name)] = &p;
    for (const auto& rp : refSF.projects) {
        auto it = cbByName.find(lower(rp.name));
        if (it == cbByName.end()) { std::printf("PROJECT only-in-ref: %s\n", rp.name.c_str()); ++g_diffs; continue; }
        const auto& cp = *it->second;
        std::unordered_map<std::string, const CB::core::animdata::ClipGenerator*> cbClips, refClips;
        for (const auto& c : cp.clips) cbClips[c.name] = &c;
        for (const auto& c : rp.clips) refClips[c.name] = &c;
        int pd = 0; auto note = [&](const std::string& s){ if(!pd) std::printf("-- %s --\n", rp.name.c_str()); ++pd; ++g_diffs; std::printf("   %s\n", s.c_str()); };
        for (const auto& rc : rp.clips) {
            auto ci = cbClips.find(rc.name);
            if (ci == cbClips.end()) { note("clip only-in-ref: '" + rc.name + "' (animIdx " + rc.animIndex + ")"); continue; }
            const auto& cc = *ci->second;
            if (rc.animIndex != cc.animIndex) note("clip '" + rc.name + "' animIndex: ref " + rc.animIndex + " cb " + cc.animIndex);
            if (rc.triggers != cc.triggers) note("clip '" + rc.name + "' TRIGGERS: ref [" + join(rc.triggers) + "] cb [" + join(cc.triggers) + "]");
        }
        for (const auto& cc : cp.clips) if (!refClips.count(cc.name)) note("clip only-in-cb:  '" + cc.name + "' (animIdx " + cc.animIndex + ")");
    }
    return g_diffs;
}

// ── animationsetdatasinglefile.txt ───────────────────────────────────────────────────────────────
int diffAsd(const std::string& refPath, const std::string& cbPath) {
    const auto refSF = CB::core::animsetdata::ParseSingleFile(readFile(refPath));
    const auto cbSF  = CB::core::animsetdata::ParseSingleFile(readFile(cbPath));
    std::printf("== asd: %zu ref project(s) vs %zu cb project(s) ==\n", refSF.projects.size(), cbSF.projects.size());
    std::unordered_map<std::string, const CB::core::animsetdata::Project*> cbByHdr;
    for (const auto& p : cbSF.projects) cbByHdr[lower(p.header)] = &p;
    for (const auto& rp : refSF.projects) {
        auto it = cbByHdr.find(lower(rp.header));
        if (it == cbByHdr.end()) { std::printf("PROJECT only-in-ref: %s\n", rp.header.c_str()); ++g_diffs; continue; }
        const auto& cp = *it->second;
        std::unordered_map<std::string, const CB::core::animsetdata::SetFile*> cbSets;
        for (const auto& s : cp.sets) cbSets[lower(s.name)] = &s;
        int pd = 0; auto note = [&](const std::string& s){ if(!pd) std::printf("-- %s --\n", rp.header.c_str()); ++pd; ++g_diffs; std::printf("   %s\n", s.c_str()); };
        for (const auto& rs : rp.sets) {
            auto si = cbSets.find(lower(rs.name));
            if (si == cbSets.end()) { note("set only-in-ref: '" + rs.name + "'"); continue; }
            const auto& cs = *si->second;
            if (rs.equipEvents != cs.equipEvents) note("set '" + rs.name + "' equipEvents: ref [" + join(rs.equipEvents) + "] cb [" + join(cs.equipEvents) + "]");
            if (rs.crcs.size() != cs.crcs.size()) note("set '" + rs.name + "' crc COUNT: ref " + std::to_string(rs.crcs.size()) + " cb " + std::to_string(cs.crcs.size()));
            std::unordered_map<std::string, const CB::core::animsetdata::Attack*> cbAtk;
            for (const auto& a : cs.attacks) cbAtk[a.event] = &a;
            for (const auto& ra : rs.attacks) {
                auto ai = cbAtk.find(ra.event);
                if (ai == cbAtk.end()) { note("set '" + rs.name + "' attack only-in-ref: '" + ra.event + "' -> [" + join(ra.clips) + "]"); continue; }
                if (ra.clips != ai->second->clips) note("set '" + rs.name + "' attack '" + ra.event + "' clips: ref [" + join(ra.clips) + "] cb [" + join(ai->second->clips) + "]");
            }
        }
    }
    return g_diffs;
}

int dumpAsdCrc(const std::string& refPath, const std::string& cbPath, const std::string& proj, const std::string& set) {
    const auto refSF = CB::core::animsetdata::ParseSingleFile(readFile(refPath));
    const auto cbSF  = CB::core::animsetdata::ParseSingleFile(readFile(cbPath));
    auto findSet = [&](const CB::core::animsetdata::SingleFile& sf) -> const CB::core::animsetdata::SetFile* {
        for (const auto& p : sf.projects) if (lower(p.header).find(lower(proj)) != std::string::npos)
            for (const auto& s : p.sets) if (lower(s.name).find(lower(set)) != std::string::npos) return &s;
        return nullptr; };
    const auto* rs = findSet(refSF); const auto* cs = findSet(cbSF);
    if (!rs || !cs) { std::printf("set not found\n"); return 1; }
    auto key = [](const CB::core::animsetdata::CrcTriple& t){ return std::to_string(t.folder)+"/"+std::to_string(t.file)+"/"+std::to_string(t.ext); };
    std::printf("== %s / %s : ref %zu / cb %zu crcs ==\n", proj.c_str(), set.c_str(), rs->crcs.size(), cs->crcs.size());
    std::unordered_map<std::string,int> refHave; for (const auto& t : rs->crcs) refHave[key(t)]++;
    for (std::size_t i = 0; i < cs->crcs.size(); ++i) { const auto k = key(cs->crcs[i]); if (!refHave.count(k)) std::printf("   CB-only [%zu] %s\n", i, k.c_str()); }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage:\n"
                    "  cbdiff adsf   <ref.txt> <cb.txt>\n"
                    "  cbdiff asd    <ref.txt> <cb.txt>\n"
                    "  cbdiff asdcrc <ref.txt> <cb.txt> <proj> <set>\n");
        return 2;
    }
    const std::string mode = argv[1];
    try {
        if      (mode == "adsf"   && argc >= 4) diffAdsf(argv[2], argv[3]);
        else if (mode == "asd"    && argc >= 4) diffAsd(argv[2], argv[3]);
        else if (mode == "asdcrc" && argc >= 6) return dumpAsdCrc(argv[2], argv[3], argv[4], argv[5]);
        else { std::printf("cbdiff: bad mode/args\n"); return 2; }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "cbdiff: %s\n", e.what());
        return 1;
    }
    std::printf("\n== %d divergence(s) ==\n", g_diffs);
    return g_diffs ? 1 : 0;
}
