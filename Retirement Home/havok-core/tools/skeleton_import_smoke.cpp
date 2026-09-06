// ---------------------------------------------------------------------------
// skeleton_import_smoke — reads a real .hkx skeleton and prints what came out.
//
// The unit tests are self-contained by design (they build their own packfiles),
// so nothing in them proves havok-core can read a file Bethesda actually
// shipped. This harness does exactly that: point it at a skeleton.hkx and it
// dumps bone count, hierarchy sanity, and a few reference-pose values.
//
// Opt-in target: configure with -DHAVOK_CORE_BUILD_TOOLS=ON.
//   skeleton-import-smoke <path-to-skeleton.hkx> [...]
// ---------------------------------------------------------------------------
#include "havok/sct/SkeletonImport.h"

#include "havok/core/BinaryReaderEx.h"
#include "havok/core/PackFileDeserializer.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

bool ReadFile(const char* path, std::vector<std::uint8_t>& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const std::streamoff n = f.tellg();
    if (n <= 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<std::size_t>(n));
    f.read(reinterpret_cast<char*>(out.data()), n);
    return static_cast<bool>(f);
}

// Prints header fields, the object table, and a hex window over the first
// hkaSkeleton. Only reached when parsing fails — this is the view that turns a
// mid-stream assertion into a byte-level diagnosis.
void DumpDiagnostics(const std::vector<std::uint8_t>& bytes)
{
    try {
        havok::PackFileDeserializer des;
        havok::BinaryReaderEx br(false, true, bytes);
        des.DeserializePartially(br);

        std::printf("      header: pointerSize=%d endian=%d paddingOption=%d version='%s'\n",
                    des._header.PointerSize, des._header.Endian,
                    des._header.PaddingOption, des._header.ContentsVersionString.c_str());

        const auto objs = des.ListObjects();
        std::printf("      %zu object(s) in __data__:\n", objs.size());
        std::uint32_t skelOff = 0;
        bool haveSkel = false;
        for (const auto& [off, name] : objs) {
            std::printf("        0x%04X  %s\n", off, name.c_str());
            if (!haveSkel && name == "hkaSkeleton") { skelOff = off; haveSkel = true; }
        }

        if (haveSkel) {
            const auto& d = des.DataSectionBytes();
            std::printf("      hex @ hkaSkeleton (0x%04X), 128 bytes:\n", skelOff);
            for (std::size_t row = 0; row < 128; row += 16) {
                const std::size_t base = skelOff + row;
                if (base >= d.size()) break;
                std::printf("        +%03zu 0x%04zX  ", row, base);
                for (std::size_t i = 0; i < 16 && base + i < d.size(); ++i)
                    std::printf("%02X ", d[base + i]);
                std::printf("\n");
            }
        }
    }
    catch (const std::exception& ex) {
        std::printf("      (diagnostics failed: %s)\n", ex.what());
    }
}

int Check(const char* path)
{
    std::vector<std::uint8_t> bytes;
    if (!ReadFile(path, bytes)) {
        std::printf("FAIL  cannot read %s\n", path);
        return 1;
    }

    std::vector<havok::sct::SkeletonData> skeletons;
    std::string err;
    if (!havok::sct::LoadSkeletonsFromHkx(bytes.data(), bytes.size(), skeletons, &err)) {
        std::printf("FAIL  %s\n        %s\n", path, err.c_str());
        DumpDiagnostics(bytes);
        return 1;
    }
    if (skeletons.empty()) {
        std::printf("FAIL  %s — parsed but no hkaSkeleton found\n", path);
        return 1;
    }

    // Record offsets matter: a record that starts 16-aligned hides bugs in any
    // padding rule expressed as an absolute alignment rather than a fixed
    // offset from the record start. Print them so a sweep can prove both cases
    // were actually exercised.
    std::string align;
    try {
        havok::PackFileDeserializer d2;
        havok::BinaryReaderEx b2(false, true, bytes);
        d2.DeserializePartially(b2);
        for (const auto& [off, name] : d2.ListObjects()) {
            if (name != "hkaSkeleton") continue;
            char buf[48];
            std::snprintf(buf, sizeof(buf), "0x%X(%s) ", off, (off % 16) ? "8-al" : "16-al");
            align += buf;
        }
    } catch (...) { align = "<offsets unavailable>"; }

    std::printf("OK    %s  (%zu KB, %zu skeleton(s))  at %s\n",
                path, bytes.size() / 1024, skeletons.size(), align.c_str());

    int problems = 0;
    for (std::size_t s = 0; s < skeletons.size(); ++s) {
        const auto& sk = skeletons[s];
        std::printf("      [%zu] name='%s'  bones=%zu\n",
                    s, sk.name.c_str(), sk.bones.size());

        if (sk.bones.empty()) { std::printf("      !! no bones\n"); ++problems; continue; }

        // Structural sanity: exactly the invariants the editor's FK solve relies
        // on. A parent index that is out of range or forward-referencing means
        // the parallel arrays were misread — the classic symptom of a padding or
        // field-order mistake in the class Read body.
        int roots = 0, badParent = 0, forwardRef = 0, unnamed = 0;
        for (std::size_t i = 0; i < sk.bones.size(); ++i) {
            const int p = sk.bones[i].parentIndex;
            if (p < 0)                                  ++roots;
            else if (p >= static_cast<int>(sk.bones.size())) ++badParent;
            else if (p >= static_cast<int>(i))          ++forwardRef;
            if (sk.bones[i].name.empty())               ++unnamed;
        }

        std::printf("          roots=%d badParent=%d forwardRef=%d unnamed=%d\n",
                    roots, badParent, forwardRef, unnamed);
        // More than one root is legitimate — Skyrim's character skeleton has a
        // second root alongside "NPC Root [Root]". Zero roots is not: it means
        // every bone claims a parent, i.e. a cycle, i.e. a misread.
        if (roots < 1 || badParent || forwardRef || unnamed) {
            std::printf("          !! structure looks wrong\n");
            ++problems;
        }

        const auto& b0 = sk.bones.front();
        std::printf("          root '%s' T=(%.3f %.3f %.3f) R=(%.3f %.3f %.3f %.3f)\n",
                    b0.name.c_str(),
                    b0.refPose.translation.x, b0.refPose.translation.y, b0.refPose.translation.z,
                    b0.refPose.rotation.x, b0.refPose.rotation.y,
                    b0.refPose.rotation.z, b0.refPose.rotation.w);

        // A reference pose of all zeros (including a zero quaternion) means the
        // QSTransform array was read from the wrong offset.
        bool allZero = true;
        for (const auto& b : sk.bones) {
            const auto& q = b.refPose.rotation;
            if (q.x != 0.f || q.y != 0.f || q.z != 0.f || q.w != 0.f) { allZero = false; break; }
        }
        if (allZero) { std::printf("          !! every reference pose is zero\n"); ++problems; }

        if (sk.bones.size() > 1)
            std::printf("          bone[1] '%s' parent=%d\n",
                        sk.bones[1].name.c_str(), sk.bones[1].parentIndex);
    }
    return problems;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::printf("usage: skeleton-import-smoke <skeleton.hkx> [more...]\n");
        return 2;
    }
    int failures = 0;
    for (int i = 1; i < argc; ++i) failures += Check(argv[i]);
    std::printf("\n%s (%d problem(s))\n", failures ? "FAILURES" : "ALL OK", failures);
    return failures ? 1 : 0;
}
