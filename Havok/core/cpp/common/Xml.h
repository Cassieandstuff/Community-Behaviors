#pragma once

// Minimal first-party XML parser — enough for Havok tagfiles (hkpackfile /
// hkobject / hkparam / hkcstring). No external dep (pugixml is editor-only; this
// header is on the plugin triplet too). Handles: elements, attributes, text,
// self-closing tags, comments, the <?xml?> decl, and the handful of entities the
// tagfile uses. Not a general-purpose XML engine — no namespaces, DTDs, CDATA.
//
// This is the parse layer under the Havok-XML reader (Nemesis/Pandora patch
// import): a patch node is `<hkobject name="#NNNN" class="C">…</hkobject>` and we
// need its class, id, and fields (scalars, #NNNN refs, arrays, inline structs).

#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace havok::xml {

struct Node {
    std::string tag;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::string text;              // direct text content (concatenated, trimmed of surrounding ws)
    std::vector<Node> children;

    std::string_view attr(std::string_view name) const {
        for (const auto& [k, v] : attrs) if (k == name) return v;
        return {};
    }
    bool hasAttr(std::string_view name) const {
        for (const auto& [k, _] : attrs) if (k == name) return true;
        return false;
    }
    // First child element with the given tag (nullptr if none).
    const Node* child(std::string_view t) const {
        for (const auto& c : children) if (c.tag == t) return &c;
        return nullptr;
    }
};

namespace detail {

inline void skipWs(std::string_view s, std::size_t& i) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
}

// Decode the entities a Havok tagfile actually emits.
inline void appendDecoded(std::string& out, std::string_view s) {
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] == '&') {
            if (s.compare(i, 4, "&lt;") == 0)        { out += '<'; i += 4; continue; }
            if (s.compare(i, 4, "&gt;") == 0)        { out += '>'; i += 4; continue; }
            if (s.compare(i, 5, "&amp;") == 0)       { out += '&'; i += 5; continue; }
            if (s.compare(i, 6, "&quot;") == 0)      { out += '"'; i += 6; continue; }
            if (s.compare(i, 6, "&apos;") == 0)      { out += '\''; i += 6; continue; }
        }
        out += s[i++];
    }
}

// Parse one element starting at s[i]=='<' (not a comment / decl). Returns the node;
// advances i past the element's closing tag.
inline Node parseElement(std::string_view s, std::size_t& i);

// Skip <!-- --> comments and <?...?> declarations/PIs at s[i].
inline bool skipMisc(std::string_view s, std::size_t& i) {
    if (s.compare(i, 4, "<!--") == 0) {
        const auto e = s.find("-->", i + 4);
        i = (e == std::string_view::npos) ? s.size() : e + 3;
        return true;
    }
    if (s.compare(i, 2, "<?") == 0) {
        const auto e = s.find("?>", i + 2);
        i = (e == std::string_view::npos) ? s.size() : e + 2;
        return true;
    }
    if (s.compare(i, 2, "<!") == 0) {   // <!DOCTYPE ...> etc.
        const auto e = s.find('>', i + 2);
        i = (e == std::string_view::npos) ? s.size() : e + 1;
        return true;
    }
    return false;
}

inline Node parseElement(std::string_view s, std::size_t& i) {
    Node n;
    ++i;  // past '<'
    // tag name
    const std::size_t ts = i;
    while (i < s.size() && s[i] != ' ' && s[i] != '\t' && s[i] != '\r' && s[i] != '\n' &&
           s[i] != '>' && s[i] != '/')
        ++i;
    n.tag = std::string(s.substr(ts, i - ts));

    // attributes
    for (;;) {
        skipWs(s, i);
        if (i >= s.size() || s[i] == '>' || s[i] == '/') break;
        const std::size_t as = i;
        while (i < s.size() && s[i] != '=' && s[i] != ' ' && s[i] != '\t' &&
               s[i] != '\r' && s[i] != '\n' && s[i] != '>' && s[i] != '/')
            ++i;
        std::string key(s.substr(as, i - as));
        skipWs(s, i);
        std::string val;
        if (i < s.size() && s[i] == '=') {
            ++i; skipWs(s, i);
            if (i < s.size() && (s[i] == '"' || s[i] == '\'')) {
                const char q = s[i++];
                const std::size_t vs = i;
                while (i < s.size() && s[i] != q) ++i;
                appendDecoded(val, s.substr(vs, i - vs));
                if (i < s.size()) ++i;  // past closing quote
            }
        }
        n.attrs.emplace_back(std::move(key), std::move(val));
    }

    if (i < s.size() && s[i] == '/') {   // self-closing
        i += 2;                          // past "/>"
        return n;
    }
    if (i < s.size() && s[i] == '>') ++i;  // past '>'

    // content: children + text until the matching </tag>
    while (i < s.size()) {
        if (s[i] == '<') {
            if (s.compare(i, 2, "</") == 0) {              // closing tag
                const auto e = s.find('>', i);
                i = (e == std::string_view::npos) ? s.size() : e + 1;
                break;
            }
            if (skipMisc(s, i)) continue;
            n.children.push_back(parseElement(s, i));
        } else {
            const std::size_t vs = i;
            while (i < s.size() && s[i] != '<') ++i;
            appendDecoded(n.text, s.substr(vs, i - vs));
        }
    }
    // Whitespace policy: a node WITH children carries only inter-element formatting whitespace as text
    // — clear it. A LEAF node's text is the value itself: keep it VERBATIM, because a string value's
    // leading/trailing space is real data (vanilla has variable names like " iState_NPCSneaking" and
    // expressions like "iCombatStance = 1 "). Numeric/ref array text (also leaf) is consumed through
    // whitespace-tolerant parsers (strtol/strtof/token-split), so keeping its formatting ws is harmless.
    if (!n.children.empty()) n.text.clear();
    return n;
}

}  // namespace detail

// Nemesis/Pandora field edits wrap a change in comment markers:
//   <!-- MOD_CODE ~tag~ OPEN --> <modded…> <!-- ORIGINAL --> <vanilla…> <!-- CLOSE -->
// The ORIGINAL block is absent for a pure insertion, and the modded block is
// absent for a deletion. Applying the patch = keep the modded content, drop the
// vanilla — i.e. **excise every ORIGINAL…CLOSE span** (inclusive of both marker
// comments). The remaining OPEN/CLOSE comments are ordinary comments the parser
// ignores. Matching is on a comment's trimmed inner text, so marker spacing
// ("<!-- ORIGINAL -->" vs "<!--ORIGINAL-->") doesn't matter.
//
// MUST run on a Nemesis patch node before Parse(); a no-op on clean tagfiles.
inline void StripPatchOriginals(std::string& s) {
    auto commentIs = [&](std::size_t open, std::string_view want) -> bool {
        // `open` points at "<!--"; check the trimmed inner text equals `want`.
        const std::size_t inner = open + 4;
        const std::size_t close = s.find("-->", inner);
        if (close == std::string::npos) return false;
        std::size_t b = inner, e = close;
        while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
        while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
        return s.compare(b, e - b, want) == 0;
    };
    for (std::size_t p = s.find("<!--"); p != std::string::npos; p = s.find("<!--", p)) {
        if (!commentIs(p, "ORIGINAL")) { p += 4; continue; }
        // Find the next CLOSE comment and excise [p, end-of-CLOSE).
        std::size_t q = s.find("<!--", p + 4);
        while (q != std::string::npos && !commentIs(q, "CLOSE")) q = s.find("<!--", q + 4);
        if (q == std::string::npos) { s.erase(p); break; }
        const std::size_t end = s.find("-->", q) + 3;
        s.erase(p, end - p);
    }
}

// Inverse of StripPatchOriginals: reconstruct the node AS THE MOD SAW ITS BASE — keep
// each MOD_CODE block's ORIGINAL content and drop the mod's OPEN content (and drop
// OPEN-only additions that have no ORIGINAL). Parsing this and diffing it against
// StripPatchOriginals(same src) yields exactly the fields the mod changed, so a stale
// field the mod left OUTSIDE MOD_CODE (one that merely differs from true vanilla) is
// never mistaken for a change.
inline void StripToOriginal(std::string& s) {
    auto inner = [&](std::size_t open, std::size_t& b, std::size_t& e) -> bool {
        const std::size_t close = s.find("-->", open + 4);
        if (close == std::string::npos) return false;
        b = open + 4; e = close;
        while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
        while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
        return true;
    };
    auto isOpen = [&](std::size_t p) {
        std::size_t b, e; if (!inner(p, b, e)) return false;
        return (e - b) >= 13 && s.compare(b, 9, "MOD_CODE ") == 0 && s.compare(e - 4, 4, "OPEN") == 0;
    };
    auto isMark = [&](std::size_t p, const char* w) {
        std::size_t b, e; if (!inner(p, b, e)) return false;
        return s.compare(b, e - b, w) == 0;
    };
    for (std::size_t p = s.find("<!--"); p != std::string::npos; p = s.find("<!--", p)) {
        if (!isOpen(p)) { p += 4; continue; }
        std::size_t q = s.find("<!--", p + 4);
        while (q != std::string::npos && !isMark(q, "ORIGINAL") && !isMark(q, "CLOSE")) q = s.find("<!--", q + 4);
        if (q == std::string::npos) { s.erase(p); break; }
        // Excise [OPEN-marker, end-of-(ORIGINAL|CLOSE)-comment): with an ORIGINAL this
        // drops the OPEN content and leaves the ORIGINAL content; a bare CLOSE means a
        // pure addition, so the added content is dropped entirely.
        const std::size_t end = s.find("-->", q) + 3;
        s.erase(p, end - p);
    }
}

// Parse a whole XML document; returns the root element (empty tag on failure).
inline Node Parse(std::string_view src) {
    std::size_t i = 0;
    while (i < src.size()) {
        detail::skipWs(src, i);
        if (i >= src.size()) break;
        if (src[i] != '<') { ++i; continue; }
        if (detail::skipMisc(src, i)) continue;
        return detail::parseElement(src, i);
    }
    return {};
}

}  // namespace havok::xml
