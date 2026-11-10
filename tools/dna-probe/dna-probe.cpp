// dna-probe: prints what a MetaHuman DNA file holds and times RigLogic on it, per LOD and calculation type. Single-threaded.
#include <riglogic/RigLogic.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
using Clock = std::chrono::steady_clock;
static double run(rl4::RigLogic* rig, rl4::RigInstance* inst, int active, int frames) {
    const int n = inst->getRawControlCount();
    auto t0 = Clock::now();
    for (int f = 0; f < frames; ++f) {
        for (int i = 0; i < n; ++i) inst->setRawControl(std::uint16_t(i), 0.0f);
        for (int a = 0; a < active && a < n; ++a) {
            int i = (a * 7919 + f / 8 * 31) % n;
            inst->setRawControl(std::uint16_t(i), float(std::fabs(std::sin((f + a) * 0.05))));
        }
        rig->calculate(inst);
    }
    return std::chrono::duration<double, std::micro>(Clock::now() - t0).count() / frames;
}
int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: dna-probe <file.dna>\n"); return 2; }
    auto stream = rl4::makeScoped<rl4::FileStream>(argv[1], rl4::FileStream::AccessMode::Read, rl4::FileStream::OpenMode::Binary);
    auto reader = rl4::makeScoped<rl4::BinaryStreamReader>(stream.get());
    auto t0 = Clock::now();
    reader->read();
    if (!rl4::Status::isOk()) { std::printf("error: %s\n", rl4::Status::get().message); return 1; }
    printf("read %.0f ms\n", std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
    auto s = [](rl4::StringView v) { return std::string(v.data(), v.size()); };
    printf("name %s lods %d maxlod %d raw %d gui %d psd %d joints %d channels %d maps %d meshes %d\n", s(reader->getName()).c_str(), reader->getLODCount(), reader->getDBMaxLOD(), reader->getRawControlCount(), reader->getGUIControlCount(), reader->getPSDCount(), reader->getJointCount(), reader->getBlendShapeChannelCount(), reader->getAnimatedMapCount(), reader->getMeshCount());
    printf("db %s / %s / complexity %s, gender-age %d %d\n", s(reader->getDBName()).c_str(), s(reader->getDBComplexity()).c_str(), "", int(reader->getGender()), reader->getAge());
    long long deltas = 0;
    for (int m = 0; m < reader->getMeshCount(); ++m) {
        long long d = 0; int t = reader->getBlendShapeTargetCount(std::uint16_t(m));
        for (int k = 0; k < t; ++k) d += reader->getBlendShapeTargetDeltaCount(std::uint16_t(m), std::uint16_t(k));
        deltas += d;
        printf(" mesh %2d %-32s verts %6u faces %6u layouts %6u maxinf %2d targets %4d deltas %8lld\n", m, s(reader->getMeshName(std::uint16_t(m))).c_str(), reader->getVertexPositionCount(std::uint16_t(m)), reader->getFaceCount(std::uint16_t(m)), reader->getVertexLayoutCount(std::uint16_t(m)), reader->getMaximumInfluencePerVertex(std::uint16_t(m)), t, d);
    }
    printf("total deltas %lld\n", deltas);
    for (int l = 0; l < reader->getLODCount(); ++l) {
        auto ms = reader->getMeshIndicesForLOD(std::uint16_t(l));
        long long v = 0; for (auto m : ms) v += reader->getVertexPositionCount(m);
        printf(" lod %d meshes %d verts %lld\n", l, int(ms.size()), v);
    }
    for (int i = 0; i < 12 && i < reader->getRawControlCount(); ++i) printf(" raw %s\n", s(reader->getRawControlName(std::uint16_t(i))).c_str());
    for (int i = 0; i < 12 && i < reader->getGUIControlCount(); ++i) printf(" gui %s\n", s(reader->getGUIControlName(std::uint16_t(i))).c_str());
    for (auto ct : {rl4::CalculationType::Scalar, rl4::CalculationType::SSE, rl4::CalculationType::AVX}) {
        rl4::Configuration cfg; cfg.calculationType = ct;
        auto t1 = Clock::now();
        auto rig = rl4::makeScoped<rl4::RigLogic>(reader.get(), cfg);
        double build = std::chrono::duration<double, std::milli>(Clock::now() - t1).count();
        auto inst = rl4::makeScoped<rl4::RigInstance>(rig.get());
        printf("calc %d build %.0f ms\n", int(ct), build);
        for (int l = 0; l < rig->getLODCount(); ++l) {
            inst->setLOD(std::uint16_t(l));
            printf("  lod %d: 20 active %.1f us, 80 active %.1f us, all %.1f us; joint outputs %d channels %d\n", l, run(rig.get(), inst.get(), 20, 2000), run(rig.get(), inst.get(), 80, 2000), run(rig.get(), inst.get(), 100000, 500), int(inst->getJointOutputs().size()), int(inst->getBlendShapeOutputs().size()));
        }
    }
}
