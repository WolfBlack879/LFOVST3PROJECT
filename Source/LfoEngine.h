// Pure LFO maths. No JUCE dependency so it can be unit-tested on its own.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace lfo
{
enum class Shape
{
    sine = 0,
    triangle,
    sawUp,
    sawDown,
    square,
    expUp,
    expDown,
    sampleHold,
    smoothRandom,
    steps,
    custom,
    count
};

constexpr int kMaxSteps = 16;
constexpr int kTableSize = 1024;
constexpr double kTwoPi = 6.283185307179586476925286766559;

/** A breakpoint of the drawable shape. x is 0..1, y is -1..1.
    curve (-1..1) bends the segment that starts at this node. */
struct Node
{
    float x = 0.0f, y = 0.0f, curve = 0.0f;
};

/** Shape data shared between the UI thread (writer) and the audio thread (reader).
    Every value is an atomic float, so reads never block and never tear. */
struct ShapeData
{
    std::array<std::atomic<float>, kTableSize + 1> table {}; // custom shape, -1..1
    std::array<std::atomic<float>, kMaxSteps> steps {};      // step levels, 0..1
};

struct ShapeParams
{
    Shape shape = Shape::sine;
    float pulseWidth = 0.5f; // square only
    float skew = 0.0f;       // -1..1, warps time inside the cycle
    int numSteps = kMaxSteps;
};

/** Deterministic random value in -1..1 for a given cycle number. */
inline float randomForCycle (std::int64_t n) noexcept
{
    auto h = static_cast<std::uint64_t> (n) + 0x9E3779B97F4A7C15ull;
    h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ull;
    h = (h ^ (h >> 27)) * 0x94D049BB133111EBull;
    h ^= (h >> 31);
    return static_cast<float> (static_cast<double> (h >> 11) / 9007199254740992.0 * 2.0 - 1.0);
}

inline float bendSegment (float t, float curve) noexcept
{
    if (std::abs (curve) < 1.0e-3f)
        return t;

    const float k = curve * 8.0f;
    return (std::exp (k * t) - 1.0f) / (std::exp (k) - 1.0f);
}

/** Evaluates the LFO at an absolute phase measured in cycles. Returns -1..1. */
inline float evaluate (const ShapeParams& sp, double phase, const ShapeData& data) noexcept
{
    const double cycle = std::floor (phase);
    double p = phase - cycle;

    if (std::abs (sp.skew) > 1.0e-6f)
        p = std::pow (p, std::exp2 (-2.0 * static_cast<double> (sp.skew)));

    p = std::clamp (p, 0.0, 0.999999999);
    const auto pf = static_cast<float> (p);

    switch (sp.shape)
    {
        case Shape::sine:      return static_cast<float> (std::sin (kTwoPi * p));
        case Shape::triangle:
        {
            double q = p + 0.25;
            q -= std::floor (q);
            return static_cast<float> (1.0 - 4.0 * std::abs (q - 0.5));
        }
        case Shape::sawUp:     return 2.0f * pf - 1.0f;
        case Shape::sawDown:   return 1.0f - 2.0f * pf;
        case Shape::square:    return pf < sp.pulseWidth ? 1.0f : -1.0f;
        case Shape::expUp:     return 2.0f * pf * pf * pf - 1.0f;
        case Shape::expDown:
        {
            const float q = 1.0f - pf;
            return 2.0f * q * q * q - 1.0f;
        }
        case Shape::sampleHold:
            return randomForCycle (static_cast<std::int64_t> (cycle));
        case Shape::smoothRandom:
        {
            const auto n = static_cast<std::int64_t> (cycle);
            const float a = randomForCycle (n);
            const float b = randomForCycle (n + 1);
            const float t = pf * pf * (3.0f - 2.0f * pf);
            return a + (b - a) * t;
        }
        case Shape::steps:
        {
            const int n = std::clamp (sp.numSteps, 1, kMaxSteps);
            const int i = std::min (n - 1, static_cast<int> (p * n));
            return 2.0f * data.steps[static_cast<size_t> (i)].load (std::memory_order_relaxed) - 1.0f;
        }
        case Shape::custom:
        {
            const float pos = pf * static_cast<float> (kTableSize);
            const int i = std::min (kTableSize - 1, static_cast<int> (pos));
            const float frac = pos - static_cast<float> (i);
            const float a = data.table[static_cast<size_t> (i)].load (std::memory_order_relaxed);
            const float b = data.table[static_cast<size_t> (i + 1)].load (std::memory_order_relaxed);
            return a + (b - a) * frac;
        }
        case Shape::count: break;
    }

    return 0.0f;
}

/** Value of a breakpoint curve at x (0..1). Nodes must be sorted by x. */
inline float evaluateNodes (const std::vector<Node>& nodes, float x) noexcept
{
    if (nodes.empty())
        return 0.0f;

    if (nodes.size() == 1 || x <= nodes.front().x)
        return nodes.front().y;

    if (x >= nodes.back().x)
        return nodes.back().y;

    size_t i = 0;
    while (i + 2 < nodes.size() && nodes[i + 1].x <= x)
        ++i;

    const auto& a = nodes[i];
    const auto& b = nodes[i + 1];
    const float width = b.x - a.x;

    if (width <= 1.0e-6f)
        return b.y;

    return a.y + (b.y - a.y) * bendSegment ((x - a.x) / width, a.curve);
}

/** Makes a node list safe to use: sorted, clamped, spanning 0..1. */
inline void sanitiseNodes (std::vector<Node>& nodes)
{
    for (auto& n : nodes)
    {
        n.x = std::clamp (n.x, 0.0f, 1.0f);
        n.y = std::clamp (n.y, -1.0f, 1.0f);
        n.curve = std::clamp (n.curve, -1.0f, 1.0f);
    }

    std::stable_sort (nodes.begin(), nodes.end(), [] (const Node& a, const Node& b) { return a.x < b.x; });

    if (nodes.size() < 2)
        nodes = { { 0.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } };

    nodes.front().x = 0.0f;
    nodes.back().x = 1.0f;
}

inline void renderNodes (const std::vector<Node>& nodes, ShapeData& data) noexcept
{
    for (int i = 0; i <= kTableSize; ++i)
        data.table[static_cast<size_t> (i)].store (evaluateNodes (nodes, static_cast<float> (i) / static_cast<float> (kTableSize)),
                                                   std::memory_order_relaxed);
}

/** Zavalishin TPT state variable filter, safe to modulate every sample. */
struct StateVariableFilter
{
    enum Type { lowPass = 1, highPass, bandPass };

    void reset() noexcept { ic1 = ic2 = 0.0f; }

    float process (float x, int type, float g, float k) noexcept
    {
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float a3 = g * a2;
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;

        switch (type)
        {
            case lowPass:  return v2;
            case highPass: return x - k * v1 - v2;
            case bandPass: return v1;
            default:       return x;
        }
    }

    float ic1 = 0.0f, ic2 = 0.0f;
};

/** Volume gain for a unipolar LFO value u (0..1) and amount (-1..1).
    Positive amounts duck when the LFO is low; negative amounts duck when it is high. */
inline float volumeGain (float u, float amount) noexcept
{
    return amount >= 0.0f ? 1.0f - amount * (1.0f - u) : 1.0f + amount * u;
}
} // namespace lfo
