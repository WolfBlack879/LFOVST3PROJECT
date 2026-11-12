#include "PluginEditor.h"

namespace
{
constexpr int kBaseWidth = 920, kBaseHeight = 600;
}

//==============================================================================
LfoLookAndFeel::LfoLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, theme::background);
    setColour (juce::Slider::textBoxTextColourId, theme::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, theme::accent.withAlpha (0.4f));
    setColour (juce::Label::textColourId, theme::dim);
    setColour (juce::ComboBox::backgroundColourId, theme::background);
    setColour (juce::ComboBox::outlineColourId, theme::panelEdge);
    setColour (juce::ComboBox::textColourId, theme::text);
    setColour (juce::ComboBox::arrowColourId, theme::dim);
    setColour (juce::PopupMenu::backgroundColourId, theme::panel);
    setColour (juce::PopupMenu::textColourId, theme::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::accent.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, theme::text);
    setColour (juce::TextEditor::backgroundColourId, theme::background);
    setColour (juce::TextEditor::textColourId, theme::text);
    setColour (juce::TextEditor::highlightColourId, theme::accent.withAlpha (0.4f));
    setColour (juce::CaretComponent::caretColourId, theme::accent);
}

void LfoLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                       float startAngle, float endAngle, juce::Slider& slider)
{
    auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (5.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float thickness = 4.0f;

    juce::Path backArc;
    backArc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour (theme::track);
    g.strokePath (backArc, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Bipolar parameters fill from the centre, the rest from the start.
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float from = bipolar ? startAngle + (float) slider.valueToProportionOfLength (0.0) * (endAngle - startAngle) : startAngle;

    juce::Path valueArc;
    valueArc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (slider.isEnabled() ? theme::accent : theme::dim);
    g.strokePath (valueArc, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float knobRadius = radius - 8.0f;
    g.setColour (theme::panelEdge);
    g.fillEllipse (juce::Rectangle<float> (knobRadius * 2.0f, knobRadius * 2.0f).withCentre (centre));

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -knobRadius + 3.0f, 3.0f, knobRadius * 0.55f, 1.5f);
    g.setColour (theme::text);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

void LfoLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = button.getToggleState();

    g.setColour (on ? theme::accent.withAlpha (highlighted ? 1.0f : 0.9f) : theme::background);
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (on ? theme::accent : (highlighted ? theme::dim : theme::panelEdge));
    g.drawRoundedRectangle (bounds, 5.0f, 1.0f);

    g.setColour (on ? theme::background : theme::dim);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
}

juce::Label* LfoLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);
    label->setFont (juce::FontOptions (12.5f));
    return label;
}

//==============================================================================
LfoDisplay::LfoDisplay (LfoAudioProcessor& p) : processor (p)
{
    startTimerHz (60);
}

static double skewExponent (float skew) { return std::exp2 (-2.0 * (double) skew); }

juce::Point<float> LfoDisplay::toScreen (float x, float y) const
{
    // x is a position in the unskewed shape; show it where the skewed cycle plays it.
    const auto area = plot();
    const auto shown = (float) std::pow ((double) x, 1.0 / skewExponent (processor.getShapeParams().skew));
    return { area.getX() + shown * area.getWidth(), area.getCentreY() - y * area.getHeight() * 0.5f };
}

juce::Point<float> LfoDisplay::fromScreen (juce::Point<float> pos) const
{
    const auto area = plot();
    const float shown = juce::jlimit (0.0f, 1.0f, (pos.x - area.getX()) / area.getWidth());
    float x = (float) std::pow ((double) shown, skewExponent (processor.getShapeParams().skew));
    float y = juce::jlimit (-1.0f, 1.0f, (area.getCentreY() - pos.y) / (area.getHeight() * 0.5f));

    if (snap)
    {
        x = std::round (x * 16.0f) / 16.0f;
        y = std::round (y * 4.0f) / 4.0f;
    }

    return { x, y };
}

int LfoDisplay::nodeAt (juce::Point<float> pos, const std::vector<lfo::Node>& nodes) const
{
    int best = -1;
    float bestDistance = 10.0f;

    for (int i = 0; i < (int) nodes.size(); ++i)
    {
        const float d = toScreen (nodes[(size_t) i].x, nodes[(size_t) i].y).getDistanceFrom (pos);

        if (d < bestDistance)
        {
            bestDistance = d;
            best = i;
        }
    }

    return best;
}

void LfoDisplay::paintStep (juce::Point<float> pos)
{
    const bool wasSnapping = std::exchange (snap, false);
    const auto p = fromScreen (pos);
    snap = wasSnapping;

    const int n = processor.getShapeParams().numSteps;
    float level = 0.5f * (p.y + 1.0f);

    if (snap)
        level = std::round (level * 8.0f) / 8.0f;

    processor.setStep (juce::jlimit (0, n - 1, (int) (p.x * (float) n)), level);
}

void LfoDisplay::mouseDown (const juce::MouseEvent& e)
{
    const auto shape = processor.getShapeParams().shape;
    dragNode = dragSegment = -1;

    if (shape == lfo::Shape::steps)
    {
        paintStep (e.position);
    }
    else if (shape == lfo::Shape::custom)
    {
        editNodes = processor.getNodes();
        dragNode = nodeAt (e.position, editNodes);

        if (dragNode < 0)
        {
            const bool wasSnapping = std::exchange (snap, false);
            const float x = fromScreen (e.position).x;
            snap = wasSnapping;

            for (int i = 0; i + 1 < (int) editNodes.size(); ++i)
                if (x >= editNodes[(size_t) i].x && x <= editNodes[(size_t) i + 1].x)
                    dragSegment = i;

            if (dragSegment >= 0)
                dragStartCurve = editNodes[(size_t) dragSegment].curve;
        }
    }
}

void LfoDisplay::mouseDrag (const juce::MouseEvent& e)
{
    const auto shape = processor.getShapeParams().shape;

    if (shape == lfo::Shape::steps)
    {
        paintStep (e.position);
        return;
    }

    if (shape != lfo::Shape::custom)
        return;

    if (dragNode >= 0 && dragNode < (int) editNodes.size())
    {
        const auto p = fromScreen (e.position);
        auto& node = editNodes[(size_t) dragNode];
        const bool isEnd = dragNode == 0 || dragNode == (int) editNodes.size() - 1;

        if (! isEnd)
            node.x = juce::jlimit (editNodes[(size_t) dragNode - 1].x, editNodes[(size_t) dragNode + 1].x, p.x);

        node.y = p.y;
        processor.setNodes (editNodes);
    }
    else if (dragSegment >= 0 && dragSegment + 1 < (int) editNodes.size())
    {
        // Dragging up always pulls the segment upwards, whichever way it slopes.
        const bool rising = editNodes[(size_t) dragSegment + 1].y >= editNodes[(size_t) dragSegment].y;
        const float amount = (float) -e.getDistanceFromDragStartY() / 120.0f;
        editNodes[(size_t) dragSegment].curve = juce::jlimit (-1.0f, 1.0f, dragStartCurve + (rising ? -amount : amount));
        processor.setNodes (editNodes);
    }
}

void LfoDisplay::mouseUp (const juce::MouseEvent&)
{
    dragNode = dragSegment = -1;
}

void LfoDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (processor.getShapeParams().shape != lfo::Shape::custom)
        return;

    auto nodes = processor.getNodes();
    const int hit = nodeAt (e.position, nodes);

    if (hit > 0 && hit < (int) nodes.size() - 1)
    {
        nodes.erase (nodes.begin() + hit);
    }
    else if (hit < 0)
    {
        const auto p = fromScreen (e.position);
        nodes.push_back ({ p.x, p.y, 0.0f });
    }

    processor.setNodes (std::move (nodes));
}

void LfoDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto area = plot();
    const auto sp = processor.getShapeParams();

    g.setColour (theme::panel);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (theme::panelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    // grid
    for (int i = 0; i <= 16; ++i)
    {
        const float x = area.getX() + area.getWidth() * (float) i / 16.0f;
        g.setColour (theme::track.withAlpha (i % 4 == 0 ? 0.9f : 0.35f));
        g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
    }

    for (int i = 0; i <= 4; ++i)
    {
        g.setColour (theme::track.withAlpha (i == 2 ? 0.9f : 0.35f));
        g.drawHorizontalLine (juce::roundToInt (area.getY() + area.getHeight() * (float) i / 4.0f), area.getX(), area.getRight());
    }

    // waveform
    juce::Path curve;
    const int points = juce::jmax (2, (int) area.getWidth());

    for (int i = 0; i <= points; ++i)
    {
        const double p = (double) i / (double) points * 0.999999;
        const float v = lfo::evaluate (sp, p, processor.shapeData);
        const juce::Point<float> pt (area.getX() + area.getWidth() * (float) i / (float) points,
                                     area.getCentreY() - v * area.getHeight() * 0.5f);
        if (i == 0)
            curve.startNewSubPath (pt);
        else
            curve.lineTo (pt);
    }

    juce::Path fill (curve);
    fill.lineTo (area.getRight(), area.getBottom());
    fill.lineTo (area.getX(), area.getBottom());
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (theme::accent.withAlpha (0.28f), 0.0f, area.getY(),
                                             theme::accent.withAlpha (0.02f), 0.0f, area.getBottom(), false));
    g.fillPath (fill);

    g.setColour (theme::accent);
    g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (sp.shape == lfo::Shape::custom)
    {
        const auto& nodes = dragNode >= 0 || dragSegment >= 0 ? editNodes : processor.getNodes();

        for (auto& n : nodes)
        {
            const auto dot = juce::Rectangle<float> (9.0f, 9.0f).withCentre (toScreen (n.x, n.y));
            g.setColour (theme::background);
            g.fillEllipse (dot);
            g.setColour (theme::text);
            g.drawEllipse (dot, 1.6f);
        }
    }

    // playhead
    const float phase = processor.vizPhase.load();
    const float value = juce::jlimit (-1.0f, 1.0f, processor.vizValue.load());
    const float px = area.getX() + phase * area.getWidth();

    g.setColour (theme::accent2.withAlpha (0.5f));
    g.drawVerticalLine (juce::roundToInt (px), area.getY(), area.getBottom());
    g.setColour (theme::accent2);
    g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ px, area.getCentreY() - value * area.getHeight() * 0.5f }));

    juce::String hint;

    if (sp.shape == lfo::Shape::custom)
        hint = "Drag points  |  double-click to add or remove  |  drag a line to bend it";
    else if (sp.shape == lfo::Shape::steps)
        hint = "Click and drag to draw step levels";

    if (hint.isNotEmpty())
    {
        g.setColour (theme::dim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (hint, bounds.reduced (12.0f, 3.0f), juce::Justification::bottomRight);
    }
}

//==============================================================================
void LfoAudioProcessorEditor::Content::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    g.setColour (theme::text);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("LFO", 18, 10, 50, 30, juce::Justification::centredLeft);
    g.setColour (theme::accent);
    g.drawText ("STUDIO", 62, 10, 120, 30, juce::Justification::centredLeft);

    for (auto& [area, title] : panels)
    {
        g.setColour (theme::panel);
        g.fillRoundedRectangle (area.toFloat(), 8.0f);
        g.setColour (theme::panelEdge);
        g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 8.0f, 1.0f);
        g.setColour (theme::dim);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (title, area.reduced (12, 7), juce::Justification::topLeft);
    }
}

LfoAudioProcessorEditor::LfoAudioProcessorEditor (LfoAudioProcessor& p)
    : AudioProcessorEditor (p), processor (p), display (p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (content);
    content.addAndMakeVisible (display);

    for (int i = 0; i < processor.getNumPrograms(); ++i)
        presetBox.addItem (processor.getProgramName (i), i + 1);

    presetBox.setSelectedId (processor.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.setTooltip ("Factory presets");
    presetBox.onChange = [this] { processor.setCurrentProgram (presetBox.getSelectedId() - 1); };
    content.addAndMakeVisible (presetBox);

    snapButton.setClickingTogglesState (true);
    snapButton.onClick = [this] { display.snap = snapButton.getToggleState(); };
    content.addAndMakeVisible (snapButton);

    addChoice (shape, ids::shape);
    addChoice (mode, ids::mode);
    addChoice (division, ids::division);
    addChoice (modifier, ids::modifier);
    addChoice (filterType, ids::filterType);
    addToggle (sync, ids::sync, "Sync");
    addToggle (ccOn, ids::ccOn, "Send");

    addKnob (rate, ids::rate, "Rate");
    addKnob (phase, ids::phase, "Phase");
    addKnob (stereo, ids::stereo, "Stereo");
    addKnob (skew, ids::skew, "Skew");
    addKnob (pw, ids::pw, "Pulse W");
    addKnob (smooth, ids::smooth, "Smooth");
    addKnob (numSteps, ids::numSteps, "Steps");
    addKnob (volAmt, ids::volAmt, "Volume");
    addKnob (panAmt, ids::panAmt, "Pan");
    addKnob (cutoff, ids::cutoff, "Cutoff");
    addKnob (reso, ids::reso, "Reso");
    addKnob (filterAmt, ids::filterAmt, "Amount");
    addKnob (ccNum, ids::ccNum, "CC #");
    addKnob (ccChan, ids::ccChan, "Channel");
    addKnob (mix, ids::mix, "Mix");
    addKnob (outGain, ids::outGain, "Gain");

    sync.button.onStateChange = [this] { updateSyncVisibility(); };

    layout();
    updateSyncVisibility();

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) kBaseWidth / kBaseHeight);
    setResizeLimits (kBaseWidth * 3 / 4, kBaseHeight * 3 / 4, kBaseWidth * 2, kBaseHeight * 2);
    setSize (kBaseWidth, kBaseHeight);
}

LfoAudioProcessorEditor::~LfoAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void LfoAudioProcessorEditor::addKnob (Knob& k, const char* id, const juce::String& name)
{
    // Added to the parent first so the text box is built with our look and feel.
    content.addAndMakeVisible (k.slider);
    content.addAndMakeVisible (k.label);
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 16);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (12.0f));
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, id, k.slider);
}

void LfoAudioProcessorEditor::addChoice (Choice& c, const char* id)
{
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (id)))
        c.box.addItemList (param->choices, 1);

    content.addAndMakeVisible (c.box);
    c.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, id, c.box);
}

void LfoAudioProcessorEditor::addToggle (Toggle& t, const char* id, const juce::String& text)
{
    t.button.setButtonText (text);
    content.addAndMakeVisible (t.button);
    t.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, id, t.button);
}

void LfoAudioProcessorEditor::updateSyncVisibility()
{
    const bool synced = sync.button.getToggleState();
    rate.slider.setVisible (! synced);
    rate.label.setVisible (! synced);
    division.box.setVisible (synced);
    modifier.box.setVisible (synced);
}

void LfoAudioProcessorEditor::layout()
{
    content.setSize (kBaseWidth, kBaseHeight);

    presetBox.setBounds (kBaseWidth - 16 - 240, 12, 240, 26);
    snapButton.setBounds (kBaseWidth - 16 - 240 - 8 - 64, 12, 64, 26);
    display.setBounds (16, 50, kBaseWidth - 32, 262);

    const int rowA = 322, rowB = 462, rowH = 130;
    content.panels.clear();

    auto panel = [this] (int x, int y, int w, int h, const char* title)
    {
        juce::Rectangle<int> r (x, y, w, h);
        content.panels.emplace_back (r, title);
        return r.withTrimmedTop (22).reduced (10, 0).withTrimmedBottom (8);
    };

    auto placeKnobs = [] (juce::Rectangle<int> area, std::initializer_list<Knob*> knobs)
    {
        const int w = area.getWidth() / (int) knobs.size();

        for (auto* k : knobs)
        {
            auto cell = area.removeFromLeft (w);
            k->label.setBounds (cell.removeFromTop (16));
            k->slider.setBounds (cell);
        }
    };

    // --- row A ---
    auto lfoArea = panel (16, rowA, 330, rowH, "LFO");
    auto left = lfoArea.removeFromLeft (150);
    shape.box.setBounds (left.removeFromTop (28).translated (0, 6));
    mode.box.setBounds (left.removeFromTop (28).translated (0, 16));
    lfoArea.removeFromLeft (14);
    sync.button.setBounds (lfoArea.removeFromLeft (56).withHeight (28).translated (0, 6));
    lfoArea.removeFromLeft (10);
    placeKnobs (lfoArea, { &rate });
    division.box.setBounds (lfoArea.withHeight (28).translated (0, 6));
    modifier.box.setBounds (lfoArea.withHeight (28).translated (0, 44));

    placeKnobs (panel (354, rowA, 550, rowH, "SHAPE"), { &phase, &stereo, &skew, &pw, &smooth, &numSteps });

    // --- row B ---
    placeKnobs (panel (16, rowB, 180, rowH, "LEVEL"), { &volAmt, &panAmt });

    auto filterArea = panel (204, rowB, 356, rowH, "FILTER");
    filterType.box.setBounds (filterArea.removeFromLeft (110).withHeight (28).translated (0, 6));
    filterArea.removeFromLeft (6);
    placeKnobs (filterArea, { &cutoff, &reso, &filterAmt });

    auto ccArea = panel (568, rowB, 180, rowH, "MIDI CC OUT");
    ccOn.button.setBounds (ccArea.removeFromLeft (48).withHeight (28).translated (0, 6));
    placeKnobs (ccArea, { &ccNum, &ccChan });

    placeKnobs (panel (756, rowB, 148, rowH, "OUTPUT"), { &mix, &outGain });
}

void LfoAudioProcessorEditor::resized()
{
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) kBaseWidth));
}
