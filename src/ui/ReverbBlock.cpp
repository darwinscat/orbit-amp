#include "ReverbBlock.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"

#include <felitronics/analysis/PlotMap.h>

namespace orbitamp
{

namespace
{
    constexpr float kFMin = 20.0f, kFMax = 20000.0f;

    float xForFreq (juce::Rectangle<float> r, float f)
    {
        return r.getX() + r.getWidth() * std::log (f / kFMin) / std::log (kFMax / kFMin);
    }

    float freqForX (juce::Rectangle<float> r, float x)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (x - r.getX()) / juce::jmax (1.0f, r.getWidth()));
        return kFMin * std::pow (kFMax / kFMin, t);
    }
}

ReverbBlock::ReverbBlock (AmpProcessor& processor)
    : BlockFrame ("Reverb", BlockFrame::Kind::dsp), amp (processor)
{
    startTimerHz (30);

    addAndMakeVisible (character);
    addAndMakeVisible (view);
    addAndMakeVisible (mix);
    addAndMakeVisible (decay);
    addAndMakeVisible (pre);
    addAndMakeVisible (hpfSw);
    addAndMakeVisible (hpfLabel);

    attachPower (*amp.apvts.getParameter (params::reverbOn));

    // The character IS the block's name: PLATE or HALL on the border where REVERB stood, set like
    // a name and in the block's colour. A quarter-width block has no room for both, and the
    // colour already says which kind of block this is.
    showTitle = false;
    {
        juce::Array<VoicingSelector::Entry> entries;
        for (const auto& name : params::reverbCharacters)
            entries.add ({ name, 0, false });
        character.setEntries (std::move (entries));
    }
    character.fontHeight = 16.0f;
    character.tracking   = 0.15f;
    character.boxed      = false;
    character.tint       = theme::lilac;

    mix.textForValue   = [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; };
    decay.textForValue = [] (double v) { return juce::String (v, 1) + "x"; };
    pre.textForValue   = [] (double v) { return juce::String (juce::roundToInt (v)); };
    decay.labelFontHeight = 7.0f;
    pre.labelFontHeight   = 7.0f;

    characterAttachment = std::make_unique<juce::ParameterAttachment> (
        *amp.apvts.getParameter (params::reverbType),
        [this] (float v)
        {
            character.setSelection (juce::roundToInt (v));
            resized();   // the name on the border is as wide as the name
        });

    character.onPick = [this] (int i) { characterAttachment->setValueAsCompleteGesture ((float) i); };

    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        amp.apvts, params::reverbMix, mix);
    decayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        amp.apvts, params::reverbDecay, decay);
    preAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        amp.apvts, params::reverbPredelay, pre);

    // The tail's HPF: the consoles' cut, at home in the reverb — the switch wears the cut's
    // orange, the dashed vertical is the handle, the curve is what it does.
    hpfSw.accent = theme::orange;
    hpfSw.attach (*amp.apvts.getParameter (params::reverbHpfOn));

    hpfLabel.setFont (theme::displayFont (12.0f));
    hpfLabel.setColour (juce::Label::textColourId, theme::txDim);
    hpfLabel.setJustificationType (juce::Justification::centredLeft);
    hpfLabel.setInterceptsMouseClicks (false, false);

    hpfHzAtt = std::make_unique<juce::ParameterAttachment> (
        *amp.apvts.getParameter (params::reverbHpfHz), [this] (float) { view.repaint(); });
    hpfRepaintAtt = std::make_unique<juce::ParameterAttachment> (
        *amp.apvts.getParameter (params::reverbHpfOn), [this] (float) { view.repaint(); });

    const auto plain = [this] (const char* id)
    {
        auto* p = amp.apvts.getParameter (id);
        return p->convertFrom0to1 (p->getValue());
    };

    view.hitCut = [this, plain] (juce::Point<float> pos)
    {
        if (plain (params::reverbHpfOn) < 0.5f)
            return false;

        return std::abs (pos.x - xForFreq (view.getLocalBounds().toFloat(),
                                           plain (params::reverbHpfHz))) < 14.0f;
    };

    view.onCut = [this] (float x, int phase)
    {
        if (phase == 0) hpfHzAtt->beginGesture();

        auto* p = amp.apvts.getParameter (params::reverbHpfHz);
        const auto range = amp.apvts.getParameterRange (params::reverbHpfHz);
        juce::ignoreUnused (p);
        hpfHzAtt->setValueAsPartOfGesture (
            juce::jlimit (range.start, range.end,
                          freqForX (view.getLocalBounds().toFloat(), x)));

        if (phase == 2) hpfHzAtt->endGesture();
    };

    view.paintBody = [this, plain] (juce::Graphics& g, juce::Rectangle<float> r)
    {
        // The ground and its bones: a dark well and the decade lines.
        g.setColour (juce::Colour (0xff101016));
        g.fillRoundedRectangle (r, theme::radiusSm);

        for (const float f : { 100.0f, 1000.0f, 10000.0f })
        {
            const float x = xForFreq (r, f);
            g.setColour (juce::Colour (0x14ffffff));
            g.drawVerticalLine ((int) x, r.getY() + 2.0f, r.getBottom() - 2.0f);
            g.setColour (juce::Colour (0x55b0b0b0));
            g.setFont (theme::displayFont (8.5f));
            g.drawText (f < 1000.0f ? "100" : f < 10000.0f ? "1K" : "10K",
                        juce::Rectangle<float> (x + 3.0f, r.getBottom() - 13.0f, 30.0f, 11.0f),
                        juce::Justification::topLeft, false);
        }

        // The pair: the door as the quiet grey ground, the ADDED tail in the block's violet —
        // and the decay is how long the violet keeps glowing after the note.
        felitronics::analysis::PlotMap pm;
        pm.width      = r.getWidth();
        pm.height     = r.getHeight();
        pm.plotBottom = r.getHeight();
        pm.freqMin    = (double) kFMin;
        pm.freqMax    = (double) kFMax;
        pm.specTop    = 0.0;
        pm.specBottom = -90.0;

        const double fs = juce::jmax (8000.0, amp.currentSampleRate());

        const auto draw = [&] (felitronics::analysis::SpectrumPane& pane, juce::Colour tint,
                               float fillTop, float fillBottom, float line)
        {
            juce::Path fill, peak;
            fill.startNewSubPath (r.getX(), r.getBottom());
            bool first = true;

            pane.buildColumns (pm, fs, 4.5, 1000.0,
                               [&] (int, float x, float yFill, float yPeak)
                               {
                                   fill.lineTo (r.getX() + x, r.getY() + yFill);

                                   if (first) { peak.startNewSubPath (r.getX() + x, r.getY() + yPeak); first = false; }
                                   else       peak.lineTo (r.getX() + x, r.getY() + yPeak);
                               });

            fill.lineTo (r.getRight(), r.getBottom());
            fill.closeSubPath();

            g.setGradientFill (juce::ColourGradient (tint.withAlpha (fillTop),
                                                     0.0f, r.getY() + r.getHeight() * 0.30f,
                                                     tint.withAlpha (fillBottom),
                                                     0.0f, r.getBottom(), false));
            g.fillPath (fill);
            g.setColour (tint.withAlpha (line));
            g.strokePath (peak, juce::PathStrokeType (1.0f));
        };

        draw (panes[0], theme::spectrum, 0.14f, 0.02f, 0.30f);   // the door
        draw (panes[1], theme::violet,   0.20f, 0.02f, 0.65f);   // what the room adds

        // The tail's HPF, when it is on: the second-order curve and its dashed vertical — the
        // consoles' cut grammar, one Q, no ladder.
        if (plain (params::reverbHpfOn) > 0.5f)
        {
            const float fc = plain (params::reverbHpfHz);

            juce::Path curve;
            const int w = juce::jmax (2, (int) r.getWidth());
            for (int px = 0; px < w; ++px)
            {
                const float f  = freqForX (r, r.getX() + (float) px);
                const float rr = fc / juce::jmax (1.0f, f);
                const float db = -10.0f * std::log10 (1.0f + rr * rr * rr * rr);
                const float y  = r.getY() + r.getHeight() * juce::jlimit (0.0f, 1.0f, -db / 90.0f);
                if (px == 0) curve.startNewSubPath (r.getX(), y);
                else         curve.lineTo (r.getX() + (float) px, y);
            }

            g.setColour (theme::orange.withAlpha (0.8f));
            g.strokePath (curve, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));

            const float x = xForFreq (r, fc);
            const float dashes[] = { 5.0f, 4.0f };
            g.setColour (theme::orange.withAlpha (view.dragging ? 1.0f : 0.65f));
            g.drawDashedLine ({ x, r.getY() + 2.0f, x, r.getBottom() - 2.0f },
                              dashes, 2, view.dragging ? 2.2f : 1.4f);
        }

        g.setColour (theme::hair2);
        g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusSm, 1.0f);
    };
}

ReverbBlock::~ReverbBlock() = default;

void ReverbBlock::timerCallback()
{
    if (! isBlockOn() || ! view.isShowing())
        return;

    for (size_t i = 0; i < panes.size(); ++i)
    {
        int order = 0;

        if (amp.reverbSpectrumTap[i].tryPull (panes[i].frameInput(), order)
            && order == AmpProcessor::eqSpectrumOrder)
            panes[i].ingest (order);
        else
            panes[i].starve();
    }

    view.repaint();
}

void ReverbBlock::layOutContent (juce::Rectangle<int> area)
{
    // The character on the border where the name would stand: at the left, sized to itself.
    {
        const auto slot = borderSlotArea();
        character.setBounds (slot.withWidth (juce::jmin (slot.getWidth(), character.idealWidth())));
        borderSlotUsed = character.getBounds();
    }

    // The picture takes the whole box; everything else overlays it.
    view.setBounds (area);

    const int side = juce::jmin (maxKnobSide, juce::jmin (area.getWidth() / 2, area.getHeight() / 2 + 14));
    auto row = area.removeFromTop (side);
    mix.setBounds (row.removeFromRight (side).translated (-2, 2));
    mix.toFront (false);

    // The two refinements, deliberately small at the hero's left hand, on one centre line.
    const int mini = 42;
    auto miniRow = row.removeFromRight (mini * 2 + 6).withHeight (mini + 11)
                       .translated (0, (side - mini) / 2 - 4);
    decay.setBounds (miniRow.removeFromLeft (mini));
    miniRow.removeFromLeft (6);
    pre.setBounds (miniRow);
    decay.toFront (false);
    pre.toFront (false);

    // The HPF switch in the picture's lower-left corner, out of the tail's way.
    auto corner = area.removeFromBottom (22).removeFromLeft (76).reduced (6, 2);
    hpfSw.setBounds (corner.removeFromLeft (30).withSizeKeepingCentre (30, 16));
    corner.removeFromLeft (6);
    hpfLabel.setBounds (corner);
    hpfSw.toFront (false);
    hpfLabel.toFront (false);
}

} // namespace orbitamp
