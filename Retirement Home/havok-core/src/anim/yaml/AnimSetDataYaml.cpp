#include "havok/anim/AnimSetDataYaml.h"

#include <RymlInclude.h>

#include <cctype>
#include <cstdlib>
#include <string>

namespace havok::animsetdata {

namespace {

std::string Low(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool EndsTxt(const std::string& s) { return s.size() >= 4 && Low(s.substr(s.size() - 4)) == ".txt"; }

// A YAML flow scalar. Vanilla event/clip/equip/variable names are bare identifiers, but quote
// defensively if a name carries a YAML-significant char so the round-trip stays safe.
std::string Scalar(const std::string& s) {
    bool needQuote = s.empty();
    for (char c : s)
        if (c == ':' || c == '{' || c == '}' || c == '[' || c == ']' || c == ',' || c == '\'' ||
            c == '"' || c == '#' || c == ' ' || c == '\t')
            { needQuote = true; break; }
    if (!needQuote) return s;
    std::string o = "'";
    for (char c : s) { if (c == '\'') o += "''"; else o += c; }
    o += "'";
    return o;
}

}  // namespace

std::string EmitMovesetsYaml(const Project& project)
{
    std::string y = "sets:\n";
    for (const auto& set : project.sets) {
        std::string name = set.name;
        if (EndsTxt(name)) name.erase(name.size() - 4);   // the slot name without ".txt"
        y += "  - name: " + Scalar(name) + "\n";
        if (!set.conditions.empty()) {
            y += "    gate:\n";
            for (const auto& c : set.conditions)
                y += "      " + Scalar(c.variable) + ": { value: " + std::to_string(c.value) +
                     ", class: " + std::to_string(c.extra) + " }\n";
        }
        if (!set.equipEvents.empty()) {
            y += "    equip: [";
            for (std::size_t i = 0; i < set.equipEvents.size(); ++i) {
                if (i) y += ", ";
                y += Scalar(set.equipEvents[i]);
            }
            y += "]\n";
        }
        if (!set.attacks.empty()) {
            y += "    attacks:\n";
            for (const auto& a : set.attacks) {
                y += "      - { event: " + Scalar(a.event);
                if (a.flag) y += ", flag: " + std::to_string(a.flag);
                y += ", clips: [";
                for (std::size_t i = 0; i < a.clips.size(); ++i) { if (i) y += ", "; y += Scalar(a.clips[i]); }
                y += "] }\n";
            }
        }
        // crcs: DERIVED from the clips' animation paths — never emitted.
    }
    return y;
}

SingleFile ParseMovesetsYaml(const std::string& text, const std::string& projectName, std::string& err)
{
    SingleFile sf;
    std::string storage = text;
    try {
        c4::yml::Tree tree = c4::yml::parse_in_place(c4::to_substr(storage));
        auto root = tree.rootref();
        if (!root.readable() || !root.is_map() || !root.has_child(c4::to_csubstr("sets")))
            return sf;

        Project proj;
        proj.header = projectName + "Data\\" + projectName + ".txt";  // "DefaultMaleData\DefaultMale.txt"

        for (auto sn : root[c4::to_csubstr("sets")]) {
            SetFile set;
            if (sn.has_child(c4::to_csubstr("name")) && sn[c4::to_csubstr("name")].has_val())
                c4::from_chars(sn[c4::to_csubstr("name")].val(), &set.name);
            if (set.name.empty()) continue;
            if (!EndsTxt(set.name)) set.name += ".txt";

            if (sn.has_child(c4::to_csubstr("gate")))
                for (auto g : sn[c4::to_csubstr("gate")]) {
                    TypeCondition c;
                    c4::from_chars(g.key(), &c.variable);
                    if (g.is_map()) {
                        if (g.has_child(c4::to_csubstr("value"))) { int v = 0; c4::from_chars(g[c4::to_csubstr("value")].val(), &v); c.value = v; }
                        c.extra = 4;
                        if (g.has_child(c4::to_csubstr("class"))) { int x = 0; c4::from_chars(g[c4::to_csubstr("class")].val(), &x); c.extra = x; }
                    } else if (g.has_val()) {
                        int v = 0; c4::from_chars(g.val(), &v); c.value = v; c.extra = 4;
                    }
                    set.conditions.push_back(std::move(c));
                }

            if (sn.has_child(c4::to_csubstr("equip")))
                for (auto e : sn[c4::to_csubstr("equip")]) {
                    if (!e.has_val()) continue;
                    std::string s; c4::from_chars(e.val(), &s); set.equipEvents.push_back(std::move(s));
                }

            if (sn.has_child(c4::to_csubstr("attacks")))
                for (auto a : sn[c4::to_csubstr("attacks")]) {
                    Attack atk;
                    if (a.has_child(c4::to_csubstr("event")) && a[c4::to_csubstr("event")].has_val())
                        c4::from_chars(a[c4::to_csubstr("event")].val(), &atk.event);
                    if (a.has_child(c4::to_csubstr("flag"))) { int f = 0; c4::from_chars(a[c4::to_csubstr("flag")].val(), &f); atk.flag = f; }
                    if (a.has_child(c4::to_csubstr("clips")))
                        for (auto cl : a[c4::to_csubstr("clips")]) {
                            if (!cl.has_val()) continue;
                            std::string s; c4::from_chars(cl.val(), &s); atk.clips.push_back(std::move(s));
                        }
                    if (!atk.event.empty()) set.attacks.push_back(std::move(atk));
                }

            proj.sets.push_back(std::move(set));
        }
        if (!proj.sets.empty()) sf.projects.push_back(std::move(proj));
    } catch (const std::exception& e) {
        err = e.what();
    }
    return sf;
}

std::string StemForHeader(const std::string& header)
{
    const auto bs = header.find_last_of("\\/");
    std::string stem = (bs == std::string::npos) ? header : header.substr(bs + 1);
    if (EndsTxt(stem)) stem.erase(stem.size() - 4);
    return Low(std::move(stem));
}

std::string EmitSetdataIndexYaml(const SingleFile& sf)
{
    // The .txt's line-1 count + ordered header list. The count is implicit (the sequence length);
    // headers are carried VERBATIM (exact casing + backslashes), so the compose reproduces them.
    std::string y = "# animationsetdatasinglefile.txt project order (the file's line-1 manifest).\n";
    y += "# Order is load-bearing: the engine cross-references projects positionally.\n";
    y += "projects:\n";
    for (const auto& p : sf.projects) y += "  - " + Scalar(p.header) + "\n";
    return y;
}

std::vector<std::string> ParseSetdataIndexYaml(const std::string& text, std::string& err)
{
    std::vector<std::string> out;
    std::string storage = text;
    try {
        c4::yml::Tree tree = c4::yml::parse_in_place(c4::to_substr(storage));
        auto root = tree.rootref();
        if (!root.readable() || !root.is_map() || !root.has_child(c4::to_csubstr("projects")))
            return out;
        for (auto pn : root[c4::to_csubstr("projects")]) {
            if (!pn.has_val()) continue;
            std::string s; c4::from_chars(pn.val(), &s); out.push_back(std::move(s));
        }
    } catch (const std::exception& e) {
        err = e.what();
    }
    return out;
}

std::string EmitSetdataCrcsYaml(const Project& project)
{
    // Baked residue: setName (no ".txt") -> its CRC triples "folder file ext", in order. Every set
    // is listed (even those with no CRCs, as []) so the file mirrors the moveset set list exactly.
    std::string y = "# BAKED residue — animationsetdata path-CRC registrations. Opaque game data,\n";
    y += "# regenerated by the converter; do NOT hand-edit. Each set lists its (folderCrc fileCrc\n";
    y += "# extCrc) triples in order. New mod content needs none of this — CRCs derive from paths.\n";
    for (const auto& set : project.sets) {
        std::string name = set.name;
        if (EndsTxt(name)) name.erase(name.size() - 4);
        if (set.crcs.empty()) { y += Scalar(name) + ": []\n"; continue; }
        y += Scalar(name) + ":\n";
        for (const auto& c : set.crcs)
            y += "  - " + std::to_string(c.folder) + " " + std::to_string(c.file) + " " +
                 std::to_string(c.ext) + "\n";
    }
    return y;
}

std::map<std::string, std::vector<CrcTriple>> ParseSetdataCrcsYaml(const std::string& text, std::string& err)
{
    std::map<std::string, std::vector<CrcTriple>> out;
    std::string storage = text;
    try {
        c4::yml::Tree tree = c4::yml::parse_in_place(c4::to_substr(storage));
        auto root = tree.rootref();
        if (!root.readable() || !root.is_map()) return out;
        for (auto sn : root) {
            if (!sn.has_key()) continue;
            std::string name; c4::from_chars(sn.key(), &name);
            name = Low(std::move(name));                 // keys compared case-insensitively (set names)
            auto& vec = out[name];
            if (!sn.is_seq()) continue;
            for (auto cn : sn) {
                if (!cn.has_val()) continue;
                std::string s; c4::from_chars(cn.val(), &s);
                // "folder file ext" — three decimal uint32 (folder CRCs can exceed INT_MAX).
                const char* p = s.c_str();
                char*       e = nullptr;
                CrcTriple   t;
                t.folder = static_cast<std::uint32_t>(std::strtoul(p, &e, 10)); p = e;
                t.file   = static_cast<std::uint32_t>(std::strtoul(p, &e, 10)); p = e;
                t.ext    = static_cast<std::uint32_t>(std::strtoul(p, &e, 10));
                vec.push_back(t);
            }
        }
    } catch (const std::exception& e) {
        err = e.what();
    }
    return out;
}

SingleFile AssembleSetdata(
    const std::vector<std::string>& headers,
    const std::map<std::string, SingleFile>& movesetsByStem,
    const std::map<std::string, std::map<std::string, std::vector<CrcTriple>>>& crcsByStem)
{
    SingleFile out;
    out.projects.reserve(headers.size());
    for (const auto& header : headers) {
        Project p;
        p.header = header;                               // exact string + order from the index
        const std::string stem = StemForHeader(header);

        if (const auto mit = movesetsByStem.find(stem);
            mit != movesetsByStem.end() && !mit->second.projects.empty())
            p.sets = mit->second.projects.front().sets;  // authored sets; order authoritative

        if (const auto cit = crcsByStem.find(stem); cit != crcsByStem.end())
            for (auto& s : p.sets) {
                std::string key = s.name;
                if (EndsTxt(key)) key.erase(key.size() - 4);
                key = Low(std::move(key));
                if (const auto k = cit->second.find(key); k != cit->second.end()) s.crcs = k->second;
            }

        out.projects.push_back(std::move(p));
    }
    return out;
}

}  // namespace havok::animsetdata
