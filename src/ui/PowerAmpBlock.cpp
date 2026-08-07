#include "PowerAmpBlock.h"

#include "../Parameters.h"

namespace orbitamp
{

PowerAmpBlock::PowerAmpBlock (juce::AudioProcessorValueTreeState& s)
    : BlockFrame ("Power Amp", BlockFrame::Kind::dsp), state (s)
{
    addAndMakeVisible (type);
    addAndMakeVisible (tube);
    addAndMakeVisible (count);

    tube.setItems (params::powerTubes, 2);
    count.accent = theme::orange;
    count.setItems (params::powerCounts, 1);

    tubeAttachment = std::make_unique<juce::ParameterAttachment> (
        *state.getParameter (params::powerTube),
        [this] (float v) { tube.setSelectedIndex (juce::roundToInt (v), juce::dontSendNotification); repaint(); });

    countAttachment = std::make_unique<juce::ParameterAttachment> (
        *state.getParameter (params::powerCount),
        [this] (float v) { count.setSelectedIndex (juce::roundToInt (v), juce::dontSendNotification); repaint(); });

    tube.onChange  = [this] (int v) { tubeAttachment->setValueAsCompleteGesture ((float) v); };
    count.onChange = [this] (int v) { countAttachment->setValueAsCompleteGesture ((float) v); };
    addAndMakeVisible (drive);
    addAndMakeVisible (sag);

    driveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, params::powerDrive, drive);
    sagAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, params::powerSag, sag);

    attachPower (*state.getParameter (params::powerOn));

    type.setItems (params::powerTypes, 0);

    typeAttachment = std::make_unique<juce::ParameterAttachment> (
        *state.getParameter (params::powerType),
        [this] (float v)
        {
            const int index = juce::roundToInt (v);
            type.setSelectedIndex (index, juce::dontSendNotification);

            // Index 0 is our simulation, the rest are captures. The frame follows.
            setKind (index == 0 ? BlockFrame::Kind::dsp : BlockFrame::Kind::captured);
        });

    type.onChange = [this] (int i) { typeAttachment->setValueAsCompleteGesture ((float) i); };

    typeAttachment->sendInitialUpdate();
    tubeAttachment->sendInitialUpdate();
    countAttachment->sendInitialUpdate();
}

PowerAmpBlock::~PowerAmpBlock() = default;

void PowerAmpBlock::layOutHeader (juce::Rectangle<int> area)
{
    type.setBounds (area);
}

void PowerAmpBlock::layOutContent (juce::Rectangle<int> area)
{
    // The bottle and how many of it — the amp rather than a setting on it, so they sit above the
    // knobs with the model's name, not among them.
    auto tubeStrip = area.removeFromTop (tubeRow);
    count.setBounds (tubeStrip.removeFromRight (countCol));
    tubeStrip.removeFromRight (rowGap);
    tube.setBounds (tubeStrip);

    area.removeFromTop (rowGap);
    glyphArea = area.removeFromTop (glyphRow);
    area.removeFromTop (rowGap);

    // Two knobs out of the stage's ten controls: Drive is what a power amp is for, Sag is what makes
    // it feel like one. Everything else is the model, not a setting.
    const int side  = juce::jmin (area.getHeight(), (area.getWidth() - knobGap) / 2);
    const int total = side * 2 + knobGap;

    auto row = area.withSizeKeepingCentre (total, side);
    drive.setBounds (row.removeFromLeft (side));
    row.removeFromLeft (knobGap);
    sag.setBounds (row.removeFromLeft (side));
}

void PowerAmpBlock::paintContent (juce::Graphics& g)
{
    // The bottles themselves, one glyph each. appkit already draws them, counts them and colours
    // them; the block only says which and how many.
    if (glyphArea.isEmpty())
        return;

    const int n = juce::jlimit (params::powerCounts.indexOf ("1") + 1, 2, count.getSelectedIndex() + 1);
    const felitronics::appkit::DeviceSpec spec { { felitronics::appkit::DeviceType::tube, n } };

    felitronics::appkit::drawDeviceSpecStatic (g, glyphArea.toFloat(), spec);
}

} // namespace orbitamp
