#include "FnisListParser.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace CommunityBehaviors::fnis {
namespace {

std::string ToLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string TrimLine(const std::string& line)
{
    const auto b = line.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = line.find_last_not_of(" \t\r\n");
    return line.substr(b, e - b + 1);
}

bool IsComment(const std::string& line)
{
    if (line.empty()) return true;
    if (line[0] == '\'') return true;
    if (line.size() >= 2 && line[0] == '/' && line[1] == '/') return true;
    return false;
}

// Split a line into whitespace-separated tokens.  Respects the FNIS convention that
// everything is space-delimited (no quoting).
std::vector<std::string> Tokenize(const std::string& line)
{
    std::vector<std::string> tokens;
    std::istringstream       ss(line);
    std::string              tok;
    while (ss >> tok) tokens.push_back(std::move(tok));
    return tokens;
}

// Try to parse a type code from a token.  Returns true on match.
bool ParseType(const std::string& tok, AnimType& out)
{
    const std::string low = ToLower(tok);
    if (low == "b")   { out = AnimType::Basic;              return true; }
    if (low == "o")   { out = AnimType::AnimObject;         return true; }
    if (low == "s")   { out = AnimType::Sequence;           return true; }
    if (low == "so")  { out = AnimType::SequenceOptimized;  return true; }
    if (low == "fu")  { out = AnimType::Furniture;          return true; }
    if (low == "fuo") { out = AnimType::FurnitureOptimized; return true; }
    if (low == "+")   { out = AnimType::Continuation;       return true; }
    if (low == "ofa") { out = AnimType::OffsetArm;          return true; }
    if (low == "pa")  { out = AnimType::Paired;             return true; }
    if (low == "km")  { out = AnimType::Killmove;           return true; }
    if (low == "aa")  { out = AnimType::AlternateAnim;      return true; }
    return false;
}

float ParseFloat(const char* begin, const char* end, float fallback)
{
    float val = fallback;
    std::from_chars(begin, end, val);
    return val;
}

// Parse the comma-separated options string (without the leading '-').
// e.g. "a,md,TweaponDraw/0.75,TweaponSheathe/8.08,B1.5"
void ParseOptions(const std::string& opts, AnimDecl& decl)
{
    std::size_t pos = 0;
    while (pos < opts.size()) {
        // Find the next comma (or end), but commas inside T<event>/<time> are tricky:
        // a triggered-event option like TweaponDraw/0.75 is one token ending at the next
        // comma.  We split on comma and then inspect each piece.
        std::size_t comma = opts.find(',', pos);
        if (comma == std::string::npos) comma = opts.size();
        const std::string opt = opts.substr(pos, comma - pos);
        pos = comma + 1;

        if (opt.empty()) continue;
        const std::string low = ToLower(opt);

        if (low == "a")                  { decl.acyclic        = true; continue; }
        if (low == "h")                  { decl.headtracking   = true; continue; }
        if (low == "md")                 { decl.motionDriven   = true; continue; }
        if (low == "tn")                 { decl.transitionNext = true; continue; }
        if (low == "k")                  { decl.known          = true; continue; }
        if (low == "bsa")                { decl.inBsa          = true; continue; }
        if (low == "o")                  { decl.hasAnimObjects = true; continue; }
        if (low == "st")                 { decl.stickyAO       = true; continue; }
        if (low == "ac" || low == "acs" || low == "acr") continue;  // camera flags — noted, not used

        // -B<n.m> — blend time
        if (low.size() > 1 && low[0] == 'b') {
            decl.blendTime = ParseFloat(opt.data() + 1, opt.data() + opt.size(), -1.f);
            continue;
        }

        // -D<time> — explicit duration (pa/km)
        if (low.size() > 1 && low[0] == 'd') {
            decl.duration = ParseFloat(opt.data() + 1, opt.data() + opt.size(), -1.f);
            continue;
        }

        // -T<Event>/<time> — triggered event during animation.
        // The option token is CASE-PRESERVING for the event name (e.g. "TweaponDraw/0.75").
        // Note: Tn (transition-next) is already handled above, so anything starting with
        // T that isn't "tn" is a triggered event.
        if (opt.size() > 1 && (opt[0] == 'T' || opt[0] == 't') && low != "tn") {
            const std::size_t slash = opt.find('/');
            if (slash != std::string::npos && slash > 1) {
                TriggeredEvent te;
                te.event = opt.substr(1, slash - 1);
                te.time  = ParseFloat(opt.data() + slash + 1, opt.data() + opt.size(), 0.f);
                decl.triggeredEvents.push_back(std::move(te));
            }
            continue;
        }
    }
}

}  // namespace

ListFile ParseListFile(std::string_view         text,
                       const std::string&        modName,
                       std::vector<std::string>* warnings)
{
    ListFile result;
    result.modName = modName;

    std::istringstream stream{ std::string(text) };
    std::string        rawLine;
    int                lineNo = 0;

    while (std::getline(stream, rawLine)) {
        ++lineNo;
        const std::string line = TrimLine(rawLine);
        if (IsComment(line)) continue;

        const auto tokens = Tokenize(line);
        if (tokens.empty()) continue;

        // Version line
        if (ToLower(tokens[0]) == "version") {
            if (tokens.size() >= 2) result.version = tokens[1];
            continue;
        }

        // AnimVar line:  AnimVar <name> <type> <value>
        if (ToLower(tokens[0]) == "animvar") {
            if (tokens.size() >= 4) {
                AnimVarDecl v;
                v.name  = tokens[1];
                v.type  = tokens[2];  // BOOL, INT32, REAL
                v.value = tokens[3];
                result.vars.push_back(std::move(v));
            } else if (warnings) {
                warnings->push_back("line " + std::to_string(lineNo) +
                                    ": malformed AnimVar (need 4 tokens): " + line);
            }
            continue;
        }

        // Animation line: <type> [-<options>] <event> <animFile> [<animObjects>...]
        AnimType type;
        if (!ParseType(tokens[0], type)) {
            if (warnings)
                warnings->push_back("line " + std::to_string(lineNo) +
                                    ": unknown type '" + tokens[0] + "': " + line);
            continue;
        }

        AnimDecl    decl;
        decl.type = type;

        std::size_t idx = 1;

        // Options? Token starts with '-'
        if (idx < tokens.size() && !tokens[idx].empty() && tokens[idx][0] == '-') {
            ParseOptions(tokens[idx].substr(1), decl);
            ++idx;
        }

        // Event name
        if (idx >= tokens.size()) {
            if (warnings)
                warnings->push_back("line " + std::to_string(lineNo) +
                                    ": missing event name: " + line);
            continue;
        }
        decl.event = tokens[idx++];

        // Animation file
        if (idx >= tokens.size()) {
            if (warnings)
                warnings->push_back("line " + std::to_string(lineNo) +
                                    ": missing animation file: " + line);
            continue;
        }
        decl.animFile = tokens[idx++];

        // Remaining tokens are AnimObject names (for o/so/fuo/km types, or with -o flag).
        // AnimObjects can have a /<time> suffix for km types (e.g. "AnimObjectSword/1").
        while (idx < tokens.size()) {
            decl.animObjects.push_back(tokens[idx++]);
            decl.hasAnimObjects = true;
        }

        result.anims.push_back(std::move(decl));
    }

    return result;
}

ScanResult ScanForLists(const fs::path&           animationsDir,
                        const std::string&         actor,
                        std::vector<std::string>*  warnings)
{
    ScanResult result;
    result.actor = actor;

    std::error_code ec;
    if (!fs::is_directory(animationsDir, ec)) return result;

    // Walk each subdirectory of the animations folder (each is a mod's animation folder).
    for (fs::directory_iterator di(animationsDir, ec), dend; !ec && di != dend; di.increment(ec)) {
        std::error_code de;
        if (!di->is_directory(de)) continue;
        const fs::path modDir = di->path();

        // Look for FNIS_*_List.txt in this mod directory (non-recursive — FNIS puts them
        // at the top level of the mod's animation folder).
        for (fs::directory_iterator fi(modDir, de), fend; !de && fi != fend; fi.increment(de)) {
            std::error_code fe;
            if (!fi->is_regular_file(fe)) continue;

            const std::string filename = fi->path().filename().string();
            const std::string low      = ToLower(filename);

            // Match FNIS_*_List.txt (case-insensitive)
            if (low.size() < 15) continue;  // "fnis_x_list.txt" = 15 chars minimum
            if (low.rfind("fnis_", 0) != 0) continue;
            if (low.substr(low.size() - 9) != "_list.txt") continue;

            // Extract mod name from filename: FNIS_<modName>_List.txt
            // The last "_List.txt" is 9 chars; "FNIS_" is 5 chars.
            const std::string modName = filename.substr(5, filename.size() - 5 - 9);
            if (modName.empty()) continue;

            // Read and parse
            std::ifstream     f(fi->path(), std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(f)),
                                    std::istreambuf_iterator<char>());
            if (text.empty()) continue;

            ListFile lf = ParseListFile(text, modName, warnings);
            if (lf.anims.empty() && lf.vars.empty()) continue;

            // Prefix each animFile with the mod folder name so paths are relative to the
            // actor's animations/ directory (matching animationNames form).  Paths that
            // already contain a parent-dir reference (e.g. "..\OtherMod\x.hkx") are kept
            // as-is — the FNIS spec allows cross-mod references with -k.
            const std::string modFolder = modDir.filename().string();
            for (auto& anim : lf.anims) {
                if (!anim.animFile.empty() && anim.animFile[0] != '.' &&
                    anim.animFile.find('\\') == std::string::npos &&
                    anim.animFile.find('/') == std::string::npos) {
                    anim.animFile = modFolder + "\\" + anim.animFile;
                }
            }

            result.lists.push_back(std::move(lf));
        }
    }

    return result;
}

// ── Grouping ────────────────────────────────────────────────────────────────

namespace {
bool IsSequenceStart(AnimType t)
{
    return t == AnimType::Sequence || t == AnimType::SequenceOptimized ||
           t == AnimType::Furniture || t == AnimType::FurnitureOptimized;
}
}  // namespace

std::vector<AnimGroup> GroupAnimations(const ScanResult& scan)
{
    std::vector<AnimGroup> groups;

    for (const auto& lf : scan.lists) {
        AnimGroup* current = nullptr;
        for (const auto& a : lf.anims) {
            if (a.type == AnimType::Continuation) {
                if (current)
                    current->continuations.push_back(&a);
                continue;
            }
            groups.push_back({&a, {}});
            current = IsSequenceStart(a.type) ? &groups.back() : nullptr;
        }
    }

    return groups;
}

}  // namespace CommunityBehaviors::fnis
