// End-to-end checks that run the real audio processor without a host.
#include "PluginProcessor.h"

#include <cstdio>
#include <cstdlib>

#define CHECK(cond) \
    do { if (! (cond)) { std::printf ("FAILED line %d: %s\n", __LINE__, #cond); std::exit (1); } } while (false)

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 256;

void set (LfoAudioProcessor& p, const char* id, float value)
{
    auto* param = p.apvts.getParameter (id);
    CHECK (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

struct Result
{
    float minL = 1.0e9f, maxL = -1.0e9f, minR = 1.0e9f, maxR = -1.0e9f, maxDiffFromInput = 0.0f;
    bool finite = true;
    std::vector<int> ccValues;
};

/** Runs the processor over a constant (or noise) stereo input. */
Result run (LfoAudioProcessor& p, double seconds, bool noise = false, int noteOnAtSample = -1)
{
    Result r;
    juce::Random random (42);
    juce::AudioBuffer<float> buffer (2, kBlock);
    int done = 0;

    for (const int total = (int) (seconds * kSampleRate); done < total; done += kBlock)
    {
        juce::AudioBuffer<float> input (2, kBlock);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kBlock; ++i)
                input.setSample (ch, i, noise ? random.nextFloat() * 2.0f - 1.0f : 1.0f);

        buffer.makeCopyOf (input);
        juce::MidiBuffer midi;

        if (noteOnAtSample >= done && noteOnAtSample < done + kBlock)
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), noteOnAtSample - done);

        p.processBlock (buffer, midi);

        for (int i = 0; i < kBlock; ++i)
        {
            const float l = buffer.getSample (0, i), right = buffer.getSample (1, i);
            r.finite = r.finite && std::isfinite (l) && std::isfinite (right);
            r.minL = juce::jmin (r.minL, l);   r.maxL = juce::jmax (r.maxL, l);
            r.minR = juce::jmin (r.minR, right); r.maxR = juce::jmax (r.maxR, right);
            r.maxDiffFromInput = juce::jmax (r.maxDiffFromInput, std::abs (l - input.getSample (0, i)));
        }

        for (const auto meta : midi)
            if (meta.getMessage().isController())
                r.ccValues.push_back (meta.getMessage().getControllerValue());
    }

    return r;
}

std::unique_ptr<LfoAudioProcessor> makeProcessor()
{
    auto p = std::make_unique<LfoAudioProcessor>();
    p->setRateAndBufferSizeDetails (kSampleRate, kBlock);
    p->prepareToPlay (kSampleRate, kBlock);
    return p;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    {   // Tremolo: a 2 Hz sine at full depth sweeps the gain over the whole 0..1 range.
        auto p = makeProcessor();
        set (*p, ids::mode, 0);
        set (*p, ids::sync, 0);
        set (*p, ids::rate, 2.0f);
        const auto r = run (*p, 1.0);
        CHECK (r.finite && r.minL < 0.01f && r.minL > -0.001f && r.maxL > 0.99f && r.maxL < 1.001f);
    }

    {   // Mix at 0 % leaves the signal untouched.
        auto p = makeProcessor();
        set (*p, ids::mix, 0.0f);
        p->prepareToPlay (kSampleRate, kBlock);
        CHECK (run (*p, 0.5, true).maxDiffFromInput < 1.0e-6f);
    }

    {   // Tempo sync without a host falls back to 120 bpm: 1/4 note = 2 Hz.
        auto p = makeProcessor();
        run (*p, 0.25); // half a cycle
        CHECK (std::abs (p->vizPhase.load() - 0.5f) < 0.02f);
    }

    {   // Auto-pan moves the two channels in opposite directions.
        auto p = makeProcessor();
        set (*p, ids::volAmt, 0.0f);
        set (*p, ids::panAmt, 100.0f);
        const auto r = run (*p, 1.0);
        CHECK (r.minL < 0.01f && r.minR < 0.01f && r.maxL > 0.99f && r.maxR > 0.99f);
    }

    {   // Stereo phase offset makes left and right differ.
        auto p = makeProcessor();
        set (*p, ids::stereo, 180.0f);
        juce::AudioBuffer<float> buffer (2, kBlock);
        juce::MidiBuffer midi;
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), 1.0f, kBlock);
        for (int block = 0; block < 12; ++block) // lands an eighth of a second in: a quarter cycle
            p->processBlock (buffer, midi), juce::FloatVectorOperations::fill (buffer.getWritePointer (0), 1.0f, kBlock),
                juce::FloatVectorOperations::fill (buffer.getWritePointer (1), 1.0f, kBlock);
        p->processBlock (buffer, midi);
        CHECK (buffer.getSample (0, 0) > 0.8f && buffer.getSample (1, 0) < 0.2f);
    }

    {   // A MIDI note restarts the cycle in Retrigger mode.
        auto p = makeProcessor();
        set (*p, ids::mode, 2);
        const int noteAt = 5000, total = kBlock * 40;
        run (*p, total / kSampleRate, false, noteAt);
        const float expected = (float) ((total - noteAt) * 2.0 / kSampleRate);
        CHECK (std::abs (p->vizPhase.load() - (expected - std::floor (expected))) < 0.01f);
    }

    {   // One-Shot plays a single cycle after a note and then holds.
        auto p = makeProcessor();
        set (*p, ids::mode, 3);
        set (*p, ids::shape, 2); // saw up: holds at the top
        run (*p, 2.0, false, 100);
        CHECK (p->vizPhase.load() > 0.999f && p->vizValue.load() > 0.99f);
    }

    {   // MIDI CC output covers the full 0..127 range and never repeats a value back to back.
        auto p = makeProcessor();
        set (*p, ids::ccOn, 1);
        set (*p, ids::ccNum, 74);
        const auto r = run (*p, 1.0);
        CHECK (r.ccValues.size() > 200);
        int lo = 127, hi = 0;
        for (size_t i = 0; i < r.ccValues.size(); ++i)
        {
            lo = juce::jmin (lo, r.ccValues[i]);
            hi = juce::jmax (hi, r.ccValues[i]);
            CHECK (i == 0 || r.ccValues[i] != r.ccValues[i - 1]);
        }
        CHECK (lo == 0 && hi == 127);
    }

    {   // Every factory preset processes noise without blowing up.
        auto p = makeProcessor();
        for (int i = 0; i < p->getNumPrograms(); ++i)
        {
            p->setCurrentProgram (i);
            p->prepareToPlay (kSampleRate, kBlock);
            const auto r = run (*p, 1.0, true, 300);
            CHECK (p->getProgramName (i).isNotEmpty());
            CHECK (r.finite && r.maxL < 12.0f && r.minL > -12.0f && r.maxR < 12.0f && r.minR > -12.0f);
        }
    }

    {   // State survives a save / load round trip, including the drawn shape and the steps.
        auto a = makeProcessor();
        set (*a, ids::shape, 10);
        set (*a, ids::cutoff, 432.0f);
        set (*a, ids::filterType, 2);
        a->setNodes ({ { 0.0f, 0.5f, 0.3f }, { 0.4f, -0.75f, -0.2f }, { 1.0f, 0.25f, 0.0f } });
        a->setStep (3, 0.123f);

        juce::MemoryBlock block;
        a->getStateInformation (block);

        auto b = makeProcessor();
        b->setStateInformation (block.getData(), (int) block.getSize());

        CHECK (b->getShapeParams().shape == lfo::Shape::custom);
        CHECK (std::abs (b->apvts.getRawParameterValue (ids::cutoff)->load() - 432.0f) < 0.5f);
        CHECK (juce::roundToInt (b->apvts.getRawParameterValue (ids::filterType)->load()) == 2);
        CHECK (std::abs (b->getStep (3) - 0.123f) < 1.0e-3f);

        const auto nodes = b->getNodes();
        CHECK (nodes.size() == 3 && std::abs (nodes[1].x - 0.4f) < 1.0e-3f && std::abs (nodes[1].y + 0.75f) < 1.0e-3f
               && std::abs (nodes[0].curve - 0.3f) < 1.0e-3f);

        // Garbage state is ignored rather than crashing.
        const char junk[] = "not a preset";
        b->setStateInformation (junk, (int) sizeof (junk));
        CHECK (b->getNodes().size() == 3);
    }

    std::printf ("All processor tests passed\n");
    return 0;
}
