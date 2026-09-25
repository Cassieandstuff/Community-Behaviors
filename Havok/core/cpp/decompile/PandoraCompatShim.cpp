// SPDX-License-Identifier: GPL-3.0-or-later
// PandoraCompatShim — CONVERTER-ONLY. See PandoraCompatShim.h for the why. This is a faithful
// port of Pandora's Nemesis text-param application: NemesisParser.ParseReplaceEdit +
// PackFileEditor.ReplaceText (occurrence-counted regex replacement over the base array value).
//
// The one subtlety that makes it match Pandora bit-for-bit: Pandora's per-block occurrence
// index is counted over a prefix in which the PRIOR blocks' OPEN (new) values are already
// applied (its preValue is reconstructed keeping OPEN, dropping ORIGINAL), and the match scan
// runs over the source with those prior edits applied. So we mutate the token stream as we go
// and count each block's index over the *mutated* prefix — not the pristine base.

#include <decompile/PandoraCompatShim.h>

#include <algorithm>
#include <cctype>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace havok::compat {
namespace {

// Collapse runs of whitespace/parens to a single space and trim tab/CR/LF/parens at the ends
// — mirrors Pandora PackFileEditor.NormalizeElementValue — then split into tokens.
std::vector<std::string> tokenize(std::string_view v) {
    std::vector<std::string> t;
    std::size_t i = 0;
    auto sep = [](char c) { return std::isspace((unsigned char)c) || c == '(' || c == ')'; };
    while (i < v.size()) {
        while (i < v.size() && sep(v[i])) ++i;
        std::size_t b = i;
        while (i < v.size() && !sep(v[i])) ++i;
        if (i > b) t.emplace_back(v.substr(b, i - b));
    }
    return t;
}

// Turn ORIGINAL tokens into Pandora's search regex over a single-space-joined stream: escape
// (* + ? | ^ . #) per token, join with `\s*` — mirrors ReplaceText's oldValue transform.
std::regex buildOldRegex(const std::vector<std::string>& toks) {
    static const std::regex esc(R"((\*|\+|\?|\||\^|\.|\#))");
    std::string pat;
    for (std::size_t i = 0; i < toks.size(); ++i) {
        if (i) pat += R"(\s*)";
        pat += std::regex_replace(toks[i], esc, R"(\$1)");
    }
    return std::regex(pat);
}

int countMatches(const std::regex& re, const std::string& hay) {
    return static_cast<int>(std::distance(
        std::sregex_iterator(hay.begin(), hay.end(), re), std::sregex_iterator()));
}

std::string join(const std::vector<std::string>& t, std::size_t from, std::size_t to) {
    std::string s;
    for (std::size_t i = from; i < to && i < t.size(); ++i) { if (i > from) s += ' '; s += t[i]; }
    return s;
}

// One MOD_CODE replace block, at token granularity.
struct ReplaceBlock { std::vector<std::string> open, original; std::size_t baseTokenStart; };

// Parse a text param's inner (still carrying MOD_CODE comments) into (baseTokens, blocks).
// baseTokens = the pre-patch array (ORIGINAL kept, OPEN dropped). Returns false on an
// INSERT-only block (no ORIGINAL) or malformed input — caller then leaves the param alone.
bool parseTextParam(std::string_view inner, std::vector<std::string>& baseTokens,
                    std::vector<ReplaceBlock>& blocks) {
    auto commentInner = [&](std::size_t open, std::size_t& b, std::size_t& e) -> bool {
        std::size_t close = inner.find("-->", open + 4);
        if (close == std::string_view::npos) return false;
        b = open + 4; e = close;
        while (b < e && std::isspace((unsigned char)inner[b])) ++b;
        while (e > b && std::isspace((unsigned char)inner[e - 1])) --e;
        return true;
    };
    auto isMark = [&](std::size_t p, std::string_view want) {
        std::size_t b, e; if (!commentInner(p, b, e)) return false;
        return inner.substr(b, e - b) == want;
    };
    auto isOpen = [&](std::size_t p) {
        std::size_t b, e; if (!commentInner(p, b, e)) return false;
        std::string_view s = inner.substr(b, e - b);
        return s.size() >= 13 && s.substr(0, 9) == "MOD_CODE " && s.substr(s.size() - 4) == "OPEN";
    };
    std::size_t i = 0;
    while (i < inner.size()) {
        std::size_t c = inner.find("<!--", i);
        std::string_view lit = (c == std::string_view::npos) ? inner.substr(i) : inner.substr(i, c - i);
        for (auto& tk : tokenize(lit)) baseTokens.push_back(std::move(tk));   // literal base tokens
        if (c == std::string_view::npos) break;
        if (!isOpen(c)) { i = c + 4; continue; }                              // stray comment
        std::size_t openEnd = inner.find("-->", c) + 3;                       // after "OPEN -->"
        std::size_t nx = inner.find("<!--", openEnd);
        while (nx != std::string_view::npos && !isMark(nx, "ORIGINAL") && !isMark(nx, "CLOSE"))
            nx = inner.find("<!--", nx + 4);
        if (nx == std::string_view::npos) return false;                       // malformed
        std::vector<std::string> openToks = tokenize(inner.substr(openEnd, nx - openEnd));
        if (isMark(nx, "CLOSE")) return false;                               // INSERT-only -> caller
        std::size_t origStart = inner.find("-->", nx) + 3;
        std::size_t cl = inner.find("<!--", origStart);
        while (cl != std::string_view::npos && !isMark(cl, "CLOSE")) cl = inner.find("<!--", cl + 4);
        if (cl == std::string_view::npos) return false;
        std::vector<std::string> origToks = tokenize(inner.substr(origStart, cl - origStart));
        blocks.push_back({ std::move(openToks), std::move(origToks), baseTokens.size() });
        for (auto& tk : blocks.back().original) baseTokens.push_back(tk);     // base keeps ORIGINAL
        i = inner.find("-->", cl) + 3;                                        // after "CLOSE -->"
    }
    return true;
}

}  // namespace

void ApplyNemesisTextArrayEdits(std::string& src) {
    std::string out;
    out.reserve(src.size());
    std::size_t i = 0;
    while (i < src.size()) {
        std::size_t tag = src.find("<hkparam", i);
        if (tag == std::string::npos) { out.append(src, i, std::string::npos); break; }
        std::size_t openEnd = src.find('>', tag);
        if (openEnd == std::string::npos) { out.append(src, i, std::string::npos); break; }
        bool selfClose = openEnd > tag && src[openEnd - 1] == '/';
        std::size_t innerStart = openEnd + 1;
        std::size_t close = src.find("</hkparam>", innerStart);
        if (selfClose || close == std::string::npos) {
            std::size_t stop = selfClose ? openEnd + 1 : src.size();
            out.append(src, i, stop - i);
            i = stop;
            continue;
        }
        std::string_view inner(src.data() + innerStart, close - innerStart);
        // Only pure-text params carrying MOD_CODE are ours: the only '<' inside must be the
        // MOD_CODE/ORIGINAL/CLOSE comments ("<!--"). A nested element (ref/struct param) is
        // left for the caller's xml::StripPatchOriginals pass (keeps OPEN positionally).
        bool hasMod = inner.find("MOD_CODE") != std::string_view::npos;
        bool hasElement = false;
        for (std::size_t p = inner.find('<'); p != std::string_view::npos; p = inner.find('<', p + 1))
            if (inner.compare(p, 4, "<!--") != 0) { hasElement = true; break; }
        std::size_t paramEnd = close + 10;               // past "</hkparam>"
        if (!hasMod || hasElement) { out.append(src, i, paramEnd - i); i = paramEnd; continue; }

        std::vector<std::string> tokens;                 // mutating source token stream
        std::vector<ReplaceBlock> blocks;
        if (!parseTextParam(inner, tokens, blocks)) { out.append(src, i, paramEnd - i); i = paramEnd; continue; }

        bool ok = true;
        for (const auto& blk : blocks) {
            try {
                std::regex re = buildOldRegex(blk.original);
                // Pandora: count the ORIGINAL pattern in the prefix WITH prior OPENs applied
                // (== our mutated prefix), then replace that Nth non-overlapping match in the
                // full (mutated) stream.
                int target = countMatches(re, join(tokens, 0, blk.baseTokenStart));
                std::string full = join(tokens, 0, tokens.size());
                int idx = -1;
                bool done = false;
                for (auto it = std::sregex_iterator(full.begin(), full.end(), re),
                          end = std::sregex_iterator(); it != end; ++it) {
                    if (++idx != target) continue;
                    // Map the char match to a token index (single-space join => spaces-before).
                    std::size_t sp = static_cast<std::size_t>((*it).position());
                    std::size_t ti = static_cast<std::size_t>(std::count(full.begin(), full.begin() + sp, ' '));
                    if (ti + blk.original.size() <= tokens.size()) {
                        tokens.erase(tokens.begin() + ti, tokens.begin() + ti + blk.original.size());
                        tokens.insert(tokens.begin() + ti, blk.open.begin(), blk.open.end());
                    }
                    done = true;
                    break;
                }
                (void)done;   // no Nth match => Pandora leaves the value unchanged (no-op)
            } catch (const std::regex_error&) {
                ok = false; break;
            }
        }
        if (!ok) { out.append(src, i, paramEnd - i); i = paramEnd; continue; }

        out.append(src, i, innerStart - i);              // the "<hkparam ...>" open tag verbatim
        out += '\n';
        out += join(tokens, 0, tokens.size());           // Pandora-applied flat value
        out += '\n';
        out += "</hkparam>";
        i = paramEnd;
    }
    src.swap(out);
}

}  // namespace havok::compat
