#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "LfoEngine.h"

namespace ids
{
#define LFO_ID(name) inline constexpr const char* name = #name;
LFO_ID (shape) LFO_ID (mode) LFO_ID (sync) LFO_ID (rate) LFO_ID (division) LFO_ID (modifier)
LFO_ID (phase) LFO_ID (stereo) LFO_ID (skew) LFO_ID (pw) LFO_ID (smooth) LFO_ID (numSteps)
LFO_ID (volAmt) LFO_ID (panAmt)
LFO_ID (filterType) LFO_ID (cutoff) LFO_ID (reso) LFO_ID (filterAmt)
LFO_ID (ccOn) LFO_ID (ccNum) LFO_ID (ccChan)
LFO_ID (mix) LFO_ID (outGain)
#undef LFO_ID
} // namespace ids

enum class TriggerMode { free = 0, transport, retrigger, oneShot };

class LfoAudioProcessor final : public juce::AudioProcessor
{
public:
    LfoAudioProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==============================================================================
    // Shape data edited from the UI. Call these from the message thread only.
    std::vector<lfo::Node> getNodes() const;
    void setNodes (std::vector<lfo::Node> newNodes);
    float getStep (int index) const;
    void setStep (int index, float value01);

    lfo::ShapeParams getShapeParams() const;

    juce::AudioProcessorValueTreeState apvts;
    lfo::ShapeData shapeData;

    // Written by the audio thread so the editor can animate the playhead.
    std::atomic<float> vizPhase { 0.0f }, vizValue { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    float raw (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    juce::CriticalSection nodeLock;
    std::vector<lfo::Node> nodes;

    int currentProgram = 0;
    double sampleRate = 44100.0;
    double phase = 0.0;
    float smoothed[2] { 0.0f, 0.0f };
    lfo::StateVariableFilter filters[2];
    int lastCcValue = -1;

    juce::SmoothedValue<float> mixSmoothed, gainSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> cutoffSmoothed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LfoAudioProcessor)
};
