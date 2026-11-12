// Dependency-free checks for the LFO maths. Exits non-zero on the first failure.
#include "LfoEngine.h"

#include <cstdio>
#include <cstdlib>

#define CHECK(cond) \
    do { if (! (cond)) { std::printf ("FAILED line %d: %s\n", __LINE__, #cond); std::exit (1); } } while (false)

static bool close (float a, float b, float tolerance = 1.0e-3f) { return std::abs (a - b) <= tolerance; }

int main()
{
    using namespace lfo;

    ShapeData data;
    for (int i = 0; i < kMaxSteps; ++i)
        data.steps[(size_t) i] = (float) i / (float) (kMaxSteps - 1);

    std::vector<Node> nodes { { 0.5f, 1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, { 1.0f, -1.0f, 0.0f } };
    sanitiseNodes (nodes);
    renderNodes (nodes, data);

    // Every shape stays inside -1..1, for any phase (including negative) and any skew.
    for (int s = 0; s < (int) Shape::count; ++s)
        for (float skew : { -1.0f, 0.0f, 0.6f })
            for (double phase = -3.0; phase < 40.0; phase += 0.00731)
            {
                ShapeParams sp { (Shape) s, 0.3f, skew, 7 };
                const float v = evaluate (sp, phase, data);
                CHECK (std::isfinite (v) && v >= -1.0001f && v <= 1.0001f);
            }

    ShapeParams sp;
    sp.shape = Shape::sine;
    CHECK (close (evaluate (sp, 0.0, data), 0.0f) && close (evaluate (sp, 0.25, data), 1.0f) && close (evaluate (sp, 7.75, data), -1.0f));

    sp.shape = Shape::triangle;
    CHECK (close (evaluate (sp, 0.0, data), 0.0f) && close (evaluate (sp, 0.25, data), 1.0f) && close (evaluate (sp, 0.75, data), -1.0f));

    sp.shape = Shape::sawUp;
    CHECK (close (evaluate (sp, 0.0, data), -1.0f) && close (evaluate (sp, 0.5, data), 0.0f));

    sp.shape = Shape::sawDown;
    CHECK (close (evaluate (sp, 0.0, data), 1.0f) && close (evaluate (sp, 0.75, data), -0.5f));

    sp.shape = Shape::square;
    sp.pulseWidth = 0.25f;
    CHECK (evaluate (sp, 0.2, data) == 1.0f && evaluate (sp, 0.3, data) == -1.0f);

    sp.shape = Shape::expUp;
    CHECK (close (evaluate (sp, 0.0, data), -1.0f) && evaluate (sp, 0.5, data) < 0.0f);
    sp.shape = Shape::expDown;
    CHECK (close (evaluate (sp, 0.0, data), 1.0f) && evaluate (sp, 0.5, data) < 0.0f);

    // Random shapes: repeatable, constant within a cycle (S&H), continuous across cycles (smooth).
    sp.shape = Shape::sampleHold;
    CHECK (evaluate (sp, 5.1, data) == evaluate (sp, 5.9, data));
    CHECK (evaluate (sp, 5.1, data) != evaluate (sp, 6.1, data));
    sp.shape = Shape::smoothRandom;
    CHECK (close (evaluate (sp, 5.999999, data), evaluate (sp, 6.0, data), 1.0e-2f));

    // Steps: 4 steps use the first four levels.
    sp.shape = Shape::steps;
    sp.numSteps = 4;
    CHECK (close (evaluate (sp, 0.1, data), -1.0f));
    CHECK (close (evaluate (sp, 0.99, data), 2.0f * (3.0f / 15.0f) - 1.0f));

    // Custom: the unsorted nodes above describe a triangle peaking in the middle.
    sp.shape = Shape::custom;
    CHECK (close (evaluate (sp, 0.0, data), -1.0f) && close (evaluate (sp, 0.5, data), 1.0f, 1.0e-2f) && close (evaluate (sp, 0.25, data), 0.0f, 1.0e-2f));

    // Bent segments still hit their end points.
    CHECK (close (bendSegment (0.0f, 0.7f), 0.0f) && close (bendSegment (1.0f, 0.7f), 1.0f) && close (bendSegment (1.0f, -0.7f), 1.0f));
    CHECK (bendSegment (0.5f, 0.7f) < 0.5f && bendSegment (0.5f, -0.7f) > 0.5f);

    // Broken node lists are repaired.
    std::vector<Node> empty;
    sanitiseNodes (empty);
    CHECK (empty.size() == 2 && empty.front().x == 0.0f && empty.back().x == 1.0f);

    // Volume mapping never boosts and never inverts.
    for (float amount = -1.0f; amount <= 1.0f; amount += 0.25f)
        for (float u = 0.0f; u <= 1.0f; u += 0.1f)
            CHECK (volumeGain (u, amount) >= -1.0e-6f && volumeGain (u, amount) <= 1.0f + 1.0e-6f);
    CHECK (close (volumeGain (0.0f, 1.0f), 0.0f) && close (volumeGain (1.0f, 1.0f), 1.0f) && close (volumeGain (1.0f, -1.0f), 0.0f));

    // The filter stays stable while its cutoff is swept every sample.
    StateVariableFilter filter;
    float peak = 0.0f;
    for (int i = 0; i < 48000; ++i)
    {
        const float fc = 20.0f + 19000.0f * (0.5f + 0.5f * std::sin ((float) i * 0.01f));
        const float g = std::tan (3.14159265f * fc / 48000.0f);
        peak = std::max (peak, std::abs (filter.process ((i % 100) < 50 ? 1.0f : -1.0f, 1 + (i % 3), g, 1.0f / 12.0f)));
    }
    CHECK (std::isfinite (peak) && peak < 100.0f);

    std::printf ("All LFO engine tests passed\n");
    return 0;
}
