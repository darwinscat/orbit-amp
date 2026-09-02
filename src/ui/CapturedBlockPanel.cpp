// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CapturedBlockPanel.h"

#include "../PluginProcessor.h"

#include <felitronics/appkit/WebpImage.h>

namespace orbitamp
{

/** The whole monitor, one picture: a kiosk component of its own with a fresh DeviceScope inside,
    black to the edges. A click anywhere or Escape hands the screen back. */
class CapturedBlockPanel::ScopeTheater final : public juce::Component
{
public:
    ScopeTheater (Block& b, std::function<double (double)> toneDb, DeviceScope::Mode mode,
                  bool waveHalf, double sampleRate,
                  felitronics::analysis::RollingSpectrumTap* tapIn, int tapOrder,
                  felitronics::appkit::DeviceSpec spec, juce::StringArray paper,
                  juce::StringArray credit, juce::Image picture,
                  std::function<void()> dismiss)
        : scope (b.scope, b.ribbon, std::move (toneDb)), onDismiss (std::move (dismiss))
    {
        scope.setMode (mode);
        scope.waveHalf = waveHalf;
        scope.setSampleRate (sampleRate);
        if (mode == DeviceScope::Mode::tone && tapIn != nullptr)
            scope.setSpectrumTap (tapIn, tapOrder);

        tap = mode == DeviceScope::Mode::tone ? tapIn : nullptr;

        // The moving pictures read a tap and need nothing carried in; the STATIC pages are all
        // carried-in content, and a fresh scope knows none of it. Without this the whole monitor
        // went black on the two pages that have the most to show on it.
        scope.setSpec (std::move (spec));
        scope.setInfo (std::move (paper));
        scope.setCredit (std::move (credit));
        scope.setPicture (std::move (picture));
        addAndMakeVisible (scope);
        setOpaque (true);
        setWantsKeyboardFocus (true);
    }

    /** The face's resolution request reaches the theatre's own scope too: it is the picture that
        owns the monitor, so it is the one the long window was raised for. */
    void setSpectrumOrder (int order)
    {
        if (tap != nullptr)
            scope.setSpectrumTap (tap, order);
    }

    void resized() override { scope.setBounds (getLocalBounds().reduced (24)); }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff060609)); }

    void mouseDown (const juce::MouseEvent&) override { if (onDismiss) onDismiss(); }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey && onDismiss)
        {
            onDismiss();
            return true;
        }
        return false;
    }

private:
    DeviceScope scope;
    felitronics::analysis::RollingSpectrumTap* tap = nullptr;   // null unless this is the TONE page
    std::function<void()> onDismiss;
};

CapturedBlockPanel::CapturedBlockPanel (AmpProcessor& processor, Block& b,
                                        const juce::String& title, const char* blockId,
                                        int eqLink,
                                        felitronics::analysis::RollingSpectrumTap& toneSpectrumTap,
                                        felitronics::analysis::RollingSpectrumTap& toneInSpectrumTap)
    : BlockFrame (title, BlockFrame::Kind::captured), amp (processor), block (b), blk (blockId),
      link (eqLink), toneTap (toneSpectrumTap),
      eq (processor.apvts, eqLink, toneSpectrumTap, toneInSpectrumTap,
          [&processor] { return processor.currentSampleRate(); }),
      inMeter ("IN", processor.blockInDb[(size_t) eqLink],
               *processor.apvts.getParameter (params::blockIn (blockId)), true),
      // The OUT wall's hand IS the console's LEVEL fader — one parameter, two doors: the fader
      // in the console's row and the grip on the wall move together, the attachment's echo
      // keeping them honest.
      outMeter ("OUT", eqLink == 0 ? processor.boostOutDb : processor.preampOutDb,
                *processor.apvts.getParameter (params::eqLevel (eqLink)), false)
{
    // The console's widgets become children of this block, which places them. The spectrum behind
    // its curve is the block's own output tap — the same one the TONE picture reads, because after
    // the chain rework they are the same point in the signal.
    eq.addTo (*this);

    addAndMakeVisible (device);
    addAndMakeVisible (gain);
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);

    device.fontHeight = 16.0f;   // the device's NAME, set exactly like the block's own beside it
    device.tracking   = 0.15f;
    device.boxed      = false;   // ...and nothing behind it, the frame opens the line under it

    // The hero wears no label — the whole square is dial — and says its name under the mouse:
    // the PACK's name for it, set when the device loads (Drive, Sustain, Fuzz, Overdrive...).
    // Its arc runs cold to hot, because that is what this one dial's position is.
    gain.labelRowHeight = 0;
    gain.heat = true;

    // Both walls stand up: IN at the block's left, OUT at its right.
    inMeter.vertical  = true;
    outMeter.vertical = true;

    // Five ways of showing the same device, one at a time. The tone curve comes from the block
    // itself — the same data its filters were designed from, resolved on the processor's pump.
    for (int i = 0; i < numViz; ++i)
    {
        scopes[(size_t) i] = std::make_unique<DeviceScope> (
            // THE PICTURE DRAWS WHAT THE BLOCK DOES, not half of it. It used to be handed the
            // device's measured curve alone, so in UNIVERSAL EQ — where that curve is parked — it
            // drew a flat line under a console showing a mountain: two descriptions of one signal,
            // and the tile's was of the half that was switched off. One function now, the same the
            // console's own curve calls.
            b.scope, b.ribbon, [this] (double hz) { return eq.drawnDb (hz); });
        scopes[(size_t) i]->setMode ((DeviceScope::Mode) i);
        scopes[(size_t) i]->setSampleRate (amp.currentSampleRate());
        if ((DeviceScope::Mode) i == DeviceScope::Mode::tone)
            scopes[(size_t) i]->setSpectrumTap (&toneTap, AmpProcessor::eqSpectrumOrder);

        // The picture takes no clicks of its own, so the right-click that changes it reaches this
        // block. Its corner glyphs are separate children and keep theirs.
        scopes[(size_t) i]->setInterceptsMouseClicks (false, false);
        addChildComponent (*scopes[(size_t) i]);
    }

    // What was up when the session was saved.
    vizPick = juce::jlimit (0, numViz - 1,
                            (int) amp.apvts.state.getProperty (vizProperty(), 0));

    for (int i = 0; i < numViz; ++i)
    {
        expandTags[(size_t) i].onClick = [this, i]
        {
            expandedViz = expandedViz == i ? -1 : i;
            for (int j = 0; j < numViz; ++j)
                expandTags[(size_t) j].expanded = expandedViz == j;
            applySpectrumResolution();
            resized();
            repaint();
        };
        addChildComponent (expandTags[(size_t) i]);
    }

    halfTag.onChange = [this]
    {
        scopes[(size_t) DeviceScope::Mode::wave]->waveHalf = halfTag.half;
        repaint();
    };

    startTimerHz (30);   // the hand's whereabouts, for the corner glyphs

    screenTag.onClick = [this] { openTheater(); };
    addChildComponent (screenTag);
    scopes[(size_t) DeviceScope::Mode::wave]->waveHalf = halfTag.half;   // half is the default
    addChildComponent (halfTag);

    // Whose bands the console wears. Written by the switch on the console and read back here, so a
    // restored session or an automating host moves the row too.
    eqModeAttachment = std::make_unique<juce::ParameterAttachment> (
        *amp.apvts.getParameter (params::blockEqMode (blk)),
        [this] (float) { applyEqMode(); resized(); repaint(); });

    eq.onReset = [this]
    {
        if (eq.wearsNative())
            resetToneSlotsToPackDefaults();
    };

    eq.onModePicked = [this] (int i)
    {
        eqModeAttachment->setValueAsCompleteGesture ((float) i);

        // ...and apply it here, because the attachment will NOT call back for its own write: it
        // sets `ignoreCallbacks` while it writes, so the lambda above never runs from a pick. The
        // parameter moved, the processor's pump read it and the SOUND changed — while the row went
        // on showing the other set until something else happened to rebuild it. Which reads as a
        // switch that misses the first click. The device combo two lines up already does exactly
        // this, for exactly this reason.
        applyEqMode();
        resized();
        repaint();
    };

    attachPower (*amp.apvts.getParameter (params::blockOn (blk)));

    // The attachment hears everyone BUT this panel — a restored session, a host automating the
    // parameter. Load the device it names before rebuilding the face, or the face is rebuilt from
    // the pack that is leaving; loading twice costs nothing because selectIfMoved is a no-op when
    // the pump already did it.
    deviceAttachment = std::make_unique<juce::ParameterAttachment> (
        *amp.apvts.getParameter (params::blockDevice (blk)),
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

        // A PICK, and only a pick. Every slot goes back to what this pack says it should be —
        // otherwise the new device arrives wearing the last one's settings, which are not even
        // about the same controls.
        //
        // Deliberately not done on every device change: a restored session moves this parameter
        // too, and resetting there would throw away exactly the settings the session was saved to
        // keep. A host automating the device index keeps whatever it is automating, which is what
        // automation means.
        resetToPackDefaults();

        deviceChanged();
    };

    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        amp.apvts, params::blockGain (blk), gain);

    // SMOOTH lit: the dial goes anywhere and the player mixes the neighbours by angle. Dark — STEP:
    // the dial itself refuses to stop between two captured positions, so what the hand feels is
    // what the player does.
    addAndMakeVisible (smoothTag);
    smoothAttachment = std::make_unique<juce::ParameterAttachment> (
        *amp.apvts.getParameter (params::blockSmooth (blk)),
        [this] (float v)
        {
            smoothTag.on = v > 0.5f;
            gain.snapToNotches = ! smoothTag.on;
            smoothTag.setTooltip (smoothTag.on
                ? "SMOOTH: between two captures both play, mixed by angle (2x CPU while between)"
                : "STEP: the dial lands on the captured positions; settled, one model runs");
            smoothTag.repaint();
        });
    smoothTag.onChange = [this]
    {
        smoothAttachment->setValueAsCompleteGesture (smoothTag.on ? 1.0f : 0.0f);
    };
    smoothAttachment->sendInitialUpdate();

    deviceAttachment->sendInitialUpdate();

    deviceChanged();
}

CapturedBlockPanel::~CapturedBlockPanel()
{
    closeTheater();
}

bool CapturedBlockPanel::foldPicture()
{
    if (expandedViz < 0)
        return false;

    expandedViz = -1;
    for (auto& t : expandTags)
        t.expanded = false;

    applySpectrumResolution();
    resized();
    repaint();
    return true;
}

void CapturedBlockPanel::openTheater()
{
    if (theater != nullptr)
        return;

    // Whatever is up — thrown across the face or sitting in its corner of the block. The theatre
    // used to require the expanded state first, which made it the second half of a gesture nobody
    // had a reason to start.
    const int shown = expandedViz >= 0 ? expandedViz : vizPick;

    const auto& from = *scopes[(size_t) shown];

    theater = std::make_unique<ScopeTheater> (
        block, [this] (double hz) { return eq.drawnDb (hz); },
        (DeviceScope::Mode) shown,
        halfTag.half, amp.currentSampleRate(),
        &toneTap, AmpProcessor::eqSpectrumOrder,
        from.deviceSpec(), from.paper(), from.creditLines(), from.picture(),
        [this] { closeTheater(); });

    // The face's own copy stops while the theatre runs — the wave ribbon must have ONE
    // resolution-setting reader at a time.
    scopes[(size_t) shown]->setVisible (false);

    theater->setBounds (juce::Desktop::getInstance().getDisplays()
                            .getPrimaryDisplay()->totalArea);
    theater->addToDesktop (juce::ComponentPeer::windowHasDropShadow);
    theater->setVisible (true);
    juce::Desktop::getInstance().setKioskModeComponent (theater.get(), false);
    theater->grabKeyboardFocus();
    applySpectrumResolution();
}

void CapturedBlockPanel::closeTheater()
{
    if (theater == nullptr)
        return;

    juce::Desktop::getInstance().setKioskModeComponent (nullptr);
    theater.reset();
    applySpectrumResolution();
    resized();   // the face's copy comes back
}

/** The long window is for the pictures that own the face. Anywhere else the console is pulling from
    the same tap and expects the standing size — a mixed order is not a compromise there, it is a
    starved reader, because a frame of the wrong size is discarded rather than resampled. */
void CapturedBlockPanel::applySpectrumResolution()
{
    const int shown = expandedViz >= 0 ? expandedViz : vizPick;
    const bool big  = (theater != nullptr || expandedViz >= 0)
                       && shown == (int) DeviceScope::Mode::tone;

    const int order = big ? AmpProcessor::eqSpectrumOrderBig : AmpProcessor::eqSpectrumOrder;

    amp.blockSpectrumOrder[(size_t) link].store (order, std::memory_order_relaxed);
    scopes[(size_t) DeviceScope::Mode::tone]->setSpectrumTap (&toneTap, order);

    if (theater != nullptr)
        theater->setSpectrumOrder (order);
}

void CapturedBlockPanel::deviceChanged()
{
    for (auto& sc : scopes)
        sc->setSampleRate (amp.currentSampleRate());

    const auto tones     = block.tones();
    const auto positions = block.gainPositions();

    // The list IS the combo: what a player has, greenest first. Nothing invented, nothing curated
    // into groups — the character ramp does the ordering a "type" heading used to.
    juce::Array<VoicingSelector::Entry> entries;
    bool sawUser = false;

    for (const auto& pack : block.packs)
    {
        VoicingSelector::Entry e;
        e.name = pack.displayName();
        e.character = pack.character;
        e.startsSection = ! pack.bundled && ! sawUser;   // the rule between shipped and added
        sawUser = sawUser || ! pack.bundled;
        entries.add (std::move (e));
    }

    device.setEntries (std::move (entries));

    caption = block.deviceName();

    // EVERY picture carries the circuit badge, not just the first. Five views of one device that
    // only sometimes say whose device it is send you back to the combo to be sure.
    const auto spec = felitronics::appkit::parseDeviceSpec (block.circuit());

    // ...and the DEVICE view carries the paper the badge has no room for.
    //
    // THE DEVICE, by the name the pack file carries — "IR-X Ch1", "Guitar Butler Clean" — the same
    // name the combo shows; the voice's alias, when the pack has one, on the line under it.
    auto* devParam = amp.apvts.getParameter (params::blockDevice (blk));
    const int chosen = juce::jlimit (0, juce::jmax (0, block.packs.size() - 1),
                                     juce::roundToInt (devParam->convertFrom0to1 (devParam->getValue())));

    juce::StringArray paper, credit;

    if (! block.packs.isEmpty())
    {
        const auto& pack = block.packs.getReference (chosen);
        paper.add (pack.displayName().toUpperCase());

        // The MAKER, not the voice's alias. «Snow Leopard» named the sound and told you nothing you
        // could act on; «Electro-Harmonix» tells you whose circuit this is, which is the fact a
        // player is actually checking when they open this page.
        if (const auto* stage = pack.rig.firstKnown())
            if (const auto make = juce::String (stage->make).trim(); make.isNotEmpty())
                paper.add (make.toUpperCase());

        // ...and on the card, the PARTICULAR BOX and the hands that took it.
        //
        // A model name is a family: "Big Muff Pi" has been a dozen circuits over fifty years, and
        // the year and the serial number are the only things that say which one played into the
        // microphone. Where it was drawn up and where it was screwed together is often the variant
        // itself — a Japanese TS808 and a Taiwanese one are different pedals under one name.
        if (const auto* stage = pack.rig.firstKnown())
        {
            juce::StringArray box;

            if (stage->year > 0)
                box.add (juce::String (stage->year));

            if (const auto sn = juce::String (stage->serialNumber).trim(); sn.isNotEmpty())
                box.add ("S/N " + sn.toUpperCase());

            if (! box.isEmpty())
                credit.add (box.joinIntoString (juce::String::fromUTF8 (" \xc2\xb7 ")));

            // One line, and it says only what it has: a box designed and built in the same place
            // says «MADE IN USA» rather than the same country twice.
            const auto designed = juce::String (stage->designedIn).trim().toUpperCase();
            const auto made     = juce::String (stage->madeIn).trim().toUpperCase();

            if (made.isNotEmpty() && (designed.isEmpty() || designed == made))
                credit.add ("MADE IN " + made);
            else if (made.isNotEmpty())
                credit.add ("DESIGNED IN " + designed
                            + juce::String::fromUTF8 (" \xc2\xb7 ") + "MADE IN " + made);
            else if (designed.isNotEmpty())
                credit.add ("DESIGNED IN " + designed);
        }

        // A captured device with no maker named is an anonymous file. This one is ours and says so.
        if (const auto by = juce::String (pack.rig.modeledBy).trim(); by.isNotEmpty())
            credit.add ("CAPTURED BY " + by.toUpperCase());
    }

    if (const auto circuit = juce::String (block.circuit()).trim(); circuit.isNotEmpty())
        paper.add (circuit.toUpperCase().replace (",", juce::String::fromUTF8 (" \xc2\xb7 ")));

    if (const int n = block.gainPositions().size(); n > 0)
        paper.add (juce::String (n) + (n == 1 ? " CAPTURE" : " CAPTURES") + " ON THE DIAL");

    // ...and the BOX itself, when the pack ships one. `picture` is a file name in the pack root
    // (see namz's Rig::picture) — WebP, a cut-out with its alpha kept. Decoded HERE, once per
    // device change on the message thread: ~4 ms for a 600-square, and the view keeps the result
    // rather than touching the codec again.
    //
    // WebP is what the format states and what the capture app writes; the fallback exists because a
    // hand-assembled pack naming a PNG is a reasonable thing to do and refusing it would be pedantry.
    juce::Image photo;

    if (! block.packs.isEmpty())
    {
        const auto& pack = block.packs.getReference (chosen);

        if (const auto entry = juce::String (pack.rig.picture).trim(); entry.isNotEmpty())
            if (const auto bytes = device::DeviceLibrary::readBinaryEntry (pack, entry);
                bytes.getSize() > 0)
            {
                photo = felitronics::appkit::decodeWebp (bytes.getData(), bytes.getSize());

                if (! photo.isValid())
                    photo = juce::ImageFileFormat::loadFrom (bytes.getData(), bytes.getSize());
            }
    }

    for (auto& sc : scopes)
    {
        sc->setSpec (spec);
        sc->setInfo (paper);
        sc->setCredit (credit);
        sc->setPicture (photo);
    }

    // A device with no photograph must not leave the face staring at an empty page — the one the
    // last device filled. The paper is what every pack has, so that is where it steps back to.
    if (! hasPicture() && (vizPick == (int) DeviceScope::Mode::photo
                            || expandedViz == (int) DeviceScope::Mode::photo))
        setViz ((int) DeviceScope::Mode::device);

    // The gain knob's detents ARE the captured positions. Twenty-one for SM7, whatever the next
    // pack says for the next one. Detented where the pack has positions, continuous where it does
    // not — but always THERE: a knob with nothing to select drives instead, and a device without a
    // gain knob is not a device. Its name is the device's own for that dial.
    gain.setNotches (juce::jmax (0, positions.size()));
    gain.setTooltip (block.dialName().isNotEmpty() ? block.dialName().toUpperCase() : juce::String ("GAIN"));

    for (int i = 0; i < params::boostNumMeasured; ++i)
    {
        auto& slot = slots[(size_t) i];

        // ORDER MATTERS, and `slot = Slot {}` had it backwards. Move-assignment walks the members in
        // declaration order, so the knob was destroyed first and its attachment — which reaches for
        // the slider in its own destructor — went next, into freed memory. Changing a device crashed
        // the plugin, and it only showed once a second pack made the combo worth using.
        slot.knobAtt.reset();
        slot.stepAtt.reset();
        slot.knob.reset();
        slot.steps.reset();
        slot.measuredIndex = -1;

        // A swept tone knob is a band of the console (nativeBands), not a knob on the face. A tone
        // SWITCH — SM7's Sharp/Smooth, a V4's bass cut — has no place on the face yet and stays at
        // its default until the console's row learns to hold one.
    }

    buildSelectors();
    applyEqMode();

    resized();
    repaint();
}

/** A device's own tone controls, as bands of the console.

    Only the SWEPT ones. A measured control whose positions carry names — SM7's Sharp/Smooth — is a
    switch rather than a dial, and a two-detent nameless knob in a row of dials would throw both its
    names away. It keeps the place it already has until the row learns to hold a switch.

    The notch count is the number of positions the pack actually swept: between them there is
    interpolation, not data, and the detents say so on the dial. */
std::vector<EqSection::Band> CapturedBlockPanel::nativeBands() const
{
    std::vector<EqSection::Band> out;

    const auto tones = block.tones();

    for (int i = 0; i < params::boostNumMeasured && i < (int) tones.size(); ++i)
    {
        const auto& m = tones[(size_t) i];

        // A SWITCH — no sweep, or two named positions — is not a band: the row holds dials.
        if (m.sweep <= 0 || (m.positions.size() == 2 && Block::namedPositions (m)))
            continue;

        // A control the pack tested and found NOT to be a filter anywhere gets no knob. namz says
        // so with a collapsed trusted band, and the player honours it by playing silence — so a
        // dial here would turn, look alive, and do nothing at all. That is worse than a gap.
        if (Block::trustFailed (m))
            continue;

        // A measured ladder clicks at the positions it was swept at; a knob shipped as bands has
        // a law for every angle and sweeps freely.
        auto band = EqSection::Band { juce::String (m.name).toUpperCase(),
                                      theme::eqNode[(size_t) (out.size() % 5)],
                                      params::blockMeasured (blk, i),
                                      (int) m.positions.size(),
                                      false };

        // The knob's point on the curve: where it acts hardest, from the manifest alone — the
        // extremes of its measured travel diffed on the shipped grid, or the strongest section's
        // centre. Fixed per device, so the dot stands still while the knob rides the curve.
        if (m.positions.size() >= 2 && m.grid.points > 1
            && (int) m.positions.front().db.size() == m.grid.points
            && (int) m.positions.back().db.size() == m.grid.points)
        {
            const auto& lo = m.positions.front();
            const auto& hi = m.positions.back();
            int    bestIdx   = 0;
            double bestSwing = 0.0;

            for (int p = 0; p < m.grid.points; ++p)
            {
                // db[] already CARRIES the position's broadband level (level_db is a derived
                // annotation, equal to the curve's 80 Hz-12 kHz mean) — adding it again was a
                // double count that inflated every swing by the knob's whole loudness ride.
                const double swing = hi.db[(size_t) p] - lo.db[(size_t) p];
                if (std::abs (swing) > std::abs (bestSwing)) { bestSwing = swing; bestIdx = p; }
            }

            band.anchorHz      = m.grid.fLo * std::pow (m.grid.fHi / m.grid.fLo,
                                                        (double) bestIdx / (double) (m.grid.points - 1));
            band.anchorSwingDb = bestSwing;
        }
        else if (! m.sections.empty())
        {
            const auto* best = &m.sections.front();
            for (const auto& sec : m.sections)
                if (std::abs (sec.dbAtMax - sec.dbAtMin) > std::abs (best->dbAtMax - best->dbAtMin))
                    best = &sec;

            if (best->hz > 0.0)
            {
                band.anchorHz      = best->hz;
                band.anchorSwingDb = best->dbAtMax - best->dbAtMin;
            }
        }

        out.push_back (std::move (band));
    }

    return out;
}

void CapturedBlockPanel::applyEqMode()
{
    const auto native = nativeBands();
    // The PARAMETER, not the apvts atomic. Called from an attachment callback — a host automating
    // this, a session restoring — the atomic has not been written yet at that moment, and reading
    // it there leaves the face exactly one change behind for good.
    auto* modeParam  = amp.apvts.getParameter (params::blockEqMode (blk));
    const int  want  = juce::roundToInt (modeParam->convertFrom0to1 (modeParam->getValue()));

    // A device that measured nothing has no native set, so the choice collapses to ours on its own
    // rather than showing an empty row and asking the player to work out why.
    const bool wearsNative = native.empty() ? false
                                            : want == (int) params::EqMode::native;

    eq.setModes (params::eqModes, wearsNative ? (int) params::EqMode::native : (int) params::EqMode::ours,
                 ! native.empty());

    if (wearsNative)
    {
        eq.setBands (native);
        eq.setNativeCurve ([this] (double hz) { return block.toneDb (hz); });
    }
    else
    {
        eq.setBands (eq.ourBandSet());
        eq.setNativeCurve (nullptr);
    }
}

void CapturedBlockPanel::resetToPackDefaults()
{
    const auto write = [this] (const juce::String& id, float plain)
    {
        if (auto* p = amp.apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (plain));
            p->endChangeGesture();
        }
    };

    // The measured slots. A pack names where its knob starts — and it is NOT the reference
    // position, which is usually an end of travel and therefore the wrong thing to reach for. An
    // unclaimed slot goes to the middle, which is what it means when nothing is behind it.
    resetToneSlotsToPackDefaults();

    // ...and the selecting ones, which have the same problem for the same reason: slot one is an
    // octave switch on one device and something else entirely on the next.
    const auto list = block.selectors();

    for (int i = 0; i < params::numSelectors; ++i)
        write (params::selectorId (blk, i),
               (float) (i < list.size() ? list.getReference (i).defaultIndex : 0));
}

void CapturedBlockPanel::resetToneSlotsToPackDefaults()
{
    const auto tones = block.tones();

    for (int i = 0; i < params::boostNumMeasured; ++i)
        if (auto* p = amp.apvts.getParameter (params::blockMeasured (blk, i)))
        {
            const float plain = (float) (i < (int) tones.size() ? Block::defaultNorm (tones[(size_t) i]) : 0.5);
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (plain));
            p->endChangeGesture();
        }
}

void CapturedBlockPanel::buildSelectors()
{
    const auto list = block.selectors();

    for (int i = 0; i < params::numSelectors; ++i)
    {
        auto& sel = selectors[(size_t) i];

        sel.attachment.reset();
        sel.steps.reset();

        if (i >= list.size())
            continue;

        const auto& def = list.getReference (i);

        juce::StringArray labels;
        for (const auto& v : def.values)
            labels.add (v.toUpperCase());

        sel.name   = def.name.toUpperCase();
        sel.values = labels;

        sel.steps = std::make_unique<VSwitch>();
        sel.steps->accent = theme::orange;

        // Lying down under the GAIN dial, one line high: the slot and its lever alone — the name
        // and the chosen position are written beside it by the panel, and the whole list is the
        // hint. Names under the detents cost a second line, and the dial's diameter paid for it.
        sel.steps->setHorizontal (true);
        sel.steps->setNamesShown (false);
        sel.steps->setItems (labels, 0);
        sel.steps->setTooltip (labels.joinIntoString (juce::String::fromUTF8 ("   \xc2\xb7   ")));
        addAndMakeVisible (*sel.steps);

        sel.attachment = std::make_unique<juce::ParameterAttachment> (
            *amp.apvts.getParameter (params::selectorId (blk, i)),
            [this, i, n = labels.size()] (float v)
            {
                if (auto* s = selectors[(size_t) i].steps.get())
                    s->setSelectedIndex (juce::jlimit (0, n - 1, juce::roundToInt (v)),
                                         juce::dontSendNotification);
                repaint();   // the chosen position's name is the panel's to write
            });

        sel.steps->onChange = [this, i] (int v)
        {
            selectors[(size_t) i].attachment->setValueAsCompleteGesture ((float) v);
        };

        sel.attachment->sendInitialUpdate();
    }
}

void CapturedBlockPanel::blockOnChanged (bool on)
{
    inMeter.live = on;
    inMeter.repaint();
    outMeter.live = on;
    outMeter.repaint();
}

void CapturedBlockPanel::layOutContent (juce::Rectangle<int> area)
{
    // The device combo stands ON the top border, between the block's name and its switch — the row
    // it used to take inside the box is the height the block gave back. Sized to the name and
    // centred in the run, so the line shows on both sides of it as it does beside the block's own.
    {
        const auto slot = borderSlotArea();
        device.setBounds (slot.withSizeKeepingCentre (juce::jmin (slot.getWidth(), device.idealWidth()),
                                                      slot.getHeight()));
        borderSlotUsed = device.getBounds();   // the frame opens the line under it
    }

    // A thrown-open tile owns the WHOLE face: every control steps aside until the fold glyph
    // brings the room back.
    if (expandedViz >= 0)
    {
        setControlsVisible (false);
        eq.setWidgetsVisible (false);

        for (int i = 0; i < numViz; ++i)
        {
            scopes[(size_t) i]->setVisible (i == expandedViz);
            expandTags[(size_t) i].setVisible (i == expandedViz);
        }

        // The whole face is the picture, so the whole face takes the click that walks the loop.
        widgetArea = area;
        scopes[(size_t) expandedViz]->setBounds (area);

        auto corner = area.removeFromTop (24).removeFromRight (96);
        expandTags[(size_t) expandedViz].setBounds (corner.removeFromRight (28).reduced (2, 4)
                                                        .translated (-6, 4));

        screenTag.setBounds (corner.removeFromRight (28).reduced (2, 4).translated (-6, 4));
        screenTag.setVisible (true);
        screenTag.toFront (false);

        halfTag.setVisible (expandedViz == (int) DeviceScope::Mode::wave);
        if (halfTag.isVisible())
        {
            halfTag.setBounds (corner.removeFromRight (30).reduced (1, 4).translated (-6, 4));
            halfTag.toFront (false);
        }

        expandTags[(size_t) expandedViz].toFront (false);
        return;
    }

    setControlsVisible (true);
    eq.setWidgetsVisible (true);

    // ---- the IN meter runs the block's WHOLE height, at its left wall — beside the console
    //      too: what the model is fed is the block's first fact, and now it reads at any
    //      glance. The component is wider than its bar; the margin is where the dB appears
    //      while the hand drags, click-through for the console beneath it otherwise. ----
    {
        auto meterCol = area.removeFromLeft (BlockMeter::designWidth);
        area.removeFromLeft (6);
        inMeter.setBounds (meterCol.withWidth (BlockMeter::designWidth + BlockMeter::standingValueW));
        inMeter.toFront (false);   // over the console, so the dragged reading paints on top

        auto outCol = area.removeFromRight (BlockMeter::designWidth);
        area.removeFromRight (6);
        outMeter.valueOnLeft = true;
        outMeter.setBounds (outCol.withX (outCol.getX() - BlockMeter::standingValueW)
                                  .withWidth (BlockMeter::designWidth + BlockMeter::standingValueW));
        outMeter.toFront (false);   // the mirrored reading paints over the console's right edge
    }

    // ---- the top zone is CAPPED and the console gets the remainder, not the other way round.
    //
    //      A dial and a picture stop improving once they are big enough to aim at; a curve does
    //      not — every unit of height it gets is another decibel you can tell apart. So the zone
    //      that saturates is the one with the ceiling on it. ----
    auto top = area.removeFromTop (juce::jmin (topZoneH, area.getHeight() - EqSection::rowH - 90));
    area.removeFromTop (gap);
    eq.layOut (area);

    // ---- inside it: a column of FIXED width carrying the big GAIN with the device's own
    //      selectors under it, and one picture taking everything to its right.
    //
    //      Fixed, because the picture must not move when the device changes. Fur Coat brings an
    //      octave switch and a Big Muff brings nothing, and if the column grew to fit them the
    //      whole right-hand side would shuffle sideways every time the combo was touched. Only the
    //      GAIN dial's diameter answers for what the switches take. ----
    auto column = top.removeFromLeft (columnW);
    top.removeFromLeft (gap);

    // Only the SELECTING controls stand here — the ones that pick a capture. A measured control is
    // a band of the curve and belongs in the console's row, whatever shape its own control has.
    // Each takes ONE line off the bottom — name, lever, chosen position — and GAIN gets the rest,
    // so a device with nothing to switch simply wears a bigger dial, and nothing else on the face
    // moves.
    for (auto& sel : selectors)
    {
        if (sel.steps == nullptr)
            continue;

        sel.row = column.removeFromBottom (pickRowH);
        column.removeFromBottom (gap);
        sel.steps->setBounds (sel.row.withX (sel.row.getX() + pickNameW).withWidth (pickSwitchW));
    }

    const int gainSide = juce::jmin (maxGainSide, juce::jmin (column.getWidth(), column.getHeight()));
    gain.setBounds (column.withSizeKeepingCentre (gainSide, gainSide));

    // The pill stands in the gap at the bottom of the dial's arc — the one place inside the dial's
    // square that neither the ring nor a notch reaches — so it reads as the dial's own caption.
    smoothTag.setBounds (juce::Rectangle<int> (66, 14).withCentre ({ gain.getBounds().getCentreX(),
                                                                     gain.getBottom() - 6 }));
    smoothTag.toFront (false);

    layWidget (top);
}

void CapturedBlockPanel::setControlsVisible (bool v)
{
    gain.setVisible (v);
    smoothTag.setVisible (v);
    inMeter.setVisible (v);
    outMeter.setVisible (v);
    device.setEnabled (v);

    for (auto& slot : slots)
    {
        if (slot.knob != nullptr)  slot.knob->setVisible (v);
        if (slot.steps != nullptr) slot.steps->setVisible (v);
    }

    for (auto& sel : selectors)
        if (sel.steps != nullptr)
            sel.steps->setVisible (v);

}

/** One picture, filling what is left of the top zone, wearing its own corner glyphs. */
void CapturedBlockPanel::layWidget (juce::Rectangle<int> area)
{
    for (int i = 0; i < numViz; ++i)
        scopes[(size_t) i]->setVisible (i == vizPick);

    widgetArea = area;
    scopes[(size_t) vizPick]->setBounds (area);

    for (auto& t : expandTags)
        t.setVisible (false);

    // BOTH ways out, always. The whole-screen brackets used to appear only once a picture had
    // already been thrown across the face — a door you can only find from inside the room.
    int right = area.getRight() - 6;

    expandTags[(size_t) vizPick].setBounds (right - 22, area.getY() + 4, 22, 16);
    expandTags[(size_t) vizPick].setVisible (true);
    expandTags[(size_t) vizPick].toFront (false);
    right -= 26;

    screenTag.setBounds (right - 22, area.getY() + 4, 22, 16);
    screenTag.setVisible (true);
    screenTag.toFront (false);
    right -= 26;

    const bool isWave = vizPick == (int) DeviceScope::Mode::wave;
    halfTag.setVisible (isWave);
    if (isWave)
    {
        halfTag.setBounds (right - 28, area.getY() + 4, 28, 16);
        halfTag.toFront (false);
    }
}

/** A right-click on the PICTURE picks which picture; anywhere else on the block it still means the
    power menu, which is the one edit that must never need aiming for. */
void CapturedBlockPanel::mouseDown (const juce::MouseEvent& e)
{
    // A selecting control's line answers on its whole length: the lever goes where it is clicked,
    // and a click on the name or on the chosen position — the two words either side of it — steps
    // to the next position round the loop, so a two-way switch is a click wherever the eye lands.
    if (expandedViz < 0 && ! e.mods.isPopupMenu())
        for (auto& sel : selectors)
            if (sel.steps != nullptr && sel.row.contains (e.getPosition())
                && ! sel.steps->getBounds().contains (e.getPosition()))
            {
                sel.steps->setSelectedIndex ((sel.steps->selectedIndex() + 1) % juce::jmax (1, sel.steps->count()));
                return;
            }

    if (widgetArea.contains (e.getPosition()))
    {
        if (e.mods.isPopupMenu())
            showVizMenu (e.getScreenPosition());
        else
            setViz (nextViz (vizPick));   // round the loop, one click at a time

        return;
    }

    BlockFrame::mouseDown (e);
}

/** Whether the pointer is over the picture — asked of the desktop, so it is also false when the
    mouse has left the window, which no enter/exit pair would have told us. */
bool CapturedBlockPanel::handIsOnThePicture() const
{
    if (widgetArea.isEmpty() || ! isShowing())
        return false;

    return widgetArea.contains (getMouseXYRelative());
}

void CapturedBlockPanel::setCornerAlpha (float a)
{
    cornerAlpha = a;

    const bool lit = a > 0.01f;

    for (auto& t : expandTags)
        if (t.isVisible() || lit)
            t.setAlpha (a);

    screenTag.setAlpha (a);
    halfTag.setAlpha (a);

    // Transparent is still clickable in JUCE, and a glyph nobody can see must not answer a click
    // meant for the picture underneath it.
    for (auto& t : expandTags)
        t.setInterceptsMouseClicks (lit, lit);

    screenTag.setInterceptsMouseClicks (lit, lit);
    halfTag.setInterceptsMouseClicks (lit, lit);
}

void CapturedBlockPanel::timerCallback()
{
    // The theatre owns the whole screen and its own way out; while it runs the face beneath is not
    // being pointed at, whatever the coordinates say.
    const bool want = theater == nullptr && handIsOnThePicture();
    const float step = want ? 1.0f / (0.25f * 30.0f) : -1.0f / (0.5f * 30.0f);
    const float next = juce::jlimit (0.0f, 1.0f, cornerAlpha + step);

    if (next != cornerAlpha)
        setCornerAlpha (next);
}

bool CapturedBlockPanel::hasPicture() const
{
    const auto& sc = scopes[(size_t) DeviceScope::Mode::photo];
    return sc != nullptr && sc->hasPicture();
}

/** Only the SHOWN scope is given bounds — the others keep whatever they had last — so the room is
    asked of the one actually standing in it. They all answer the same when they are the same size,
    which is the point: the question is about the room, not about which page is up. */
bool CapturedBlockPanel::pairsAsCard() const
{
    const auto& sc = scopes[(size_t) (expandedViz >= 0 ? expandedViz : vizPick)];
    return sc != nullptr && sc->pairsAsCard();
}

/** The loop walks PAGES THAT HAVE SOMETHING OF THEIR OWN TO SHOW.

    Two reasons a page steps out of it. Most packs ship no photograph — the field is newer than they
    are — and a click that lands on a blank page is a click the player has to undo. And wherever the
    room seats the pair, DEVICE and PHOTO draw the SAME card, so a loop that visited both would stop
    twice on one picture and read as a stutter. DEVICE is the one that survives the pair: every pack
    has paper, and only some have a photograph. */
int CapturedBlockPanel::nextViz (int from) const
{
    for (int i = 1; i <= numViz; ++i)
    {
        const int j = (from + i) % numViz;

        if (j == (int) DeviceScope::Mode::photo && (! hasPicture() || pairsAsCard()))
            continue;

        return j;
    }

    return from;
}

/** Both ways of choosing land here, so the choice is remembered whichever was used. */
void CapturedBlockPanel::setViz (int which)
{
    vizPick = juce::jlimit (0, numViz - 1, which);

    // Thrown open, the loop walks the big picture rather than the small one behind it — otherwise
    // clicking a full-face view changes something you cannot see.
    if (expandedViz >= 0)
    {
        expandedViz = vizPick;
        for (int j = 0; j < numViz; ++j)
            expandTags[(size_t) j].expanded = expandedViz == j;
    }

    // Kept with the session rather than as a parameter: it is which picture you are looking at,
    // not something a host should be automating.
    amp.apvts.state.setProperty (vizProperty(), vizPick, nullptr);

    applySpectrumResolution();
    resized();
    repaint();
}

/** Which of them is up. A menu rather than a row of checkboxes: the block carries a whole EQ
    console now and has room for one picture, and choosing is the honest verb — you look at the
    envelope INSTEAD of the transfer curve, not as well as.

    Where the room seats the pair the list is one shorter, because DEVICE and PHOTO have become one
    card and offering them as two entries that draw the same thing is a choice with no difference in
    it. Where it does not, PHOTO stays on the list even when the pack ships none — greyed, so a
    missing photograph is a fact you can see rather than a page you cannot find. */
void CapturedBlockPanel::showVizMenu (juce::Point<int> screenPos)
{
    static const char* const names[numViz] = { "SHAPE", "ENVELOPE", "TRANSFER", "TONE", "WAVE",
                                              "DEVICE", "PHOTO" };

    const int photoIdx  = (int) DeviceScope::Mode::photo;
    const int deviceIdx = (int) DeviceScope::Mode::device;
    const bool paired   = pairsAsCard();

    juce::PopupMenu m;

    // The way to the whole screen, in words. It used to live only in the corner brackets, and the
    // corner brackets now come and go with the hand — a door that is only visible while you are
    // already reaching for it is a door somebody never finds.
    m.addItem (numViz + 1, "WHOLE SCREEN");
    m.addSeparator();

    m.addSectionHeader ("PICTURE");
    for (int i = 0; i < numViz; ++i)
    {
        if (i == photoIdx && paired)
            continue;

        // Paired, the DEVICE entry answers for whichever of the two the loop is standing on.
        const bool ticked = i == vizPick || (paired && i == deviceIdx && vizPick == photoIdx);

        m.addItem (i + 1, names[i], i != photoIdx || hasPicture(), ticked);
    }

    m.showMenuAsync (juce::PopupMenu::Options()
                         .withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 }),
                     [safe = juce::Component::SafePointer<CapturedBlockPanel> (this)] (int r)
                     {
                         if (r == 0 || safe == nullptr)
                             return;

                         if (r == numViz + 1)
                         {
                             safe->openTheater();
                             return;
                         }

                         safe->setViz (r - 1);
                     });
}

void CapturedBlockPanel::paintContent (juce::Graphics& g)
{
    if (caption.isEmpty())
    {
        g.setColour (theme::txFaint.withAlpha (0.5f));
        theme::drawTracked (g, "No device loaded", contentArea().toFloat(), theme::displayFont (8.0f),
                            0.1f, juce::Justification::centred);
    }

    // Each selecting control's line: its name to the left of the lever, the chosen position to
    // the right of it — the lever between them is the switch itself.
    if (expandedViz < 0)
        for (const auto& sel : selectors)
        {
            if (sel.steps == nullptr || sel.row.isEmpty())
                continue;

            auto row = sel.row.toFloat();
            g.setColour (theme::txDim);
            theme::drawTracked (g, sel.name, row.removeFromLeft ((float) pickNameW - 4.0f),
                                theme::displayFont (10.0f), 0.08f, juce::Justification::centredLeft);

            row.removeFromLeft ((float) pickSwitchW + 8.0f);
            const int chosen = sel.steps->selectedIndex();
            g.setColour (theme::orange);
            theme::drawTracked (g, chosen < sel.values.size() ? sel.values[chosen] : juce::String(),
                                row, theme::displayFont (10.0f), 0.08f, juce::Justification::centredLeft);
        }
}

} // namespace orbitamp
