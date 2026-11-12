#pragma once

#include "PluginProcessor.h"

namespace theme
{
const juce::Colour background { 0xff12141a };
const juce::Colour panel { 0xff1b1e27 };
const juce::Colour panelEdge { 0xff2a2e3b };
const juce::Colour track { 0xff303544 };
const juce::Colour text { 0xffe6e8ee };
const juce::Colour dim { 0xff8b91a3 };
const juce::Colour accent { 0xff35d6c8 };
const juce::Colour accent2 { 0xffffa94d };
} // namespace theme

class LfoLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LfoLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::FontOptions (14.0f); }
    juce::Font getPopupMenuFont() override { return juce::FontOptions (14.0f); }
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

//==============================================================================
/** Shows one LFO cycle with a moving playhead. Step and custom shapes are edited here with the mouse. */
class LfoDisplay final : public juce::Component, private juce::Timer
{
public:
    explicit LfoDisplay (LfoAudioProcessor&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    bool snap = false;

private:
    void timerCallback() override { repaint(); }
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (14.0f, 16.0f); }
    juce::Point<float> toScreen (float x, float y) const;
    juce::Point<float> fromScreen (juce::Point<float>) const; // -> (phase 0..1, value -1..1)
    int nodeAt (juce::Point<float>, const std::vector<lfo::Node>&) const;
    void paintStep (juce::Point<float>);

    LfoAudioProcessor& processor;
    std::vector<lfo::Node> editNodes;
    int dragNode = -1, dragSegment = -1;
    float dragStartCurve = 0.0f;
};

//==============================================================================
class LfoAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit LfoAudioProcessorEditor (LfoAudioProcessor&);
    ~LfoAudioProcessorEditor() override;

    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct Choice
    {
        juce::ComboBox box;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };

    struct Toggle
    {
        juce::ToggleButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    /** Everything is laid out at a fixed base size and scaled to the window. */
    struct Content final : public juce::Component
    {
        void paint (juce::Graphics&) override;
        std::vector<std::pair<juce::Rectangle<int>, juce::String>> panels;
    };

    void addKnob (Knob&, const char* id, const juce::String& name);
    void addChoice (Choice&, const char* id);
    void addToggle (Toggle&, const char* id, const juce::String& text);
    void layout();
    void updateSyncVisibility();

    LfoAudioProcessor& processor;
    LfoLookAndFeel lookAndFeel;
    Content content;
    LfoDisplay display;

    juce::ComboBox presetBox;
    juce::ToggleButton snapButton { "Snap" };

    Choice shape, mode, division, modifier, filterType;
    Toggle sync, ccOn;
    Knob rate, phase, stereo, skew, pw, smooth, numSteps;
    Knob volAmt, panAmt, cutoff, reso, filterAmt, ccNum, ccChan, mix, outGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LfoAudioProcessorEditor)
};
