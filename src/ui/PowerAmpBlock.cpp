#include "PowerAmpBlock.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"

namespace orbitamp
{

PowerAmpBlock::PowerAmpBlock (AmpProcessor& processor, core::CapturedBlock& b)
    : BlockFrame ("Power", BlockFrame::Kind::captured), amp (processor), block (b)
{
    // The pack's name is the block's name, on the border.
    showTitle = false;
    device.fontHeight = 16.0f;
    device.tracking   = 0.15f;
    device.boxed      = false;
    addAndMakeVisible (device);

    // The dial wears no label; its name is the pack's own for that axis, under the mouse. Its arc
    // runs cold to hot, like the other captured dials'.
    gain.labelRowHeight = 0;
    gain.heat = true;
    addChildComponent (gain);

    attachPower (*amp.apvts.getParameter (params::powerOn));

    // The attachment hears everyone BUT this block — a restored session, a host automating the
    // parameter. Load the device it names before rebuilding the face.
    deviceAttachment = std::make_unique<juce::ParameterAttachment> (
        *amp.apvts.getParameter (params::blockDevice (params::powerId)),
        [this] (float v)
        {
            const int i = juce::roundToInt (v);
            device.setSelection (i);
            block.selectIfMoved (i);
            deviceChanged();
        });

    device.onPick = [this] (int i)
    {
        deviceAttachment->setValueAsCompleteGesture ((float) i);
        block.select (i);   // message thread — it reads files
        deviceChanged();
    };

    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        amp.apvts, params::blockGain (params::powerId), gain);

    deviceAttachment->sendInitialUpdate();
    deviceChanged();
}

PowerAmpBlock::~PowerAmpBlock() = default;

void PowerAmpBlock::deviceChanged()
{
    juce::Array<VoicingSelector::Entry> entries;
    bool sawUser = false;

    for (const auto& pack : block.packs)
    {
        VoicingSelector::Entry e;
        e.name = pack.displayName();
        e.character = pack.character;
        e.startsSection = ! pack.bundled && ! sawUser;
        sawUser = sawUser || ! pack.bundled;
        entries.add (std::move (e));
    }

    device.setEntries (std::move (entries));

    // What was captured: the circuit's glyphs and the gear's full name, the voice's alias under it.
    spec  = felitronics::appkit::parseDeviceSpec (block.circuit());
    name  = block.deviceName();
    alias = {};

    if (auto* devParam = amp.apvts.getParameter (params::blockDevice (params::powerId));
        devParam != nullptr && ! block.packs.isEmpty())
    {
        const int chosen = juce::jlimit (0, block.packs.size() - 1,
                                         juce::roundToInt (devParam->convertFrom0to1 (devParam->getValue())));
        alias = block.packs.getReference (chosen).alias;
    }

    // The dial, only where the pack has an axis to turn: its detents are the captured positions.
    const auto positions = block.gainPositions();
    gain.setNotches (positions.size());
    gain.setVisible (! block.dialName().isEmpty());
    gain.setTooltip (block.dialName().isNotEmpty() ? block.dialName().toUpperCase() : juce::String ("GAIN"));

    resized();
    repaint();
}

void PowerAmpBlock::layOutContent (juce::Rectangle<int> area)
{
    // The pack's name on the border where the block's name would stand: at the left, sized to itself.
    {
        const auto slot = borderSlotArea();
        device.setBounds (slot.withWidth (juce::jmin (slot.getWidth(), device.idealWidth())));
        borderSlotUsed = device.getBounds();
    }

    // The dial takes the right side when there is one; the circuit's glyphs and the name the rest,
    // glyphs above, words below.
    if (gain.isVisible())
    {
        const int side = juce::jmin (maxKnobSide, juce::jmin (area.getWidth() / 2, area.getHeight()));
        gain.setBounds (area.removeFromRight (side).withSizeKeepingCentre (side, side));
        area.removeFromRight (gap);
    }

    textArea  = area.removeFromBottom (34);
    area.removeFromBottom (gap);
    glyphArea = area;
}

void PowerAmpBlock::paintContent (juce::Graphics& g)
{
    if (block.packs.isEmpty())
    {
        g.setColour (theme::txFaint.withAlpha (0.5f));
        theme::drawTracked (g, "No power amp loaded", contentArea().toFloat(), theme::displayFont (8.0f),
                            0.1f, juce::Justification::centred);
        return;
    }

    felitronics::appkit::drawDeviceSpecStatic (g, glyphArea.toFloat().reduced (4.0f), spec);

    auto t = textArea.toFloat();
    g.setColour (theme::tx);
    theme::drawTracked (g, name.toUpperCase(), t.removeFromTop (18.0f), theme::displayFont (12.0f), 0.08f,
                        juce::Justification::centred);

    if (alias.isNotEmpty())
    {
        g.setColour (theme::txDim);
        theme::drawTracked (g, alias.toUpperCase(), t, theme::displayFont (10.0f), 0.08f,
                            juce::Justification::centred);
    }
}

} // namespace orbitamp
