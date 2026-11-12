#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
using APVTS = juce::AudioProcessorValueTreeState;

const double kDivisionBeats[] = { 32.0, 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 0.0625 };
const double kModifierScale[] = { 1.0, 1.5, 2.0 / 3.0 };

std::vector<lfo::Node> pumpNodes()
{
    return { { 0.0f, -1.0f, -0.45f }, { 0.55f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } };
}

const float kGatePattern[lfo::kMaxSteps] = { 1.0f, 0.0f, 0.6f, 0.0f, 1.0f, 1.0f, 0.0f, 0.4f,
                                             1.0f, 0.0f, 0.6f, 1.0f, 0.0f, 1.0f, 0.3f, 0.7f };

struct Preset
{
    const char* name;
    std::vector<std::pair<const char*, float>> values; // overrides on top of the defaults
};

// Choice values are item indices. Anything not listed is reset to its default.
const std::vector<Preset>& presets()
{
    static const std::vector<Preset> list {
        { "Init Tremolo", {} },
        { "Sidechain Pump", { { ids::shape, 10 }, { ids::division, 5 } } },
        { "Auto Pan", { { ids::division, 4 }, { ids::volAmt, 0 }, { ids::panAmt, 100 } } },
        { "Stereo Tremolo", { { ids::shape, 1 }, { ids::division, 6 }, { ids::stereo, 180 }, { ids::volAmt, 80 } } },
        { "Trance Gate", { { ids::shape, 9 }, { ids::division, 3 }, { ids::smooth, 4 } } },
        { "Chopper", { { ids::shape, 4 }, { ids::division, 7 }, { ids::smooth, 3 }, { ids::pw, 0.4f } } },
        { "Filter Wobble", { { ids::division, 6 }, { ids::modifier, 2 }, { ids::volAmt, 0 }, { ids::filterType, 1 },
                             { ids::cutoff, 500 }, { ids::reso, 0.55f }, { ids::filterAmt, 2.5f } } },
        { "Random Filter", { { ids::shape, 7 }, { ids::division, 7 }, { ids::smooth, 12 }, { ids::volAmt, 0 },
                             { ids::filterType, 3 }, { ids::cutoff, 1200 }, { ids::reso, 0.6f }, { ids::filterAmt, 2.0f } } },
        { "Slow Sweep", { { ids::shape, 1 }, { ids::sync, 0 }, { ids::rate, 0.1f }, { ids::mode, 0 }, { ids::volAmt, 0 },
                          { ids::filterType, 1 }, { ids::cutoff, 1500 }, { ids::reso, 0.3f }, { ids::filterAmt, 2.5f } } },
        { "Drift", { { ids::shape, 8 }, { ids::sync, 0 }, { ids::rate, 0.6f }, { ids::mode, 0 }, { ids::volAmt, 35 },
                     { ids::panAmt, 60 }, { ids::stereo, 90 } } },
        { "Note Swell (One-Shot)", { { ids::shape, 5 }, { ids::mode, 3 }, { ids::division, 3 }, { ids::skew, 0.4f } } },
        { "MIDI CC Sine", { { ids::volAmt, 0 }, { ids::ccOn, 1 }, { ids::ccNum, 1 } } },
    };
    return list;
}

auto floatAttr (juce::String suffix, int decimals)
{
    return juce::AudioParameterFloatAttributes().withStringFromValueFunction (
        [suffix, decimals] (float v, int)
        {
            // juce::String (v, 0) means "as many decimals as needed", so round whole numbers ourselves.
            return (decimals == 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, decimals)) + suffix;
        });
}

std::unique_ptr<juce::AudioParameterFloat> floatParam (const char* id, const char* name, juce::NormalisableRange<float> range,
                                                       float def, juce::String suffix, int decimals)
{
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, def, floatAttr (suffix, decimals));
}

std::unique_ptr<juce::AudioParameterChoice> choiceParam (const char* id, const char* name, juce::StringArray items, int def)
{
    return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, items, def);
}

juce::NormalisableRange<float> skewedRange (float lo, float hi, float centre)
{
    juce::NormalisableRange<float> r (lo, hi);
    r.setSkewForCentre (centre);
    return r;
}
} // namespace

//==============================================================================
APVTS::ParameterLayout LfoAudioProcessor::createLayout()
{
    APVTS::ParameterLayout layout;

    layout.add (choiceParam (ids::shape, "Shape",
                             { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Exp Up", "Exp Down",
                               "Sample & Hold", "Smooth Random", "Steps", "Custom" }, 0));
    layout.add (choiceParam (ids::mode, "Trigger Mode", { "Free", "Transport", "Retrigger", "One-Shot" }, 1));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ids::sync, 1 }, "Tempo Sync", true));
    layout.add (floatParam (ids::rate, "Rate", skewedRange (0.01f, 40.0f, 2.0f), 2.0f, " Hz", 2));
    layout.add (choiceParam (ids::division, "Division",
                             { "8 bars", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" }, 5));
    layout.add (choiceParam (ids::modifier, "Division Type", { "Straight", "Dotted", "Triplet" }, 0));

    layout.add (floatParam (ids::phase, "Phase", { 0.0f, 360.0f }, 0.0f, juce::String::fromUTF8 ("\xc2\xb0"), 0));
    layout.add (floatParam (ids::stereo, "Stereo Phase", { -180.0f, 180.0f }, 0.0f, juce::String::fromUTF8 ("\xc2\xb0"), 0));
    layout.add (floatParam (ids::skew, "Skew", { -1.0f, 1.0f }, 0.0f, "", 2));
    layout.add (floatParam (ids::pw, "Pulse Width", { 0.02f, 0.98f }, 0.5f, "", 2));
    layout.add (floatParam (ids::smooth, "Smooth", skewedRange (0.0f, 500.0f, 50.0f), 0.0f, " ms", 1));
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { ids::numSteps, 1 }, "Steps", 2, lfo::kMaxSteps, lfo::kMaxSteps));

    layout.add (floatParam (ids::volAmt, "Volume Amount", { -100.0f, 100.0f }, 100.0f, " %", 0));
    layout.add (floatParam (ids::panAmt, "Pan Amount", { -100.0f, 100.0f }, 0.0f, " %", 0));

    layout.add (choiceParam (ids::filterType, "Filter Type", { "Off", "Low Pass", "High Pass", "Band Pass" }, 0));
    layout.add (floatParam (ids::cutoff, "Filter Cutoff", skewedRange (20.0f, 20000.0f, 1000.0f), 1000.0f, " Hz", 0));
    layout.add (floatParam (ids::reso, "Filter Resonance", { 0.0f, 1.0f }, 0.3f, "", 2));
    layout.add (floatParam (ids::filterAmt, "Filter Amount", { -5.0f, 5.0f }, 2.0f, " oct", 2));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ids::ccOn, 1 }, "MIDI CC Out", false));
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { ids::ccNum, 1 }, "MIDI CC Number", 0, 127, 1));
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { ids::ccChan, 1 }, "MIDI CC Channel", 1, 16, 1));

    layout.add (floatParam (ids::mix, "Mix", { 0.0f, 100.0f }, 100.0f, " %", 0));
    layout.add (floatParam (ids::outGain, "Output Gain", { -24.0f, 12.0f }, 0.0f, " dB", 1));

    return layout;
}

LfoAudioProcessor::LfoAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    setNodes (pumpNodes());

    for (int i = 0; i < lfo::kMaxSteps; ++i)
        setStep (i, kGatePattern[i]);
}

//==============================================================================
std::vector<lfo::Node> LfoAudioProcessor::getNodes() const
{
    const juce::ScopedLock sl (nodeLock);
    return nodes;
}

void LfoAudioProcessor::setNodes (std::vector<lfo::Node> newNodes)
{
    lfo::sanitiseNodes (newNodes);
    lfo::renderNodes (newNodes, shapeData);

    const juce::ScopedLock sl (nodeLock);
    nodes = std::move (newNodes);
}

float LfoAudioProcessor::getStep (int index) const
{
    return shapeData.steps[(size_t) juce::jlimit (0, lfo::kMaxSteps - 1, index)].load();
}

void LfoAudioProcessor::setStep (int index, float value01)
{
    if (juce::isPositiveAndBelow (index, lfo::kMaxSteps))
        shapeData.steps[(size_t) index].store (juce::jlimit (0.0f, 1.0f, value01));
}

lfo::ShapeParams LfoAudioProcessor::getShapeParams() const
{
    lfo::ShapeParams sp;
    sp.shape = (lfo::Shape) juce::roundToInt (raw (ids::shape));
    sp.pulseWidth = raw (ids::pw);
    sp.skew = raw (ids::skew);
    sp.numSteps = juce::roundToInt (raw (ids::numSteps));
    return sp;
}

//==============================================================================
int LfoAudioProcessor::getNumPrograms() { return (int) presets().size(); }

const juce::String LfoAudioProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, getNumPrograms()) ? presets()[(size_t) index].name : juce::String();
}

void LfoAudioProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms()))
        return;

    currentProgram = index;

    for (auto* p : getParameters())
        p->setValueNotifyingHost (p->getDefaultValue());

    for (auto& [id, value] : presets()[(size_t) index].values)
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));

    setNodes (pumpNodes());

    for (int i = 0; i < lfo::kMaxSteps; ++i)
        setStep (i, kGatePattern[i]);
}

//==============================================================================
void LfoAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);

    juce::StringArray nodeText, stepText;

    for (auto& n : getNodes())
        nodeText.add (juce::String (n.x, 5) + "," + juce::String (n.y, 5) + "," + juce::String (n.curve, 4));

    for (int i = 0; i < lfo::kMaxSteps; ++i)
        stepText.add (juce::String (getStep (i), 4));

    state.setProperty ("nodes", nodeText.joinIntoString (";"), nullptr);
    state.setProperty ("steps", stepText.joinIntoString (";"), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void LfoAudioProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    currentProgram = juce::jlimit (0, getNumPrograms() - 1, (int) state.getProperty ("program", 0));

    if (state.hasProperty ("nodes"))
    {
        std::vector<lfo::Node> loaded;

        for (auto& item : juce::StringArray::fromTokens (state["nodes"].toString(), ";", ""))
        {
            auto parts = juce::StringArray::fromTokens (item, ",", "");

            if (parts.size() == 3)
                loaded.push_back ({ parts[0].getFloatValue(), parts[1].getFloatValue(), parts[2].getFloatValue() });
        }

        setNodes (std::move (loaded));
    }

    if (state.hasProperty ("steps"))
    {
        auto parts = juce::StringArray::fromTokens (state["steps"].toString(), ";", "");

        for (int i = 0; i < juce::jmin (parts.size(), (int) lfo::kMaxSteps); ++i)
            setStep (i, parts[i].getFloatValue());
    }

    apvts.replaceState (state);
}

//==============================================================================
bool LfoAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && out == layouts.getMainInputChannelSet();
}

void LfoAudioProcessor::prepareToPlay (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    phase = 0.0;
    lastCcValue = -1;
    smoothed[0] = smoothed[1] = 0.0f;

    for (auto& f : filters)
        f.reset();

    mixSmoothed.reset (sampleRate, 0.02);
    gainSmoothed.reset (sampleRate, 0.02);
    cutoffSmoothed.reset (sampleRate, 0.03);
    mixSmoothed.setCurrentAndTargetValue (raw (ids::mix) * 0.01f);
    gainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (raw (ids::outGain)));
    cutoffSmoothed.setCurrentAndTargetValue (raw (ids::cutoff));
}

void LfoAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (2, getTotalNumInputChannels(), buffer.getNumChannels());

    for (int ch = numChannels; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    // ---- parameters -------------------------------------------------------
    const auto sp = getShapeParams();
    const auto mode = (TriggerMode) juce::roundToInt (raw (ids::mode));
    const bool sync = raw (ids::sync) > 0.5f;
    const double rateHz = raw (ids::rate);
    const double beatsPerCycle = kDivisionBeats[juce::jlimit (0, 9, juce::roundToInt (raw (ids::division)))]
                                 * kModifierScale[juce::jlimit (0, 2, juce::roundToInt (raw (ids::modifier)))];
    const double phaseOffset = raw (ids::phase) / 360.0;
    const double stereoOffset = raw (ids::stereo) / 360.0;
    const float smoothMs = raw (ids::smooth);
    const float smoothCoef = smoothMs <= 0.01f ? 1.0f
                                               : 1.0f - std::exp (-1.0f / (smoothMs * 0.001f * (float) sampleRate));
    const float volAmt = raw (ids::volAmt) * 0.01f;
    const float panAmt = raw (ids::panAmt) * 0.01f;
    const int filterType = juce::roundToInt (raw (ids::filterType));
    const float filterOct = raw (ids::filterAmt);
    const float filterK = 1.0f / (0.5f * std::pow (24.0f, raw (ids::reso)));
    const float maxCutoff = (float) juce::jmin (20000.0, sampleRate * 0.45);
    const bool ccOn = raw (ids::ccOn) > 0.5f;
    const int ccNum = juce::roundToInt (raw (ids::ccNum));
    const int ccChan = juce::roundToInt (raw (ids::ccChan));

    mixSmoothed.setTargetValue (raw (ids::mix) * 0.01f);
    gainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (raw (ids::outGain)));
    cutoffSmoothed.setTargetValue (raw (ids::cutoff));

    // ---- host transport ---------------------------------------------------
    double bpm = 120.0;
    bool playing = false;
    juce::Optional<double> ppq, seconds;

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm(); b.hasValue() && *b > 1.0)
                bpm = *b;

            playing = pos->getIsPlaying();
            ppq = pos->getPpqPosition();
            seconds = pos->getTimeInSeconds();
        }
    }

    const double cyclesPerSecond = sync ? (bpm / 60.0) / beatsPerCycle : rateHz;
    const double increment = cyclesPerSecond / sampleRate;

    if (mode == TriggerMode::transport && playing)
    {
        if (sync && ppq.hasValue())
            phase = *ppq / beatsPerCycle;
        else if (! sync && seconds.hasValue())
            phase = *seconds * rateHz;
    }

    // ---- MIDI: notes retrigger the LFO, everything is passed through ------
    const bool notesTrigger = mode == TriggerMode::retrigger || mode == TriggerMode::oneShot;
    const bool oneShot = mode == TriggerMode::oneShot;
    const double oneShotEnd = 1.0 - 1.0e-9;
    auto midiIt = midi.cbegin();

    if (oneShot && phase >= oneShotEnd)
        phase = oneShotEnd;
    juce::MidiBuffer ccOut;

    float* channels[2] = { numChannels > 0 ? buffer.getWritePointer (0) : nullptr,
                           numChannels > 1 ? buffer.getWritePointer (1) : nullptr };

    for (int i = 0; i < numSamples; ++i)
    {
        for (; midiIt != midi.cend() && (*midiIt).samplePosition <= i; ++midiIt)
            if (notesTrigger && (*midiIt).getMessage().isNoteOn())
                phase = 0.0;

        // LFO values (left / right)
        const double phaseL = phase + phaseOffset;
        float lfoValue[2];
        lfoValue[0] = lfo::evaluate (sp, phaseL, shapeData);
        lfoValue[1] = std::abs (stereoOffset) < 1.0e-9 ? lfoValue[0] : lfo::evaluate (sp, phaseL + stereoOffset, shapeData);

        for (int ch = 0; ch < 2; ++ch)
        {
            smoothed[ch] += smoothCoef * (lfoValue[ch] - smoothed[ch]);
            lfoValue[ch] = smoothed[ch];
        }

        const float baseCutoff = cutoffSmoothed.getNextValue();
        const float mix = mixSmoothed.getNextValue();
        const float gain = gainSmoothed.getNextValue();

        const float pan = juce::jlimit (-1.0f, 1.0f, panAmt * lfoValue[0]);
        const float panGain[2] = { pan > 0.0f ? 1.0f - pan : 1.0f, pan < 0.0f ? 1.0f + pan : 1.0f };

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float dry = channels[ch][i];
            float wet = dry;

            if (filterType != 0)
            {
                const float fc = juce::jlimit (20.0f, maxCutoff, baseCutoff * std::exp2 (filterOct * lfoValue[ch]));
                const float g = std::tan (juce::MathConstants<float>::pi * fc / (float) sampleRate);
                wet = filters[ch].process (wet, filterType, g, filterK);
            }

            wet *= lfo::volumeGain (0.5f * (lfoValue[ch] + 1.0f), volAmt);

            if (numChannels == 2)
                wet *= panGain[ch];

            channels[ch][i] = (dry + mix * (wet - dry)) * gain;
        }

        if (ccOn)
        {
            const int value = juce::jlimit (0, 127, juce::roundToInt (63.5f * (lfoValue[0] + 1.0f)));

            if (value != lastCcValue)
            {
                ccOut.addEvent (juce::MidiMessage::controllerEvent (ccChan, ccNum, value), i);
                lastCcValue = value;
            }
        }

        phase += increment;

        if (oneShot && phase >= oneShotEnd)
            phase = oneShotEnd; // hold on the last value until the next note
    }

    if (filterType == 0)
        for (auto& f : filters)
            f.reset();

    if (! ccOn)
        lastCcValue = -1;

    midi.addEvents (ccOut, 0, numSamples, 0);

    const double shown = phase + phaseOffset;
    vizPhase.store ((float) (shown - std::floor (shown)));
    vizValue.store (smoothed[0]);
}

juce::AudioProcessorEditor* LfoAudioProcessor::createEditor() { return new LfoAudioProcessorEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LfoAudioProcessor(); }
