// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CabinetBlock.h"

#include "../PluginProcessor.h"
#include "Prefs.h"

namespace orbitamp
{

CabinetBlock::CabinetBlock (AmpProcessor& processor)
    : BlockFrame ("Cab IR", BlockFrame::Kind::captured), amp (processor)
{
    startTimerHz (30);
    attachPower (*amp.apvts.getParameter (params::cabOn));

    // The IR's name on the border beside the block's, set like a name and in the block's colour.
    {
        juce::Array<VoicingSelector::Entry> entries;
        for (const auto& name : params::cabIrNames)
            entries.add ({ name, 0, false });
        ir.setEntries (std::move (entries));
    }
    ir.fontHeight = 16.0f;
    ir.tracking   = 0.15f;
    ir.boxed      = false;
    ir.tint       = theme::orange;
    addAndMakeVisible (ir);

    irAtt = std::make_unique<juce::ParameterAttachment> (
        *amp.apvts.getParameter (params::cabIr),
        [this] (float v)
        {
            const int i = juce::jlimit (0, params::cabIrNames.size() - 1, juce::roundToInt (v));
            ir.setSelection (i);
            loadWave (i);
            resized();   // the name on the border is as wide as the name
        });

    ir.onPick = [this] (int i) { irAtt->setValueAsCompleteGesture ((float) i); };

    // The picture: the IR, the cuts' curve and the trim's handle drawn on it, in the block's colour.
    wave.setAccent (theme::orange);
    wave.waveTint = theme::violet;   // the impulse itself in violet — the curve keeps the orange voice
    // The handle exists only in MANUAL — pushToWave keeps this current with the combo's mode.
    wave.setEqVisible (true);

    // The spectra behind the impulse, in the consoles' own hand: what walks into the IR as the
    // ground, what leaves it as the line — the same liquid columns, the same tilt, one renderer.
    wave.paintSpectrumUnder = [this] (juce::Graphics& g, juce::Rectangle<float> r)
    {
        if (! prefs::spectraShown())
            return;

        felitronics::analysis::PlotMap pm;
        pm.width      = r.getWidth();
        pm.height     = r.getHeight();
        pm.plotBottom = r.getHeight();
        pm.freqMin    = 20.0;
        pm.freqMax    = 20000.0;
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

        draw (panes[0], theme::spectrum, 0.14f, 0.02f, 0.30f);   // the door: the consoles' quiet ground
        draw (panes[1], theme::orange,   0.18f, 0.02f, 0.55f);   // the exit: the cabinet's own voice
    };

    addAndMakeVisible (wave);

    // A handle dragged on the picture writes its parameter; the parameter's echo redraws it.
    wave.onHpfChanged  = [this] (bool, float hz) { hpfHzAtt->setValueAsCompleteGesture (hz); };
    wave.onLpfChanged  = [this] (bool, float hz) { lpfHzAtt->setValueAsCompleteGesture (hz); };
    wave.onTrimChanged = [this] (float f)        { trimAtt->setValueAsCompleteGesture (f); };
    // The picture's right-click menu IS the trim combo's menu — one story, two doors.
    wave.onMenuRequested = [this]                { showTrimMenu(); };

    // The vertical half of a cut drag: the consoles' slope ladder, one notch per step.
    const auto stepSlope = [this] (const char* id, juce::ParameterAttachment& att, int steps)
    {
        auto* p = amp.apvts.getParameter (id);
        const int next = juce::jlimit (0, params::eqSlopes.size() - 1,
                                       juce::roundToInt (p->convertFrom0to1 (p->getValue())) + steps);
        att.setValueAsCompleteGesture ((float) next);
    };
    wave.onHpfSlopeStep = [this, stepSlope] (int n) { stepSlope (params::cabHpfSlope, *hpfSlopeAtt, n); };
    wave.onLpfSlopeStep = [this, stepSlope] (int n) { stepSlope (params::cabLpfSlope, *lpfSlopeAtt, n); };

    const auto follow = [this] (const char* id) -> std::unique_ptr<juce::ParameterAttachment>
    {
        return std::make_unique<juce::ParameterAttachment> (*amp.apvts.getParameter (id),
                                                            [this] (float) { pushToWave(); });
    };

    hpfHzAtt    = follow (params::cabHpfHz);
    lpfHzAtt    = follow (params::cabLpfHz);
    trimAtt     = follow (params::cabTrim);
    trimOnAtt   = follow (params::cabTrimOn);
    hpfSlopeAtt = follow (params::cabHpfSlope);
    lpfSlopeAtt = follow (params::cabLpfSlope);

    trimCombo.getText = [this]
    {
        switch (trimMode)
        {
            case TrimMode::fixed:  return "TRIM " + juce::String (juce::roundToInt (trimModeMs));
            case TrimMode::manual: return juce::String ("TRIM MAN");
            case TrimMode::off:
            default:               return juce::String ("TRIM OFF");
        }
    };
    trimCombo.onOpen = [this] { showTrimMenu(); };
    addAndMakeVisible (trimCombo);

    // The switches, each the truth of its parameter and nothing of its own.
    const char* ids[]    = { params::cabHpfOn, params::cabLpfOn, params::cabPhase };
    const char* names[]  = { "HPF", "LPF", "\xc3\x98" };

    for (size_t i = 0; i < switches.size(); ++i)
    {
        auto& s = switches[i];
        // Each cut wears its line's colour, the consoles' grammar — orange HPF, violet LPF;
        // the trim and the phase stay the block's own orange.
        s.sw.accent = ids[i] == params::cabLpfOn ? theme::violet : theme::orange;

        // The two CUTS stand on the panel; the PHASE does not. A cut is worked — you turn it on
        // and then drag its corner along the curve — so its switch belongs where the curve is.
        // The phase is set once a session and left, and a permanent seat for it cost the trim its
        // air. It keeps its object (the parameter attachment lives here, and the menu flips it
        // through this switch); it simply never appears.
        if (i == 2) addChildComponent (s.sw);
        else        addAndMakeVisible  (s.sw);

        s.label.setText (juce::String::fromUTF8 (names[i]), juce::dontSendNotification);
        s.label.setFont (theme::displayFont (12.0f));
        s.label.setColour (juce::Label::textColourId, theme::txDim);
        s.label.setJustificationType (juce::Justification::centredLeft);
        s.label.setInterceptsMouseClicks (false, false);

        if (i == 2) addChildComponent (s.label);
        else        addAndMakeVisible  (s.label);

        s.att = std::make_unique<juce::ParameterAttachment> (
            *amp.apvts.getParameter (ids[i]),
            [this, i] (float v)
            {
                switches[i].sw.setOn (v > 0.5f, false);
                pushToWave();
            });

        s.sw.onChange = [this, i] (bool on) { switches[i].att->setValueAsCompleteGesture (on ? 1.0f : 0.0f); };
        s.att->sendInitialUpdate();
    }

    irAtt->sendInitialUpdate();
    pushToWave();
}

CabinetBlock::~CabinetBlock() = default;

void CabinetBlock::loadWave (int index)
{
    const auto& bytes = AmpProcessor::cabIrBytes (index);
    wave.setFromMemory (bytes.data, (size_t) bytes.size);

    // A fixed window keeps its WORD across an IR swap: the parameter is a fraction of the shot's
    // length, so 50 ms of the old cab is not 50 ms of the new — re-assert the milliseconds.
    // A shot too short to hold the window shows all of itself and keeps the word anyway — see the
    // STILL OURS test in `deriveTrimMode`, which is what makes that survive the swap.
    if (trimMode == TrimMode::fixed && wave.lengthMs() > 0.0)
        trimAtt->setValueAsCompleteGesture (
            (float) juce::jlimit (0.001, 1.0, trimModeMs / wave.lengthMs()));

    pushToWave();
}

void CabinetBlock::pushToWave()
{
    // The PARAMETERS, not the atomics: an attachment callback runs before the atomic is written,
    // and reading it there leaves the picture exactly one change behind for good.
    const auto plain = [this] (const char* id)
    {
        auto* p = amp.apvts.getParameter (id);
        return p->convertFrom0to1 (p->getValue());
    };

    const auto slopeDb = [&] (const char* id)
    { return params::eqSlopeValues[juce::jlimit (0, (int) std::size (params::eqSlopeValues) - 1,
                                                 juce::roundToInt (plain (id)))]; };

    wave.setFilters (plain (params::cabHpfOn) > 0.5f, plain (params::cabHpfHz), params::cabHpfMinHz, params::cabHpfMaxHz,
                     plain (params::cabLpfOn) > 0.5f, plain (params::cabLpfHz), params::cabLpfMinHz, params::cabLpfMaxHz);
    wave.setSlopes (slopeDb (params::cabHpfSlope), slopeDb (params::cabLpfSlope));
    // The VALUE first, then the switch. Turning the trim on is what puts the handle under the view
    // window's rule, and that rule WRITES: `setTrimEnabled` calls `clampTrimToWindow`, which pulls
    // a handle standing outside the window onto its edge through `onTrimChanged` — a real gesture
    // on the parameter. Enabled first, the clamp judged the OLD fraction and could overwrite the
    // one arriving a line later.
    const bool trimOn = plain (params::cabTrimOn) > 0.5f;

    wave.setTrimFraction (plain (params::cabTrim));
    deriveTrimMode();

    // Coming ON from OUTSIDE — a preset, a register, an automation lane — the picture is still
    // wearing whatever window the last look left, and the clamp above would judge the recalled
    // length against it and write the window's edge over it. Give the window the mode's own answer
    // first and the clamp has nothing to pull. Only on the edge: setting it on every push would
    // fight the servo that zooms under a dragging hand. A menu pick sets its own window and holds
    // the mode, so it is not this code's business either.
    // ...and only once there IS a shot. Until the IR lands there is nothing to frame and nothing
    // worth remembering: an editor opening on a saved trim runs through here first with a length
    // of zero, and latching the switch's state there spent the edge on nothing — the picture then
    // stayed on the whole shot with the trim a sliver at the far edge, which is the very thing the
    // framing law exists to prevent.
    if (wave.lengthMs() > 0.0)
    {
        if (trimOn && ! trimWasOn && ! modeIsHeld)
            wave.frameTrim();

        trimWasOn = trimOn;
    }

    wave.setTrimEnabled (trimOn);

    wave.setTrimInteractive (trimMode == TrimMode::manual);

    trimCombo.repaint();
}

void CabinetBlock::deriveTrimMode()
{
    // Something already SAID what the mode is; re-deriving it from half-written values is how it
    // used to be lost. See `modeIsHeld`.
    if (modeIsHeld)
        return;

    auto* on = amp.apvts.getParameter (params::cabTrimOn);

    if (on->convertFrom0to1 (on->getValue()) < 0.5f)
    {
        trimMode = TrimMode::off;
        return;
    }

    // NOTHING TO DERIVE FROM is not a verdict. With no shot loaded there are no milliseconds to
    // compare: every mark misses, the answer is MANUAL, and MANUAL sticks. The editor opens that
    // way — the switches' initial updates all run through here before the IR arrives, since
    // `irAtt->sendInitialUpdate()` is the last of them — so a clean 100 MS used to come back as
    // TRIM MAN on every single open. Say nothing and wait for the shot.
    if (wave.lengthMs() <= 0.0)
        return;

    // An explicitly chosen MANUAL stays MANUAL even when the magnet lands the handle exactly on
    // a mark — a handle that vanished under the hand would be a bug wearing a rule's clothes.
    if (trimMode == TrimMode::manual)
        return;

    auto* tp = amp.apvts.getParameter (params::cabTrim);
    const double ms = (double) tp->convertFrom0to1 (tp->getValue()) * wave.lengthMs();

    // STILL OURS: the window already worn, measured on THIS shot. A fixed pick too long for a
    // short cab shows the whole thing, and the whole thing is not a mark — but the pick has not
    // changed, and reading it as MANUAL here is what used to lose it for good on the next
    // unrelated repaint, an HPF toggle being enough. Compare against the pick clamped to the shot
    // and a 500 ms window survives a 183 ms cab and comes back whole on the next long one.
    if (trimMode == TrimMode::fixed
        && std::abs (ms - juce::jmin (trimModeMs, wave.lengthMs())) < 1.0)
        return;

    for (const double mark : { 25.0, 50.0, 100.0, 200.0, 500.0 })
        if (std::abs (ms - mark) < 1.0)
        {
            trimMode   = TrimMode::fixed;
            trimModeMs = mark;
            return;
        }

    // A NUMBER NOBODY CLAIMED is a fixed window on its own number. It used to be read as MANUAL,
    // and that was the last place a mode could be invented rather than found: a session saved
    // mid-drag came back manual, the same session saved on a mark came back fixed, and which one
    // you got depended on where the hand happened to stop. The mode is not stored anywhere — the
    // two parameters are the whole truth — so the only honest reading is the one that says the
    // same thing every time.
    //
    // The combo shows the number, no window in the menu is ticked (none IS chosen), and the
    // handle waits for MANUAL to be asked for. One click, and the sound never moved: the value is
    // already exactly where it was saved.
    trimMode   = TrimMode::fixed;
    trimModeMs = ms;
}

/** The trim menu's ids: OFF, then one per fixed mark, then MANUAL well clear of them, and the
    block's own switches from 10 up. Named because the marks grow and the ones after them must
    not have to be renumbered by hand every time one does. */
static constexpr int itemOff = 1, itemFirstMark = 2, itemManual = 7;

void CabinetBlock::showTrimMenu()
{
    // The groups are NAMED. This menu carries the block's cuts and its phase as well, so a bare
    // OFF at the top has no owner and reads as the whole cabinet standing down — which is the
    // strip's arrow, not this. A header costs one row and says whose switch each one is.
    juce::PopupMenu m;
    // TRIM is ONE thing with a line through it, not two named things. Above the line, who decides
    // — nobody, or your own hand. Below it, the answer when neither does. A second header there
    // would promise a second subject; the line says "still the trim, other half" and costs a row
    // instead of a name.
    m.addSectionHeader ("TRIM");
    m.addItem (itemOff,    "OFF",    true, trimMode == TrimMode::off);
    m.addItem (itemManual, "MANUAL", true, trimMode == TrimMode::manual);
    m.addSeparator();

    const double marks[]  = { 25.0, 50.0, 100.0, 200.0, 500.0 };
    const char* labels[] = { "25 MS", "50 MS - DRY", "100 MS", "200 MS - WET", "500 MS" };

    // A window has to be shorter than the shot BY A MARGIN to be worth offering. The old test was
    // simply "shorter", so on a 501 ms cab the 500 ms window came alive to cut one millisecond
    // nobody can hear — a live item that does nothing is worse than a grey one, because grey at
    // least says why. A tenth of the shot is the least that reads as a cut.
    //
    // GREY, never gone: a window that does not fit this cabinet still exists, and a row that
    // vanishes says nothing about why. It is also the only way to see a window that IS chosen and
    // does not fit — carried over from a longer cab, it stands grey and ticked at once: this is
    // what is set, and here it does not go.
    constexpr double roomNeeded = 0.9;

    for (int i = 0; i < (int) std::size (marks); ++i)
        m.addItem (itemFirstMark + i, labels[i], marks[i] < wave.lengthMs() * roomNeeded,
                   trimMode == TrimMode::fixed && juce::approximatelyEqual (trimModeMs, marks[i]));

    // Only what has no face on the panel. The cuts stand out there in plain sight and answer to
    // their own switches; listing them again here would be a second door to a room already open,
    // and a menu that repeats the panel teaches nobody where anything lives.
    //
    // One item needs no heading over it — a heading for a single line is a heading that says the
    // line twice. Another rule, and the line names itself.
    m.addSeparator();
    m.addItem (10, juce::String::fromUTF8 ("\xc3\x98 INVERT PHASE"), true, switches[2].sw.isOn());

    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                     [safe = juce::Component::SafePointer<CabinetBlock> (this)] (int r)
                     {
                         if (safe != nullptr && r > 0)
                             safe->applyTrimPick (r);
                     });
}

void CabinetBlock::applyTrimPick (int itemId)
{
    // The switch section: flips through the switch, so the attachment writes the parameter and
    // the echo redraws face and picture alike.
    if (itemId >= 10)
    {
        switches[itemId == 10 ? 2 : itemId == 11 ? 0 : 1].sw.toggle();
        return;
    }

    // From here down the MODE is chosen, not derived: the two writes below echo back through
    // `pushToWave` as they land, and a half-written pair is no basis for reading a mode off.
    const juce::ScopedValueSetter<bool> held (modeIsHeld, true);

    // pushToWave runs LAST, unconditionally: a pick that writes a value the parameter already has
    // echoes nothing, and the combo and the handle would be left telling yesterday's story.
    //
    // ONE ORDER for all three: the VALUE, then the FRAME, then the SWITCH — and the switch last
    // because turning the trim on is what makes the window rule the handle. `setTrimEnabled` and
    // `setViewWindow` both call `clampTrimToWindow`, which pulls a handle standing outside the
    // window onto its edge AND WRITES THAT — a real gesture on the parameter, through
    // `onTrimChanged`. Turn the switch on while the old value and a stale window are still in
    // place and the clamp fires on numbers nobody asked for: it used to cost MANUAL its
    // remembered spot outright, and a fixed pick a spurious write into the host's automation.
    // With the value and the window already what the pick says, the clamp has nothing to pull.
    if (itemId == itemOff)
    {
        // Nothing is being cut, so the law has no trim to frame: the whole shot, which is what
        // there is to look at.
        trimMode = TrimMode::off;
        wave.setViewWindow (0.0);
        trimOnAtt->setValueAsCompleteGesture (0.0f);
    }
    else if (itemId == itemManual)
    {
        // MANUAL takes the trim WHERE IT STANDS. Coming off a fixed window the handle appears on
        // that window's own edge, so you carry on from what you are hearing — nothing jumps, and
        // there is no earlier place to be sent back to. Which also means the pick writes no value
        // at all: the number is already right, only the hand it answers to changes.
        trimMode = TrimMode::manual;

        wave.frameTrim();
        trimOnAtt->setValueAsCompleteGesture (1.0f);
    }
    else
    {
        const double marks[] = { 25.0, 50.0, 100.0, 200.0, 500.0 };
        trimMode   = TrimMode::fixed;
        trimModeMs = marks[itemId - itemFirstMark];

        if (wave.lengthMs() > 0.0)
            trimAtt->setValueAsCompleteGesture (
                (float) juce::jlimit (0.001, 1.0, trimModeMs / wave.lengthMs()));

        wave.frameTrim();
        trimOnAtt->setValueAsCompleteGesture (1.0f);
    }

    pushToWave();
}

void CabinetBlock::TrimCombo::paint (juce::Graphics& g)
{
    if (getText == nullptr)
        return;

    const auto r = getLocalBounds().toFloat();

    g.setColour (theme::txDim);
    theme::drawTracked (g, getText(), r.withTrimmedRight (11.0f), theme::displayFont (12.0f), 0.04f,
                        juce::Justification::centredLeft);

    // The chevron that says "this opens" — the consoles' SlopeCombo grammar.
    juce::Path v;
    const float cx = r.getRight() - 9.0f, cy = r.getCentreY() - 1.0f;
    v.startNewSubPath (cx - 3.0f, cy);
    v.lineTo (cx, cy + 3.0f);
    v.lineTo (cx + 3.0f, cy);
    g.strokePath (v, juce::PathStrokeType (1.2f));
}

void CabinetBlock::layOutContent (juce::Rectangle<int> area)
{
    // A narrow tile gives its border to the IR's name alone — the orange frame already says
    // what kind of block this is, and "CAB IR" was spending the name's room.
    showTitle = area.getWidth() >= 300;

    // The IR's name on the border, sized to itself and centred in the run.
    {
        const auto slot = borderSlotArea();
        ir.setBounds (slot.withSizeKeepingCentre (juce::jmin (slot.getWidth(), ir.idealWidth()), slot.getHeight()));
        borderSlotUsed = ir.getBounds();
    }

    // The bottom row: HPF holds the left wall and LPF the right — each cut on its own side of the
    // spectrum, the way they stand on the curve — with the trim combo centred between them. Three
    // things, evenly. The phase used to make it four, and a row of four turned the trim, the one
    // control here that is actually worked, into just another item in a list.
    //
    // A NARROW tile drops the cuts' words; the switches whisper them under the mouse instead.
    const bool narrow = area.getWidth() < 300;

    switches[0].label.setVisible (! narrow);
    switches[1].label.setVisible (! narrow);
    switches[0].sw.setTooltip (narrow ? "HPF" : juce::String());
    switches[1].sw.setTooltip (narrow ? "LPF" : juce::String());

    auto row = area.removeFromBottom (switchRow);
    area.removeFromBottom (gap);

    if (narrow)
    {
        switches[0].sw.setBounds (row.removeFromLeft (30).withSizeKeepingCentre (30, 16));
        switches[1].sw.setBounds (row.removeFromRight (30).withSizeKeepingCentre (30, 16));
    }
    else
    {
        const auto place = [] (Switch& sw, juce::Rectangle<int> cell)
        {
            sw.sw.setBounds (cell.removeFromLeft (30).withSizeKeepingCentre (30, 16));
            cell.removeFromLeft (6);
            sw.label.setBounds (cell);
        };

        place (switches[0], row.removeFromLeft (70));    // HPF
        place (switches[1], row.removeFromRight (70));   // LPF
    }

    trimCombo.setBounds (row.withSizeKeepingCentre (juce::jmin (90, row.getWidth() - 12),
                                                    row.getHeight()));

    wave.setBounds (area);
}

void CabinetBlock::paintContent (juce::Graphics&) {}

/** The consoles' feeding rule, twice: a fresh window into its pane, a starve when the audio
    stopped — then one repaint, and the hook above draws both. */
void CabinetBlock::timerCallback()
{
    if (! isBlockOn() || ! wave.isShowing())
        return;

    bool moved = false;

    for (int i = 0; i < 2; ++i)
    {
        auto& pane = panes[(size_t) i];
        int order = 0;

        if (amp.cabSpectrumTap[(size_t) i].tryPull (pane.frameInput(), order)
            && order == AmpProcessor::eqSpectrumOrder)
            pane.ingest (order);
        else
            pane.starve();

        moved = true;
    }

    if (moved)
        wave.repaint();
}

} // namespace orbitamp
