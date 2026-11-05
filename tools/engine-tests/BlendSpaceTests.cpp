/// @file
/// @brief The 2D blend space's weights, and the schema shape the plugin's class relies on.

#include "TestSupport.h"

#include <decompile/BehaviorDecompile.h>
#include <locomotion/BlendSpace.h>

#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace CB::engine::tests {

namespace {

namespace loco = locomotion;

std::vector<float> Weights(const std::vector<loco::BlendSample>& samples, float x, float y) {
    std::vector<float> out(samples.size(), -1.0f);
    loco::GradientBandWeights(samples, x, y, out);
    return out;
}

bool Near(float a, float b) { return std::abs(a - b) < 1e-5f; }

float Sum(const std::vector<float>& w) {
    float s = 0.0f;
    for (const float v : w) s += v;
    return s;
}

void WeightChecks() {
    const std::vector<loco::BlendSample> grid{ { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 }, { 0, 0 } };

    for (std::size_t i = 0; i < grid.size(); ++i) {
        const std::vector<float> w = Weights(grid, grid[i].x, grid[i].y);
        for (std::size_t j = 0; j < grid.size(); ++j) Check(Near(w[j], i == j ? 1.0f : 0.0f), "on a sample, that sample has all the weight");
    }
    for (const auto& [x, y] : std::array<std::pair<float, float>, 6>{ { { 0.3f, 0.1f }, { -0.9f, 0.5f }, { 0.0f, 0.99f }, { 5.0f, 5.0f }, { -40.0f, 2.0f }, { 0.0f, -3.0f } } }) {
        const std::vector<float> w = Weights(grid, x, y);
        Check(Near(Sum(w), 1.0f), "the weights sum to 1, inside the samples and outside them");
        for (const float v : w) Check(v >= 0.0f && v <= 1.0f, "a weight stays in [0, 1], which the blender clamps to");
    }

    const std::vector<float> pair = Weights({ { 0, 0 }, { 2, 0 } }, 0.5f, 7.0f);
    Check(Near(pair[0], 0.75f) && Near(pair[1], 0.25f), "two samples blend along their line, whatever the distance off it");

    const std::vector<float> mid = Weights(grid, 0.5f, 0.0f);
    Check(mid[0] < 1e-5f && mid[2] < 1e-5f, "a sample far behind the point drops out, so its child can go inactive");

    const std::vector<float> one = Weights({ { 3, 3 } }, -8.0f, 1.0f);
    Check(Near(one[0], 1.0f), "one sample always has all the weight");

    const std::vector<float> same = Weights({ { 1, 1 }, { 1, 1 } }, 4.0f, 4.0f);
    Check(Near(same[0], 0.5f) && Near(same[1], 0.5f), "samples at one place share the weight");

    Weights({}, 0.0f, 0.0f);
}

/// @brief What vanilla's blender of blenders gives, from computeBlendInterval's own rules, against the grid in one call.
void GridChecks() {
    // MT_Direction_Blend's first rows: forward at 0, forward right at .125 with five speeds, right at .25 with four.
    const std::vector<loco::BlendSample> mt{
        { 0.25f, 8.0f },   { 0.0f, 80.0f },   { 0.125f, 8.0f },   { 0.875f, 10.0f },  { 0.0f, 8.0f },    { 0.125f, 80.0f },
        { 0.0f, 140.0f },  { 0.125f, 140.0f }, { 0.25f, 80.0f },  { 0.25f, 350.0f },  { 0.875f, 100.0f }, { 0.0f, 350.0f },
    };
    const auto weights = [&](float x, float y) {
        std::vector<float> out(mt.size(), -1.0f);
        loco::GridWeights(mt, x, y, true, 0.0f, 1.0f, out);
        return out;
    };
    const auto at = [&](const std::vector<float>& w, float x, float y) {
        for (std::size_t i = 0; i < mt.size(); ++i)
            if (mt[i].x == x && mt[i].y == y) return w[i];
        Check(false, "no such sample");
        return 0.0f;
    };

    const std::vector<float> between = weights(0.0625f, 44.0f);
    Check(Near(at(between, 0.0f, 8.0f), 0.25f) && Near(at(between, 0.0f, 80.0f), 0.25f), "halfway between rows and speeds, the first row's pair");
    Check(Near(at(between, 0.125f, 8.0f), 0.25f) && Near(at(between, 0.125f, 80.0f), 0.25f), "and the second row's");
    Check(Near(Sum(between), 1.0f), "the grid's weights sum to 1");

    const std::vector<float> uneven = weights(0.1875f, 110.0f);
    Check(Near(at(uneven, 0.125f, 80.0f), 0.25f) && Near(at(uneven, 0.125f, 140.0f), 0.25f), "a row of three blends over its own speeds");
    Check(Near(at(uneven, 0.25f, 80.0f), 0.5f * (1.0f - 30.0f / 270.0f)), "and a row without 140 over its own");

    const std::vector<float> wrap = weights(0.95f, 50.0f);
    const float past = 0.075f / 0.125f;
    Check(Near(at(wrap, 0.0f, 8.0f) + at(wrap, 0.0f, 80.0f), past), "past the last row the blend wraps to the first");
    Check(Near(at(wrap, 0.875f, 10.0f) + at(wrap, 0.875f, 100.0f), 1.0f - past), "and leaves the last");

    const std::vector<float> end = weights(1.0f, 8.0f);
    Check(Near(at(end, 0.0f, 8.0f), 1.0f), "the cycle's end reads as its start, as vanilla's does");
    const std::vector<float> below = weights(-0.5f, 8.0f);
    Check(Near(at(below, 0.0f, 8.0f), 1.0f), "below the cycle reads as its start, not wrapped");

    const std::vector<float> slow = weights(0.0f, 1.0f);
    Check(Near(at(slow, 0.0f, 8.0f), 1.0f), "below a row's speeds clamps to its first");
    const std::vector<float> fast = weights(0.0f, 9000.0f);
    Check(Near(at(fast, 0.0f, 350.0f), 1.0f), "above them to its last");

    std::vector<float> open(mt.size(), -1.0f);
    loco::GridWeights(mt, 2.0f, 8.0f, false, 0.0f, 0.0f, open);
    Check(Near(at(open, 0.875f, 10.0f), 1.0f), "a grid that doesn't wrap clamps to its last row");

    std::vector<float> one(1, -1.0f);
    const std::vector<loco::BlendSample> single{ { 3.0f, 3.0f } };
    loco::GridWeights(single, 0.0f, 0.0f, false, 0.0f, 0.0f, one);
    Check(Near(one[0], 1.0f), "one sample always has all the weight");
    loco::GridWeights({}, 0.0f, 0.0f, true, 0.0f, 1.0f, {});
}

void SchemaChecks(const schema::SchemaRegistry& reg) {
    std::string err;
    Check(reg.ComputeSize("CBBlendSpace2DGenerator", &err) == 176, "the class is the blender, two floats and the grid flag: " + err);
    Check(reg.ComputeSize("hkbBlenderGeneratorChild", &err) == 80, "x and y sit in the child's padding, its size unchanged: " + err);

    const schema::ClassSchema* cls = reg.Find("CBBlendSpace2DGenerator");
    Check(cls && cls->signature != 0, "the class has a derived signature");

    for (const auto& [name, c] : reg.All())
        if (name.rfind("CB", 0) == 0 && (reg.Derives(name, "hkbGenerator") || reg.Derives(name, "hkbModifier")))
            Check(decompile::IsNodeClass(name), name + " is a generator or modifier but no node class: a new one never reaches a canvas");

    const schema::ClassSchema* child = reg.Find("hkbBlenderGeneratorChild");
    Check(child && child->vanillaSignature == 0xe2b384b0u, "without x and y the child hashes to the exe's own signature");
    Check(child && child->signature != child->vanillaSignature, "with them it hashes to the one CB writes");

    const std::vector<int> offsets = reg.OwnFieldOffsets("hkbBlenderGeneratorChild", &err);
    Check(child && offsets.size() == child->fields.size(), "the child's offsets: " + err);
    for (std::size_t i = 0; child && i < offsets.size() && i < child->fields.size(); ++i) {
        if (child->fields[i].name == "x") Check(offsets[i] == 0x48, "x sits at 0x48");
        if (child->fields[i].name == "y") Check(offsets[i] == 0x4c, "y sits at 0x4c");
    }
}

}  // namespace

void RunBlendSpaceTests(const schema::SchemaRegistry& reg) {
    WeightChecks();
    GridChecks();
    SchemaChecks(reg);
}

}  // namespace CB::engine::tests
