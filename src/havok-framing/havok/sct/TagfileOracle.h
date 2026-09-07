#pragma once
#include "havok/core/PackFileDeserializer.h"

#include <cctype>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// The structural-alignment oracle (spec §4.4): recover the tagfile #NNNN for every
// binary object WITHOUT reproducing hkdump's traversal, by co-DFS-ing the binary
// reference graph (refs in Read/field order) against the tagfile's reference graph
// (refs in document order) from their shared root. This is the single source of
// truth used by both `havok-core-cli idalign` (grading) and the patch converter.
//
// Precondition: `des` has been fully constructed from its root
// (ConstructVirtualClass(root)), so RefsInReadOrder()/DeserializedObjects() are
// populated. `xmlText` is the matching vanilla tagfile XML.

namespace havok::sct {

struct OracleResult {
    std::unordered_map<std::uint32_t, std::uint32_t> off2id;  // binary offset -> #NNNN
    std::size_t binObjs = 0, xmlObjs = 0, mapped = 0;
    int classMism = 0, refCountMism = 0, conflict = 0;
    bool ok = false;
    std::string error;
    struct Mismatch { std::uint32_t id = 0; std::string binClass, xmlClass; std::size_t binN = 0, xmlN = 0; };
    std::vector<Mismatch> classMismatches;  // first few (bo class != xn class)
    std::vector<Mismatch> refMismatches;    // first few (ref-count differs)
};

inline OracleResult AlignTagfile(PackFileDeserializer& des, const std::string& xmlText) {
    OracleResult res;

    // Binary side: offset -> class, the root, and per-object refs in Read order.
    std::unordered_map<std::uint32_t, std::string> binClass;
    std::uint32_t binRoot = 0xFFFFFFFFu;
    for (const auto& [o, c] : des.ListObjects()) {
        binClass[o] = c;
        if (c == "hkRootLevelContainer") binRoot = o;
    }
    if (binRoot == 0xFFFFFFFFu) { res.error = "no root in binary"; return res; }
    const auto& binRefs = des.RefsInReadOrder();
    res.binObjs = binClass.size();

    // XML side: #N -> class, #N -> [#M refs in document order], and the root #N.
    // Proven string scan (kept byte-identical to the graded idalign path): a data
    // object's block runs from its opening tag to the next `<hkobject name="#`, and
    // every '#'-prefixed integer inside it is an outgoing ref, in document order.
    std::unordered_map<std::uint32_t, std::string> xmlClass;
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> xmlRefs;
    std::uint32_t xmlRoot = 0xFFFFFFFFu;
    {
        const std::string& all = xmlText;
        const std::string tag = "<hkobject name=\"#";
        std::size_t pos = all.find(tag);
        while (pos != std::string::npos) {
            const std::size_t ns = pos + tag.size();
            const std::size_t ne = all.find('"', ns);
            const std::uint32_t id = static_cast<std::uint32_t>(std::stoul(all.substr(ns, ne - ns)));
            const std::size_t cp = all.find("class=\"", ne), cs = cp + 7, ce = all.find('"', cs);
            xmlClass[id] = all.substr(cs, ce - cs);
            if (xmlClass[id] == "hkRootLevelContainer") xmlRoot = id;
            const std::size_t next = all.find(tag, ne);
            const std::size_t bodyStart = all.find('>', ce);
            const std::size_t blockEnd = (next == std::string::npos) ? all.size() : next;
            std::vector<std::uint32_t>& refs = xmlRefs[id];
            for (std::size_t q = all.find('#', bodyStart); q != std::string::npos && q < blockEnd;
                 q = all.find('#', q + 1)) {
                // Skip HTML numeric entities like SyncAnimPrefix's "&#9216;" — the '#'
                // there is not an object reference (this is the BSSynchronizedClipGenerator
                // "xml=2 vs bin=1" false mismatch).
                if (q > 0 && all[q - 1] == '&') continue;
                std::size_t d = q + 1, e2 = d;
                while (e2 < all.size() && std::isdigit(static_cast<unsigned char>(all[e2]))) ++e2;
                if (e2 > d) refs.push_back(static_cast<std::uint32_t>(std::stoul(all.substr(d, e2 - d))));
                q = e2 - 1;
            }
            pos = next;
        }
    }
    if (xmlRoot == 0xFFFFFFFFu) { res.error = "no root in XML"; return res; }
    res.xmlObjs = xmlClass.size();

    // Co-DFS from both roots in lockstep, matching refs positionally.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> stack{ { binRoot, xmlRoot } };
    while (!stack.empty()) {
        const auto [bo, xn] = stack.back(); stack.pop_back();
        auto ins = res.off2id.emplace(bo, xn);
        if (!ins.second) { if (ins.first->second != xn) ++res.conflict; continue; }
        const auto bit = binRefs.find(bo);
        static const std::vector<std::uint32_t> empty;
        const std::vector<std::uint32_t>& b2 = (bit != binRefs.end()) ? bit->second : empty;
        const std::vector<std::uint32_t>& x2 = xmlRefs[xn];
        if (binClass[bo] != xmlClass[xn]) {
            ++res.classMism;
            if (res.classMismatches.size() < 12)
                res.classMismatches.push_back({ xn, binClass[bo], xmlClass[xn], b2.size(), x2.size() });
        }
        if (b2.size() != x2.size()) {
            ++res.refCountMism;
            if (res.refMismatches.size() < 12)
                res.refMismatches.push_back({ xn, binClass[bo], xmlClass[xn], b2.size(), x2.size() });
        }
        const std::size_t n = std::min(b2.size(), x2.size());
        for (std::size_t i = 0; i < n; ++i) stack.push_back({ b2[i], x2[i] });
    }
    res.mapped = res.off2id.size();
    res.ok = true;
    return res;
}

}  // namespace havok::sct
