#include "TreeDiff.h"

#include <RymlInclude.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace havok::diff {

namespace {

namespace ryml = c4::yml;

// csubstr -> std::string.
std::string sv(c4::csubstr s) { return std::string(s.str, s.len); }

bool nodeIsScalar(ryml::NodeRef n) { return n.readable() && n.has_val(); }

// A bare integer scalar (optional leading '-', all digits) — the numeric-fallback `id:` form.
bool isBareInteger(const std::string& s) {
    if (s.empty()) return false;
    std::size_t i = (s[0] == '-') ? 1 : 0;
    if (i >= s.size()) return false;
    for (; i < s.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    return true;
}

// Full numeric (int or float) parse of a scalar; sets `ok`.
double parseNum(const std::string& s, bool& ok) {
    if (s.empty()) { ok = false; return 0.0; }
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    ok = (end == s.c_str() + s.size());
    return v;
}

// Two scalars are semantically equal: identical text, OR both numeric and equal, OR both ±0.0
// (signed-zero tolerance, reused from the skeleton-full-schema-check rule). Floats already come
// through the decompiler as %.9g and enums as canonical names, so text usually matches directly.
bool scalarEqual(const std::string& a, const std::string& b) {
    if (a == b) return true;
    bool na = false, nb = false;
    const double va = parseNum(a, na), vb = parseNum(b, nb);
    if (na && nb) {
        if (va == vb) return true;                 // 1 vs 1.0, 0.5 vs 0.500000000
        if (va == 0.0 && vb == 0.0) return true;   // +0.0 vs -0.0
        // Emit-precision tolerance: CB and Pandora compile the SAME value to slightly different
        // floats (e.g. a 1/30 trigger localTime as 0.0333333351 vs 0.0333329998). Treat a tiny
        // relative/absolute delta as equal — real changes (priority 0 vs 40, duration 0.1 vs 0.2)
        // are orders of magnitude larger.
        const double diff = std::fabs(va - vb);
        const double scale = std::max({1.0, std::fabs(va), std::fabs(vb)});
        if (diff <= 1e-5 * scale) return true;
    }
    return false;
}

// Normalize a scalar for an array element's SECONDARY key: round a numeric value to a few
// significant digits so the same emit-precision noise yields the SAME key and elements align
// (then scalarEqual's tolerance confirms the field). Non-numeric scalars pass through verbatim.
std::string keyScalar(const std::string& s) {
    bool ok = false;
    const double v = parseNum(s, ok);
    if (!ok) return s;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.5g", v);
    return buf;
}

// Compact, deterministic re-serialization of a node subtree (for added/removed/only-in values).
void nodeToText(ryml::NodeRef n, std::string& out, int depth) {
    if (!n.readable()) return;
    if (n.has_val()) { out += sv(n.val()); return; }
    if (n.is_map()) {
        out += "{";
        bool first = true;
        for (auto c : n) {
            if (!first) out += ", ";
            first = false;
            if (c.has_key()) out += sv(c.key()) + ": ";
            nodeToText(c, out, depth + 1);
        }
        out += "}";
    } else if (n.is_seq()) {
        out += "[";
        bool first = true;
        for (auto c : n) {
            if (!first) out += ", ";
            first = false;
            nodeToText(c, out, depth + 1);
        }
        out += "]";
    }
}
std::string nodeToText(ryml::NodeRef n) { std::string s; nodeToText(n, s, 0); return s; }

std::string keyName(ryml::NodeRef n) { return n.has_key() ? sv(n.key()) : std::string(); }

// The trailing field name of a dotted path (drops any "[secondary]" suffix), used to pick the
// array policy for a sequence.
std::string lastField(const std::string& path) {
    std::size_t dot = path.find_last_of('.');
    std::string f = (dot == std::string::npos) ? path : path.substr(dot + 1);
    std::size_t br = f.find('[');
    if (br != std::string::npos) f = f.substr(0, br);
    return f;
}

struct Differ {
    const DiffPolicy&        policy;
    std::vector<FieldDiff>&  out;

    void scalarDiff(const std::string& path, ryml::NodeRef a, ryml::NodeRef b) {
        const std::string va = sv(a.val()), vb = sv(b.val());
        if (!scalarEqual(va, vb)) out.push_back({path, va, vb, "changed"});
    }

    // Is `key` a raw index field whose companion name agrees on both sides? Then it is volatile.
    bool volatileIndexSkipped(const std::string& key, ryml::NodeRef a,
                              ryml::NodeRef b) {
        for (const auto& [rawF, nameF] : policy.volatileIndexFields) {
            if (key != rawF) continue;
            c4::csubstr nk = c4::to_csubstr(nameF.c_str());
            if (a.has_child(nk) && b.has_child(nk)) {
                auto an = a[nk], bn = b[nk];
                if (an.has_val() && bn.has_val() && scalarEqual(sv(an.val()), sv(bn.val())))
                    return true;   // name matches -> the raw index is expected to renumber; skip
            }
        }
        return false;
    }

    void mapDiff(const std::string& path, ryml::NodeRef a, ryml::NodeRef b) {
        // Union of keys, in a stable order (A's order, then B-only keys).
        std::vector<std::string> keys;
        std::set<std::string>    seen;
        for (auto c : a) { std::string k = keyName(c); if (seen.insert(k).second) keys.push_back(k); }
        for (auto c : b) { std::string k = keyName(c); if (seen.insert(k).second) keys.push_back(k); }

        for (const std::string& k : keys) {
            const std::string fp = path.empty() ? k : path + "." + k;
            c4::csubstr ck = c4::to_csubstr(k.c_str());
            const bool inA = a.has_child(ck), inB = b.has_child(ck);

            // Skip a numeric-fallback `id:` (encounter-order volatile).
            if (policy.skipNumericFallbackId && k == "id" && inA && inB) {
                auto av = a[ck], bv = b[ck];
                if (av.has_val() && bv.has_val() &&
                    isBareInteger(sv(av.val())) && isBareInteger(sv(bv.val())))
                    continue;
            }
            if (inA && inB && volatileIndexSkipped(k, a, b)) continue;

            if (inA && !inB) { out.push_back({fp, nodeToText(a[ck]), "", "only-a"}); continue; }
            if (!inA && inB) { out.push_back({fp, "", nodeToText(b[ck]), "only-b"}); continue; }
            nodeDiff(fp, a[ck], b[ck]);
        }
    }

    // Composite secondary key for a key-aligned array element.
    std::string secondaryKey(ryml::NodeRef el, const std::vector<std::string>& sub) {
        if (!el.is_map()) return {};
        std::string key;
        for (const std::string& s : sub) {
            c4::csubstr cs = c4::to_csubstr(s.c_str());
            key += "|";
            if (el.has_child(cs) && el[cs].has_val()) key += keyScalar(sv(el[cs].val()));
        }
        return key;
    }

    void positionalDiff(const std::string& path, ryml::NodeRef a, ryml::NodeRef b) {
        std::vector<ryml::NodeRef> ea, eb;
        for (auto c : a) ea.push_back(c);
        for (auto c : b) eb.push_back(c);
        const std::size_t n = std::min(ea.size(), eb.size());
        for (std::size_t i = 0; i < n; ++i)
            nodeDiff(path + "[" + std::to_string(i) + "]", ea[i], eb[i]);
        for (std::size_t i = n; i < ea.size(); ++i)
            out.push_back({path + "[" + std::to_string(i) + "]", nodeToText(ea[i]), "", "removed"});
        for (std::size_t i = n; i < eb.size(); ++i)
            out.push_back({path + "[" + std::to_string(i) + "]", "", nodeToText(eb[i]), "added"});
    }

    void multisetDiff(const std::string& path, ryml::NodeRef a, ryml::NodeRef b) {
        std::multiset<std::string> ma, mb;
        for (auto c : a) ma.insert(nodeToText(c));
        for (auto c : b) mb.insert(nodeToText(c));
        std::vector<std::string> onlyA, onlyB;
        std::set_difference(ma.begin(), ma.end(), mb.begin(), mb.end(), std::back_inserter(onlyA));
        std::set_difference(mb.begin(), mb.end(), ma.begin(), ma.end(), std::back_inserter(onlyB));
        for (const auto& s : onlyA) out.push_back({path, s, "", "removed"});
        for (const auto& s : onlyB) out.push_back({path, "", s, "added"});
    }

    void keyedDiff(const std::string& path, ryml::NodeRef a, ryml::NodeRef b,
                   const std::vector<std::string>& sub, bool reportReorder) {
        // Build ordered (key -> element) for each side; detect intra-side key collisions.
        std::vector<std::pair<std::string, ryml::NodeRef>> la, lb;
        std::unordered_map<std::string, int> ca, cb;
        for (auto c : a) { std::string k = secondaryKey(c, sub); la.push_back({k, c}); ca[k]++; }
        for (auto c : b) { std::string k = secondaryKey(c, sub); lb.push_back({k, c}); cb[k]++; }
        bool collision = false;
        for (auto& [k, n] : ca) if (n > 1) collision = true;
        for (auto& [k, n] : cb) if (n > 1) collision = true;
        if (collision) {
            // Duplicate secondary keys within a side — can't align by key. Fall back to a positional
            // compare, which reports only REAL element differences (identical-and-same-order arrays,
            // the common case for collision-heavy wildcard blocks, then produce no diff). No blanket
            // "reordered" sentinel: it fired even when the arrays matched positionally (false positive).
            positionalDiff(path, a, b);
            return;
        }
        std::unordered_map<std::string, ryml::NodeRef> mb;
        for (auto& [k, n] : lb) mb.emplace(k, n);
        std::set<std::string> matchedKeys;
        for (auto& [k, an] : la) {
            auto it = mb.find(k);
            if (it == mb.end()) { out.push_back({path + "[" + k + "]", nodeToText(an), "", "removed"}); continue; }
            matchedKeys.insert(k);
            nodeDiff(path + "[" + k + "]", an, it->second);
        }
        for (auto& [k, bn] : lb)
            if (!matchedKeys.count(k)) out.push_back({path + "[" + k + "]", "", nodeToText(bn), "added"});
        // Reorder detection: the common keys must appear in the same relative order on both sides.
        if (reportReorder) {
            std::vector<std::string> orderA, orderB;
            for (auto& [k, n] : la) if (matchedKeys.count(k)) orderA.push_back(k);
            for (auto& [k, n] : lb) if (matchedKeys.count(k)) orderB.push_back(k);
            if (orderA != orderB) {
                std::string ja, jb;
                for (auto& k : orderA) ja += (ja.empty() ? "" : ",") + k;
                for (auto& k : orderB) jb += (jb.empty() ? "" : ",") + k;
                out.push_back({path, ja, jb, "reordered"});
            }
        }
    }

    void seqDiff(const std::string& path, ryml::NodeRef a, ryml::NodeRef b) {
        const std::string field = lastField(path);
        auto ok = policy.orderedKeyedArrays.find(field);
        if (ok != policy.orderedKeyedArrays.end()) { keyedDiff(path, a, b, ok->second, true); return; }
        auto uk = policy.unorderedKeyedArrays.find(field);
        if (uk != policy.unorderedKeyedArrays.end()) { keyedDiff(path, a, b, uk->second, false); return; }
        if (policy.multisetArrays.count(field))     { multisetDiff(path, a, b); return; }
        positionalDiff(path, a, b);
    }

    void nodeDiff(const std::string& path, ryml::NodeRef a, ryml::NodeRef b) {
        const bool sa = nodeIsScalar(a), sb = nodeIsScalar(b);
        if (sa && sb) { scalarDiff(path, a, b); return; }
        if (a.is_map() && b.is_map()) { mapDiff(path, a, b); return; }
        if (a.is_seq() && b.is_seq()) { seqDiff(path, a, b); return; }
        // Shape mismatch (scalar vs container, map vs seq).
        out.push_back({path, nodeToText(a), nodeToText(b), "type"});
    }
};

std::vector<FieldDiff> diffOneRecord(const std::string& yamlA, const std::string& yamlB,
                                     const DiffPolicy& policy) {
    std::vector<FieldDiff> fields;
    std::string sa = yamlA, sb = yamlB;   // parse_in_place mutates + is referenced by the tree
    try {
        ryml::Tree ta = ryml::parse_in_place(c4::to_substr(sa));
        ryml::Tree tb = ryml::parse_in_place(c4::to_substr(sb));
        Differ d{policy, fields};
        d.nodeDiff("", ta.rootref(), tb.rootref());
    } catch (const std::exception&) {
        // Unparseable YAML on one side: fall back to a byte verdict so a diff is still surfaced.
        if (yamlA != yamlB) fields.push_back({"(unparsed)", "", "", "changed"});
    }
    return fields;
}

} // namespace

DiffResult DiffRecordSets(const RecordSet& a, const RecordSet& b, const DiffPolicy& policy) {
    DiffResult r;
    for (const auto& [k, ya] : a) {
        auto it = b.find(k);
        if (it == b.end()) { r.onlyInA.push_back(k); continue; }
        if (ya == it->second) continue;                     // fast path: byte-equal (same emitter)
        auto fields = diffOneRecord(ya, it->second, policy);
        if (!fields.empty()) r.changed.push_back({k, std::move(fields)});
    }
    for (const auto& [k, yb] : b)
        if (!a.count(k)) r.onlyInB.push_back(k);
    return r;
}

std::string SanitizeKey(const std::string& key) {
    std::string s;
    s.reserve(key.size());
    for (char c : key) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_') s += c;
        else if (c == ':' ) s += "__";
        else s += '_';
    }
    if (s.empty()) s = "_";
    if (s.size() > 180) s = s.substr(0, 160) + "_" + std::to_string(std::hash<std::string>{}(key));
    return s;
}

void WriteArtifactDelta(const std::filesystem::path& deltaDir, const std::string& domain,
                        const std::string& artifactRel, const DiffResult& result,
                        SummaryEntry& entry) {
    namespace fs = std::filesystem;
    entry.artifact  = artifactRel;
    entry.domain    = domain;
    entry.onlyA     = static_cast<int>(result.onlyInA.size());
    entry.onlyB     = static_cast<int>(result.onlyInB.size());
    entry.differing = static_cast<int>(result.changed.size());

    const fs::path base = deltaDir / domain / artifactRel;
    std::error_code ec;

    for (const auto& rd : result.changed) {
        fs::create_directories(base, ec);
        std::ofstream os(base / (SanitizeKey(rd.key) + ".diff.yaml"), std::ios::binary);
        if (!os) continue;
        os << "record: " << rd.key << "\n";
        os << "domain: " << domain << "\n";
        os << "artifact: " << artifactRel << "\n";
        os << "diffs:\n";
        for (const auto& f : rd.fields) {
            os << "  - path: " << f.path << "\n";
            os << "    kind: " << f.kind << "\n";
            os << "    a: " << f.a << "\n";
            os << "    b: " << f.b << "\n";
        }
    }
    if (!result.onlyInA.empty() || !result.onlyInB.empty() || !result.duplicateKeyWarnings.empty()) {
        fs::create_directories(base, ec);
        std::ofstream os(base / "_missing.diff.yaml", std::ios::binary);
        if (os) {
            os << "artifact: " << artifactRel << "\n";
            os << "only_in_A:\n";
            for (const auto& k : result.onlyInA) os << "  - " << k << "\n";
            os << "only_in_B:\n";
            for (const auto& k : result.onlyInB) os << "  - " << k << "\n";
            if (!result.duplicateKeyWarnings.empty()) {
                os << "duplicate_key_warnings:\n";
                for (const auto& w : result.duplicateKeyWarnings) os << "  - " << w << "\n";
            }
        }
    }
}

void WriteSummary(const std::filesystem::path& deltaDir, const std::vector<SummaryEntry>& entries,
                  bool anyDifference) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(deltaDir, ec);
    std::ofstream os(deltaDir / "summary.yaml", std::ios::binary);
    if (!os) return;
    os << "any_difference: " << (anyDifference ? "true" : "false") << "\n";
    os << "artifacts:\n";
    for (const auto& e : entries) {
        os << "  - artifact: " << e.artifact << "\n";
        os << "    domain: " << e.domain << "\n";
        os << "    compared: " << e.compared << "\n";
        os << "    only_in_A: " << e.onlyA << "\n";
        os << "    only_in_B: " << e.onlyB << "\n";
        os << "    differing: " << e.differing << "\n";
    }
}

} // namespace havok::diff
