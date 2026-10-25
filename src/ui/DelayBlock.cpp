// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "DelayBlock.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"

namespace orbitamp
{

DelayBlock::DelayBlock (AmpProcessor& processor)
    : BlockFrame ("Delay", BlockFrame::Kind::dsp), amp (processor)
{
    startTimerHz (envTicksPerSecond);

    addAndMakeVisible (view);
    addAndMakeVisible (timeSel);
    addAndMakeVisible (mix);
    addAndMakeVisible (repeats);
    addAndMakeVisible (dark);
    addAndMakeVisible (offset);
    addAndMakeVisible (field);

    // TAP in the standalone alone: in a plug-in the host keeps the time.
    addChildComponent (tap);
    tap.setVisible (! amp.runsInHost());
    tap.onTap = [this] { tapTempo(); };

    attachPower (*amp.apvts.getParameter (params::delayOn));

    // The TIME on the border, where the captured blocks stand their combo: the sync ladder, and
    // FREE under a rule at the bottom for the player who wants milliseconds instead of a grid.
    {
        juce::Array<VoicingSelector::Entry> entries;
        for (const auto& name : params::delayDivisions)
            entries.add ({ name, 0, false });
        entries.add ({ "FREE", 0, true });
        timeSel.setEntries (std::move (entries));
    }
    timeSel.fontHeight = 12.0f;
    timeSel.tracking   = 0.10f;
    timeSel.boxed      = false;
    timeSel.tint       = theme::lilac;

    // MIX keeps its percent sign; REPS and DARK are bare numbers, and their hints say percent (Oleh, 24.10).
    mix.textForValue     = [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; };
    repeats.textForValue = [] (double v) { return juce::String (juce::roundToInt (v)); };
    dark.textForValue    = [] (double v) { return juce::String (juce::roundToInt (v)); };
    offset.textForValue  = [] (double v)
    {
        const int ms = juce::roundToInt (v);
        return (ms > 0 ? "+" : "") + juce::String (ms);
    };

    mix.setHint     ("MIX:   how loud the echoes ride over the dry, which stays untouched");
    repeats.setHint ("REPS:   how much of every echo comes back for another, in percent");
    dark.setHint    ("DARK:   how much darker every repeat comes back, in percent");
    offset.setHint  ("OFFSET:   one side's repeats held back against the other's, in ms: the delay's stereo");

    mix.setFader (true);

    for (auto* k : { &mix, &repeats, &dark, &offset })
    {
        k->labelFontHeight = Knob::fxLabelH;
        k->labelTracking   = Knob::fxLabelTracking;
        k->labelRowHeight  = Knob::fxLabelRowH;
        k->valueInDial     = false;
        k->setValueRowShown (true);
    }

    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        amp.apvts, params::delayMix, mix);
    repeatsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        amp.apvts, params::delayRepeats, repeats);
    darkAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        amp.apvts, params::delayDark, dark);
    offsetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        amp.apvts, params::delayOffset, offset);

    // Sync and division both redress the face; the field follows whichever number is conducting.
    syncAtt = std::make_unique<juce::ParameterAttachment> (
        *amp.apvts.getParameter (params::delaySync), [this] (float) { applyTimeMode(); });
    divAtt = std::make_unique<juce::ParameterAttachment> (
        *amp.apvts.getParameter (params::delayDiv), [this] (float) { applyTimeMode(); });
    bpmAtt = std::make_unique<juce::ParameterAttachment> (
        *amp.apvts.getParameter (params::delayBpm), [this] (float) { field.repaint(); });
    hostTempoAtt = std::make_unique<juce::ParameterAttachment> (
        *amp.apvts.getParameter (params::delayHostTempo), [this] (float) { applyTimeMode(); });
    timeAtt = std::make_unique<juce::ParameterAttachment> (
        *amp.apvts.getParameter (params::delayTimeMs), [this] (float) { field.repaint(); });

    timeSel.onOpen = [this] { showTimeMenu(); };

    field.text = [this]
    {
        // The number that CONDUCTS, whoever owns it: the host's tempo wears its name, since
        // dragging it here would move nothing.
        if (hostConducts())
            return "HOST " + juce::String (juce::roundToInt (amp.hostTempoBpm()));

        return syncOn() ? juce::String (juce::roundToInt (plain (params::delayBpm))) + " BPM"
                        : juce::String (juce::roundToInt (plain (params::delayTimeMs))) + " MS";
    };

    field.onDrag = [this] (int phase, float dy)
    {
        if (hostConducts())
            return;   // not ours to move — the menu gives the block its own BPM back

        auto& att = syncOn() ? *bpmAtt : *timeAtt;

        if (phase == 0)
        {
            dragStart = plain (syncOn() ? params::delayBpm : params::delayTimeMs);
            att.beginGesture();
        }
        else if (phase == 1)
            att.setValueAsPartOfGesture (dragStart + dy * (syncOn() ? 0.25f : 2.0f));
        else
            att.endGesture();
    };

    field.onTyped = [this] (const juce::String& typed)
    {
        if (hostConducts())
            return;

        if (const auto v = typed.getDoubleValue(); v > 0.0)
            (syncOn() ? *bpmAtt : *timeAtt).setValueAsCompleteGesture ((float) v);
    };

    view.paintBody = [this] (juce::Graphics& g, juce::Rectangle<float> r) { paintComb (g, r); };

    applyTimeMode();
}

DelayBlock::~DelayBlock()
{
    closeTapCount();
}

/** A TAP ON THE BUTTON. The first tap of a count opens one undo step, every tap after it writes the
    BPM the taps so far make, and the pause that ends the count closes the step (timerCallback) —
    everything one count did is one undo. */
void DelayBlock::tapTempo()
{
    if (! syncOn() || hostConducts())
        return;   // a free time has no beat to tap, and a conducted one is not ours to give

    const double now = juce::Time::getMillisecondCounterHiRes();

    if (! tapCount.isCounting (now))
    {
        closeTapCount();
        amp.history.beginGesture ("Tap tempo");
        tapGestureOpen = true;
    }

    if (const double bpm = tapCount.tap (now); bpm > 0.0)
        bpmAtt->setValueAsCompleteGesture (juce::jlimit (params::delayBpmMin, params::delayBpmMax,
                                                         (float) std::round (bpm)));
}

void DelayBlock::closeTapCount()
{
    if (! tapGestureOpen)
        return;

    tapGestureOpen = false;
    amp.history.endGesture();
}

void DelayBlock::timerCallback()
{
    // The tap's count closes on the pause, shown or not; its flash fades with the tick.
    if (tapGestureOpen && tapCount.closedBy (juce::Time::getMillisecondCounterHiRes()))
        closeTapCount();

    if (tap.flash > 0.0f)
    {
        tap.flash = tap.flash < 0.05f ? 0.0f : tap.flash * 0.8f;
        tap.repaint();
    }

    if (! view.isShowing())
        return;

    // The pulse's memory advances every tick, silence included — a quiet bucket is what lets
    // a note READ as a note when it arrives. A switched-off block feeds zeros: its line hears
    // nothing, so its comb goes still rather than replaying the last thing it saw.
    const float in = isBlockOn() ? amp.delayTaps().inputPeak() : 0.0f;

    // The host's tempo can move under a running session — a tempo track — and nothing calls us.
    if (const auto t = hostConducts() ? amp.hostTempoBpm() : 0.0f; ! juce::approximatelyEqual (t, shownHostTempo))
    {
        shownHostTempo = t;
        field.setMouseCursor (t > 0.0f ? juce::MouseCursor::NormalCursor : juce::MouseCursor::UpDownResizeCursor);
        field.repaint();
    }

    // Fast attack, slow release — the eye wants the hit now and the fade at its own pace.
    envSmooth = in > envSmooth ? in : envSmooth * 0.72f;

    envPos = (envPos + 1) % envHistSize;
    envHist[(size_t) envPos] = envSmooth;

    view.repaint();
}

void DelayBlock::paintComb (juce::Graphics& g, juce::Rectangle<float> r)
{
    // The ground: the dark well every picture stands in.
    g.setColour (juce::Colour (0xff101016));
    g.fillRoundedRectangle (r, theme::radiusSm);

    const auto& taps  = amp.delayTaps();
    const float timeT = juce::jmax (1.0f, taps.shownTimeMs());
    const float offMs = taps.shownOffsetMs();
    const float reps  = plain (params::delayRepeats) * 0.01f;
    const float darkA = plain (params::delayDark) * 0.01f;
    const float mixA  = plain (params::delayMix) * 0.01f;

    // The window: fixed at two seconds so turning TIME visibly spreads the teeth, growing
    // only when a long time would push the first repeats off the glass.
    const float winMs = juce::jmax (2000.0f, 3.5f * timeT);

    const auto plot = r.reduced (6.0f, 8.0f);
    const float base = plot.getBottom();

    // The dry hit at the left edge: the note entering the line, the grey the spectra use.
    {
        const float p = std::pow (juce::jlimit (0.0f, 1.0f, envHist[(size_t) envPos]), 0.4f);
        g.setColour (theme::spectrum.withAlpha (0.25f + 0.6f * p));
        g.fillRoundedRectangle (plot.getX(), base - plot.getHeight() * (0.25f + 0.2f * p),
                                2.0f, plot.getHeight() * (0.25f + 0.2f * p), 1.0f);
    }

    // The teeth: one per repeat. Height falls by REPEATS and stands on MIX; the ink dulls
    // along the tail by DARK — the filter made visible; OFFSET stands a lilac twin beside
    // every tooth. The pulse reads the envelope n repeats ago: the note walks the comb.
    const float heightScale = 0.30f + 0.65f * mixA;
    const juce::Colour dulled = theme::violet.interpolatedWith (juce::Colour (0xff3a3350), 0.85f);

    for (int n = 1; n <= 24; ++n)
    {
        const float h01 = std::pow (juce::jmax (0.0f, reps), (float) (n - 1)) * heightScale;
        const float x   = plot.getX() + plot.getWidth() * ((float) n * timeT / winMs);

        if (h01 < 0.02f || x > plot.getRight())
            break;

        // The bucket n repeats of the current time ago; older than the memory is simply quiet.
        const int   age   = juce::roundToInt ((float) n * timeT / 1000.0f * (float) envTicksPerSecond);
        const float env   = age < envHistSize ? envHist[(size_t) ((envPos - age + envHistSize) % envHistSize)] : 0.0f;
        const float pulse = std::pow (juce::jlimit (0.0f, 1.0f, env), 0.4f)
                              * std::pow (juce::jmax (0.0f, reps), (float) (n - 1));

        const float h    = plot.getHeight() * h01 * (1.0f + 0.18f * pulse);
        const auto  ink  = theme::violet.interpolatedWith (dulled,
                               juce::jlimit (0.0f, 1.0f, (float) n * darkA * 0.30f));

        const auto tooth = [&] (float tx, juce::Colour c, float w)
        {
            g.setColour (c);
            g.fillRoundedRectangle (tx - w * 0.5f, base - h, w, h, w * 0.5f);
        };

        // The lilac twin first, so the main tooth reads on top when the offset is small.
        if (std::abs (offMs) > 0.05f)
            tooth (x + plot.getWidth() * (offMs / winMs),
                   theme::lilac.withAlpha (0.30f + 0.45f * pulse), 2.0f);

        tooth (x, ink.withAlpha (0.55f + 0.45f * pulse), 2.5f);
    }

    g.setColour (theme::hair2);
    g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusSm, 1.0f);
}

bool DelayBlock::syncOn() const
{
    return plain (params::delaySync) > 0.5f;
}

bool DelayBlock::hostConducts() const
{
    return syncOn() && plain (params::delayHostTempo) > 0.5f && amp.hostTempoBpm() > 0.0f;
}

void DelayBlock::showTimeMenu()
{
    // The ladder, FREE under a rule, and — in a host — whose tempo the ladder counts in, under a
    // second rule and its own name: a division means nothing until someone says how long a beat is.
    enum : int { firstDiv = 1, itemFree = 100, itemHost = 101, itemOwn = 102 };

    const bool sync = syncOn();
    const int  div  = juce::jlimit (0, params::delayDivisions.size() - 1, juce::roundToInt (plain (params::delayDiv)));

    const auto itemFor = [] (const juce::String& name, int id, bool ticked, bool enabled = true)
    {
        juce::PopupMenu::Item item (name);
        item.itemID    = id;
        item.colour    = theme::lilac;
        item.isTicked  = ticked;
        item.isEnabled = enabled;
        return item;
    };

    juce::PopupMenu menu;

    for (int i = 0; i < params::delayDivisions.size(); ++i)
        menu.addItem (itemFor (params::delayDivisions[i], firstDiv + i, sync && div == i));

    menu.addSeparator();
    menu.addItem (itemFor ("FREE", itemFree, ! sync));

    if (amp.runsInHost())
    {
        const bool followsHost = plain (params::delayHostTempo) > 0.5f;
        const auto hostBpm     = amp.hostTempoBpm();

        // The host's line says what it is conducting at; a host that reports no tempo leaves the
        // choice standing but the own BPM playing, and the line says that too.
        menu.addSectionHeader ("TEMPO");
        menu.addItem (itemFor (hostBpm > 0.0f ? "HOST  " + juce::String (hostBpm, 1) + " BPM"
                                              : juce::String ("HOST  (no tempo)"),
                               itemHost, followsHost));
        menu.addItem (itemFor ("OWN  " + juce::String (juce::roundToInt (plain (params::delayBpm))) + " BPM",
                               itemOwn, ! followsHost));
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&timeSel),
                        [safe = juce::Component::SafePointer<DelayBlock> (this)] (int r)
                        {
                            if (safe == nullptr || r <= 0)
                                return;

                            if (r == itemFree)
                                safe->syncAtt->setValueAsCompleteGesture (0.0f);
                            else if (r == itemHost || r == itemOwn)
                                safe->hostTempoAtt->setValueAsCompleteGesture (r == itemHost ? 1.0f : 0.0f);
                            else
                            {
                                safe->divAtt->setValueAsCompleteGesture ((float) (r - firstDiv));
                                if (! safe->syncOn())
                                    safe->syncAtt->setValueAsCompleteGesture (1.0f);
                            }
                        });
}

float DelayBlock::plain (const char* id) const
{
    auto* p = amp.apvts.getParameter (id);
    return p->convertFrom0to1 (p->getValue());
}

void DelayBlock::applyTimeMode()
{
    timeSel.setSelection (syncOn() ? juce::jlimit (0, params::delayDivisions.size() - 1,
                                                   juce::roundToInt (plain (params::delayDiv)))
                                   : params::delayDivisions.size());
    field.repaint();

    tap.setEnabled (syncOn());
    tap.setTooltip (syncOn() ? "TAP: the block's own BPM, from the last few taps"
                             : "TAP counts a BPM, and the time is FREE");
    tap.repaint();

    resized();   // the pill on the border is as wide as its word
}

void DelayBlock::layOutContent (juce::Rectangle<int> area)
{
    // The time pill on the border, after the name — the captured blocks' combo slot.
    {
        const auto slot = borderSlotArea();
        timeSel.setBounds (slot.withWidth (juce::jmin (slot.getWidth(), timeSel.idealWidth())));
        borderSlotUsed = timeSel.getBounds();
    }

    // THE REVERB'S ARRANGEMENT, to the unit (ReverbBlock::layOutContent; Oleh, 24.10): MIX a fader the
    // box's whole height at the left, its number under it; at the right the three refinements in even
    // shares of a row, in the loop's own order — how many, how dark, how wide — each number tucked up
    // under its cap (Knob::valueLift); under them the conducting line, and under that the picture.
    mix.setBounds (area.removeFromLeft (Knob::faderW));
    area.removeFromLeft (Knob::faderGap);

    const int lift = Knob::snugLift (miniSide);
    auto minis = area.removeFromTop (Knob::fxLabelRowH + miniSide + Knob::valueRowH - lift);
    const int cell = minis.getWidth() / 3;

    for (auto* k : { &repeats, &dark, &offset })
        k->valueLift = lift;

    repeats.setBounds (minis.removeFromLeft (cell));
    dark.setBounds    (minis.removeFromLeft (cell));
    offset.setBounds  (minis);

    // ...one line: the conducting number, and TAP where there is no host to keep time.
    area.removeFromTop (3);
    auto line = area.removeFromTop (22);

    if (tap.isVisible())
    {
        tap.setBounds (line.removeFromRight (tap.idealWidth()));
        line.removeFromRight (4);
    }

    field.setBounds (line);

    // ...and the picture, the rest.
    area.removeFromTop (6);
    view.setBounds (area);

    for (juce::Component* c : { (juce::Component*) &repeats, (juce::Component*) &dark, (juce::Component*) &offset,
                                (juce::Component*) &field, (juce::Component*) &tap })
        c->toFront (false);
}

} // namespace orbitamp
