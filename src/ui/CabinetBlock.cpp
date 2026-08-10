#include "CabinetBlock.h"

#include "../Parameters.h"

namespace orbitamp
{

CabinetBlock::CabinetBlock (juce::AudioProcessorValueTreeState& s)
    : BlockFrame ("Cabinet", BlockFrame::Kind::captured), state (s)
{
    addAndMakeVisible (grid);
    attachPower (*state.getParameter (params::cabOn));

    grid.setUnit ("cm");

    // Clicking an empty cell moves the ACTIVE mic; clicking a dot makes that mic active; dragging a
    // dot moves that one. One gesture vocabulary for two mics, no modes.
    grid.onSelect     = [this] (const juce::String& pos, double d) { placeMic (activeSlot, pos, d); };
    grid.onDotSelect  = [this] (int id) { activeSlot = id; refreshDots(); };
    grid.onDotDrag    = [this] (int id, const juce::String& pos, double d) { placeMic (id, pos, d); };

    for (int i = 0; i < params::cabNumMics; ++i)
    {
        auto& slot = slots[(size_t) i];

        const auto tint = felitronics::appkit::brand::slotColours[(size_t) i];

        slot.pick = std::make_unique<Selector> (tint, true);
        slot.pick->setItems (params::cabMics, i);
        addAndMakeVisible (*slot.pick);

        // Three named angles, all visible at once. There is nothing between them: each one is a
        // capture that was or was not taken.
        slot.angle = std::make_unique<StepSwitch>();
        slot.angle->accent = tint;
        slot.angle->setItems (params::cabAngles, 1);
        addAndMakeVisible (*slot.angle);

        slot.onAtt = std::make_unique<juce::ParameterAttachment> (
            *state.getParameter (params::cabMicOn (i)),
            [this, i] (float v)
            {
                slots[(size_t) i].on = v > 0.5f;
                const float a = slots[(size_t) i].on ? 1.0f : theme::offAlpha;
                slots[(size_t) i].pick->setAlpha (a);
                slots[(size_t) i].angle->setAlpha (a);
                refreshDots();
                repaint();
            });

        slot.typeAtt = std::make_unique<juce::ParameterAttachment> (
            *state.getParameter (params::cabMicType (i)),
            [this, i] (float v)
            {
                slots[(size_t) i].pick->setSelectedIndex (juce::roundToInt (v), juce::dontSendNotification);
                refreshDots();
            });

        slot.angleAtt = std::make_unique<juce::ParameterAttachment> (
            *state.getParameter (params::cabMicAngle (i)),
            [this, i] (float v) { slots[(size_t) i].angle->setSelectedIndex (juce::roundToInt (v),
                                                                             juce::dontSendNotification); });

        slot.angle->onChange = [this, i] (int v)
        {
            slots[(size_t) i].angleAtt->setValueAsCompleteGesture ((float) v);
            activeSlot = i;
            refreshDots();
        };

        slot.posAtt  = std::make_unique<juce::ParameterAttachment> (
            *state.getParameter (params::cabMicPos (i)),  [this] (float) { refreshDots(); });
        slot.distAtt = std::make_unique<juce::ParameterAttachment> (
            *state.getParameter (params::cabMicDist (i)), [this] (float) { refreshDots(); });

        slot.pick->onChange = [this, i] (int v)
        {
            slots[(size_t) i].typeAtt->setValueAsCompleteGesture ((float) v);
            activeSlot = i;
            refreshDots();
        };
    }

    for (auto& slot : slots)
        for (auto* a : { &slot.onAtt, &slot.typeAtt, &slot.posAtt, &slot.distAtt, &slot.angleAtt })
            (*a)->sendInitialUpdate();
}

CabinetBlock::~CabinetBlock() = default;

void CabinetBlock::placeMic (int slot, const juce::String& position, double distanceCm)
{
    const int index = params::cabPositions.indexOf (position);
    if (index < 0 || slot < 0 || slot >= params::cabNumMics)
        return;

    auto& s = slots[(size_t) slot];

    // Both halves of one gesture: they land in the same message-loop turn, so the history's settle
    // timer folds them into a single undo step.
    s.posAtt->setValueAsCompleteGesture ((float) index);
    s.distAtt->setValueAsCompleteGesture ((float) distanceCm);
    activeSlot = slot;
}

void CabinetBlock::refreshDots()
{
    std::vector<felitronics::appkit::MicGrid::Dot> dots;

    for (int i = 0; i < params::cabNumMics; ++i)
    {
        if (! slots[(size_t) i].on)
            continue;   // a mic that is switched off is not standing anywhere

        const int pos = juce::jlimit (0, params::cabPositions.size() - 1,
                                      juce::roundToInt (state.getRawParameterValue (params::cabMicPos (i))->load()));
        const double dist = (double) state.getRawParameterValue (params::cabMicDist (i))->load();

        dots.push_back ({ i, params::cabPositions[pos], dist,
                          felitronics::appkit::brand::slotColours[(size_t) i], i == activeSlot });
    }

    grid.setDots (std::move (dots));
}

juce::Rectangle<int> CabinetBlock::switchArea (int slot) const
{
    auto col = contentArea().removeFromLeft (micColumn);
    const int band = col.getHeight() / params::cabNumMics;

    return col.removeFromTop (band)
              .withSizeKeepingCentre (col.getWidth(), micRow + gap / 2 + angleRow)
              .removeFromTop (micRow)
              .removeFromLeft (micSwitchW).withSizeKeepingCentre (micSwitchW, micSwitchH)
              .translated (0, slot * band);
}

void CabinetBlock::layOutContent (juce::Rectangle<int> area)
{
    auto mics = area.removeFromLeft (micColumn);
    area.removeFromLeft (gap);
    grid.setBounds (area);

    // The column is split evenly and each mic is centred in its share, so the two rows use the whole
    // height instead of huddling at the top over empty space.
    const int band = mics.getHeight() / params::cabNumMics;

    for (int i = 0; i < params::cabNumMics; ++i)
    {
        auto cell = mics.removeFromTop (band)
                        .withSizeKeepingCentre (mics.getWidth(), micRow + gap / 2 + angleRow);
        auto row = cell.removeFromTop (micRow);
        row.removeFromLeft (micSwitchW + gap);
        slots[(size_t) i].pick->setBounds (row);

        cell.removeFromTop (gap / 2);
        slots[(size_t) i].angle->setBounds (cell.removeFromTop (angleRow)
                                                .withTrimmedLeft (micSwitchW + gap));
    }
}

void CabinetBlock::paintContent (juce::Graphics& g)
{
    for (int i = 0; i < params::cabNumMics; ++i)
    {
        const auto sw = switchArea (i).toFloat();
        const float r = sw.getHeight() * 0.5f;
        const bool  on = slots[(size_t) i].on;
        const auto  tint = felitronics::appkit::brand::slotColours[(size_t) i];

        g.setColour (on ? tint.withAlpha (0.5f).overlaidWith (juce::Colour (0xff26262f))
                        : juce::Colour (0xff26262f));
        g.fillRoundedRectangle (sw, r);
        g.setColour (on ? tint : theme::hair2);
        g.drawRoundedRectangle (sw.reduced (0.5f), r, 1.0f);

        const float knob = sw.getHeight() - 4.0f;
        const float kx   = on ? sw.getRight() - knob - 2.0f : sw.getX() + 2.0f;
        g.setColour (on ? juce::Colours::white : juce::Colour (0xff8a8a96));
        g.fillEllipse (kx, sw.getCentreY() - knob * 0.5f, knob, knob);
    }
}

void CabinetBlock::mouseDown (const juce::MouseEvent& e)
{
    // The mic switches are edits — read-only while the block is off, and right-clicks belong to
    // the frame's power menu. The frame's own handler still runs so the power switch works.
    if (isBlockOn() && ! e.mods.isPopupMenu())
        for (int i = 0; i < params::cabNumMics; ++i)
        {
            if (switchArea (i).contains (e.getPosition()))
            {
                slots[(size_t) i].onAtt->setValueAsCompleteGesture (slots[(size_t) i].on ? 0.0f : 1.0f);
                return;
            }
        }

    BlockFrame::mouseDown (e);
}

} // namespace orbitamp
