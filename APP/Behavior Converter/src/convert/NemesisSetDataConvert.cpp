#include "NemesisSetDataConvert.h"

#include <havok/anim/AnimationSetData.h>   // the setdata model now lives in havok-core

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <unordered_set>

namespace fs = std::filesystem;
using namespace havok::animsetdata;   // model types (SingleFile/SetFile/… moved to havok-core)

namespace CommunityBehaviors::asd {
namespace {

using LogFn = std::function<void(const std::string&)>;

void Say(const LogFn& log, const std::string& s) { if (log) log(s); }

std::string ToLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
std::string ReadAll(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
bool WriteAll(const fs::path& p, const std::string& s)
{
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(s.data(), static_cast<std::streamsize>(s.size()));
    return static_cast<bool>(f);
}

// Parse one mod's Nemesis set-data patch tree into a delta SingleFile (only its additions).
// modDir = a Nemesis mod folder containing animationsetdatasinglefile\<Proj>Data~<Proj>\.
SingleFile LoadModDelta(const fs::path& modDir, std::size_t& sets, std::size_t& fails, const LogFn& log)
{
    SingleFile out;
    const fs::path root = modDir / "animationsetdatasinglefile";
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return out;

    for (fs::directory_iterator pi(root, ec), pend; !ec && pi != pend; pi.increment(ec)) {
        std::error_code de;
        if (!pi->is_directory(de)) continue;
        const std::string dirName = pi->path().filename().string();  // "DefaultMaleData~DefaultMale"
        const std::size_t tilde = dirName.find('~');
        const std::string projData = (tilde == std::string::npos) ? dirName : dirName.substr(0, tilde);
        std::string stem = projData;
        {
            const std::string low = ToLower(projData);
            if (low.size() > 4 && low.compare(low.size() - 4, 4, "data") == 0)
                stem = projData.substr(0, projData.size() - 4);
        }
        Project proj;
        proj.header = projData + "\\" + stem + ".txt";

        for (fs::directory_iterator fi(pi->path(), ec), fend; !ec && fi != fend; fi.increment(ec)) {
            std::error_code fe;
            if (!fi->is_regular_file(fe)) continue;
            if (ToLower(fi->path().extension().string()) != ".txt") continue;
            const std::string text = ReadAll(fi->path());
            if (text.rfind("V3", 0) != 0) continue;
            try {
                SetFile set = ParseNemesisSetFile(text);
                if (set.equipEvents.empty() && set.conditions.empty() &&
                    set.attacks.empty() && set.crcs.empty())
                    continue;  // patch touched nothing in this set
                set.name = fi->path().filename().string();
                proj.sets.push_back(std::move(set));
                ++sets;
            } catch (const std::exception& e) {
                Say(log, "    bad '" + dirName + "/" + fi->path().filename().string() + "': " + e.what());
                ++fails;
            }
        }
        if (!proj.sets.empty()) out.projects.push_back(std::move(proj));
    }
    return out;
}

// Extract the <hkcstring> entries under the animationNames param of a Nemesis
// hkbCharacterStringData patch (the character's animation roster). Nemesis marker lines
// are ignored — we take every name and let the runtime dedup against the vanilla roster.
std::vector<std::string> ExtractAnimNames(const std::string& text)
{
    std::vector<std::string> names;
    const std::size_t p = text.find("name=\"animationNames\"");
    if (p == std::string::npos) return names;
    std::size_t end = text.find("</hkparam>", p);
    if (end == std::string::npos) end = text.size();
    const std::string open = "<hkcstring>", close = "</hkcstring>";
    std::size_t i = p;
    while (true) {
        const std::size_t s = text.find(open, i);
        if (s == std::string::npos || s >= end) break;
        const std::size_t s2 = s + open.size();
        const std::size_t e = text.find(close, s2);
        if (e == std::string::npos || e > end) break;
        names.push_back(text.substr(s2, e - s2));
        i = e + close.size();
    }
    return names;
}

// Scan a mod for character-roster patches: any dir (other than the set-data dir) whose
// #*.txt hold an hkbCharacterStringData animationNames block. The dir name is the character
// (e.g. "defaultmale"). Accumulates names into out[charName] (deduped/emitted by the caller).
void LoadModAnimNames(const fs::path& modDir, std::map<std::string, std::vector<std::string>>& out)
{
    std::error_code ec;
    // Scan one graph dir's #*.txt for an hkbCharacterStringData animationNames block; key by charName.
    auto scanCharDir = [&](const fs::path& dir, const std::string& charName) {
        std::error_code se;
        for (fs::directory_iterator fi(dir, se), fend; !se && fi != fend; fi.increment(se)) {
            std::error_code fe;
            if (!fi->is_regular_file(fe)) continue;
            if (ToLower(fi->path().extension().string()) != ".txt") continue;
            const std::string text = ReadAll(fi->path());
            if (text.find("hkbCharacterStringData") == std::string::npos) continue;
            const std::vector<std::string> names = ExtractAnimNames(text);
            if (names.empty()) continue;
            auto& v = out[ToLower(charName)];
            v.insert(v.end(), names.begin(), names.end());
        }
    };
    for (fs::directory_iterator di(modDir, ec), dend; !ec && di != dend; di.increment(ec)) {
        std::error_code de;
        if (!di->is_directory(de)) continue;
        const std::string dirName = di->path().filename().string();
        if (ToLower(dirName) == "animationsetdatasinglefile") continue;
        // The FirstPerson character's roster patch lives one level deeper, at _1stperson/<char>/#*.txt
        // (SkyParkour's _1stperson/firstperson/). Descend and key by the SUBDIR name (the character),
        // else the first-person roster stays empty and its moveset CRCs get dropped as unrostered.
        if (ToLower(dirName) == "_1stperson") {
            for (fs::directory_iterator sdi(di->path(), ec), send; !ec && sdi != send; sdi.increment(ec)) {
                std::error_code sde;
                if (sdi->is_directory(sde)) scanCharDir(sdi->path(), sdi->path().filename().string());
            }
            continue;
        }
        scanCharDir(di->path(), dirName);
    }
}

}  // namespace

NemesisConvertStats ConvertNemesisSetData(const std::vector<std::filesystem::path>&     modDirs,
                                          const std::filesystem::path&                  bundle,
                                          const std::function<void(const std::string&)>& log)
{
    NemesisConvertStats stats;

    // Merge each mod's delta into a combined accumulator, in load order. MergeInto unions
    // additions (crcs dedup, attacks delta-wins), so later mods override on conflict and
    // parallel additions to the same set accumulate — the same semantics as the runtime.
    SingleFile combined;
    for (const fs::path& mod : modDirs) {
        std::size_t modSets = 0;
        SingleFile  delta = LoadModDelta(mod, modSets, stats.fails, log);
        const MergeStats st = MergeInto(combined, delta);
        Say(log, "  " + mod.filename().string() + " -> " + std::to_string(modSets) +
                 " set(s) (+" + std::to_string(st.projectsAdded) + " proj, +" +
                 std::to_string(st.setsAdded) + " sets, +" + std::to_string(st.attacksAdded) +
                 " attacks, +" + std::to_string(st.crcsAdded) + " crcs merged)");
    }

    // Emit the combined set-data delta as split form into the bundle.
    for (const Project& proj : combined.projects) {
        const std::size_t bs = proj.header.find('\\');
        const std::string projDataDir = ToLower(bs == std::string::npos ? proj.header : proj.header.substr(0, bs));
        for (const SetFile& set : proj.sets) {
            const fs::path out = bundle / "meshes" / "animationsetdata" / projDataDir / ToLower(set.name);
            if (!WriteAll(out, EmitSetFile(set))) {
                Say(log, "  FAILED to write '" + out.string() + "'");
                ++stats.fails;
                continue;
            }
            ++stats.sets;
            stats.crcs += set.crcs.size();
            stats.attacks += set.attacks.size();
        }
    }
    stats.projects = combined.projects.size();

    // Extract each mod's character-roster additions (hkbCharacterStringData animationNames),
    // union across mods per character, and write them into the SAME bundle so the roster
    // ships with the behaviors + set-data. The runtime dedup-appends these to the character's
    // animationNames, so the moveset table's crcs resolve against a roster that actually
    // contains them (the missing third leg that otherwise desyncs char-setup).
    std::map<std::string, std::vector<std::string>> charAnims;
    for (const fs::path& mod : modDirs) LoadModAnimNames(mod, charAnims);

    for (const auto& [charName, names] : charAnims) {
        std::vector<std::string>        uniq;
        std::unordered_set<std::string> seen;
        for (const std::string& n : names)
            if (seen.insert(ToLower(n)).second) uniq.push_back(n);
        std::string body;
        for (const std::string& n : uniq) { body += n; body += "\r\n"; }
        // BR-specific delta (not a game-file mirror): one file per character, keyed by the
        // character name (matches hkbCharacterStringData::name at runtime).
        const fs::path out = bundle / "animationnames" / (charName + ".txt");
        if (WriteAll(out, body)) { ++stats.charFiles; stats.animNames += uniq.size(); }
        else { Say(log, "  FAILED to write '" + out.string() + "'"); ++stats.fails; }
    }

    return stats;
}

}  // namespace CommunityBehaviors::asd
