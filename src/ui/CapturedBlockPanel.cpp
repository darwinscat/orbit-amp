// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CapturedBlockPanel.h"

#include "../AmpChannels.h"
#include "../PluginProcessor.h"
#include "../device/SwitchWords.h"
#include "FactoryMissing.h"

#include <felitronics/appkit/WebpImage.h>

#include <cmath>

namespace orbitamp
{

namespace
{
    /** A decibel written the way a DIFFERENCE has to be written: with its sign. "6 dB below" and
        "6 dB" are not the same sentence, and the meters' hints are nothing but differences. A
        number that rounds to nothing loses the sign with the rest of it — a direction printed on a
        zero is a direction that is not there. */
    juce::String signed1 (double db)
    {
        const double v = std::abs (db) < 0.05 ? 0.0 : db;
        return (v > 0.0 ? "+" : "") + juce::String (v, 1);
    }
}

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

    /** The product's switch for its analysers, as the tile's own scope has it. */
    void showSpectraBy (const std::atomic<bool>& shown) { scope.showSpectraBy (shown); }

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
               *processor.apvts.getParameter (params::blockIn (blockId)), true)
{
    // The console's widgets become children of this block, which places them. The spectrum behind
    // its curve is the block's own output tap — the same one the TONE picture reads, because after
    // the chain rework they are the same point in the signal.
    eq.spectraShown = &prefs::spectraCache (processor.product());
    eq.addTo (*this);

    addAndMakeVisible (device);
    addAndMakeVisible (gain);
    addAndMakeVisible (inMeter);

    device.fontHeight = 16.0f;   // the device's NAME, set exactly like the block's own beside it
    device.tracking   = 0.15f;
    device.boxed      = false;   // ...and nothing behind it, the frame opens the line under it

    // The hero wears no label — the whole square is dial — and says its name under the mouse:
    // the PACK's name for it, set when the device loads (Drive, Sustain, Fuzz, Overdrive...).
    // Its arc runs cold to hot, because that is what this one dial's position is.
    gain.labelRowHeight = 0;
    gain.heat = true;
    gain.setArc (juce::MathConstants<float>::pi * 7.0f / 6.0f, juce::MathConstants<float>::pi * 17.0f / 6.0f);     // 0 at 7:00, 2 at 9:00, 8 at 3:00, 10 at 5:00

    // The wall stands up, at the picture well's left edge.
    //
    // What its two marks MEAN is set with the pack (deviceChanged) and kept current by this face's
    // own tick (refreshMeterOffsets): the fill is the model's input, because the green zone
    // describes that door and nothing else, and the line is what enters the block.
    //
    // No OUT wall. Its hand was the block's LEVEL, and LEVEL and the panel's OUT are two volumes
    // before the limiter with nothing between them that a level breaks — the delay's repeats press
    // softly, the room and the cabinet are linear — so the panel's OUT is the one hand for loudness.
    // LEVEL stays in the engine, at what the preset says.
    inMeter.vertical = true;

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
        scopes[(size_t) i]->showSpectraBy (prefs::spectraCache (amp.product()));
        scopes[(size_t) i]->setSampleRate (amp.currentSampleRate());
        if ((DeviceScope::Mode) i == DeviceScope::Mode::tone)
            scopes[(size_t) i]->setSpectrumTap (&toneTap, AmpProcessor::eqSpectrumOrder);

        // The picture takes no clicks of its own, so the right-click that changes it reaches this
        // block. Its corner glyphs are separate children and keep theirs.
        scopes[(size_t) i]->setInterceptsMouseClicks (false, false);
        addChildComponent (*scopes[(size_t) i]);
    }

    // What was up when the session was saved — and the EQ curve where nothing was: it is the page
    // that has hands on it, and before it became a page it was always in view.
    vizPick = juce::jlimit (0, numPages - 1,
                            (int) amp.apvts.state.getProperty (vizProperty(), eqPage));

    expandTag.onClick = [this]
    {
        thrownOpen = ! thrownOpen;
        expandTag.expanded = thrownOpen;
        applySpectrumResolution();
        resized();
        repaint();
    };
    addChildComponent (expandTag);

    pageTag.onClick = [this] (juce::Point<int> screenPos) { showVizMenu (screenPos); };
    addChildComponent (pageTag);

    halfTag.onChange = [this]
    {
        scopes[(size_t) DeviceScope::Mode::wave]->waveHalf = halfTag.half;
        repaint();
    };

    startTimerHz (30);   // the hand's whereabouts, for the corner glyphs

    // ...and they start OUT (see childrenChanged).
    setCornerAlpha (0.0f);

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
            block.selectIfMoved (juce::roundToInt (v));
            deviceChanged();
        });

    // The list is not flat: devices, and the player's folders as submenus. The combo shows the
    // device; the channel lever beside the dial shows which of its channels plays.
    device.onOpen = [this] { showDeviceMenu(); };

    // BOOST: a press is the next position round — OFF to ON and back, for the two-way switch every
    // pack so far has; the attachment the selector builds lights the key back (buildSelectors).
    addChildComponent (boostKey);
    boostKey.onPress = [this]
    {
        if (! juce::isPositiveAndBelow (boostIndex, params::numSelectors))
            return;

        auto& sel = selectors[(size_t) boostIndex];
        if (sel.attachment == nullptr || sel.values.isEmpty())
            return;

        auto* p = amp.apvts.getParameter (params::selectorId (blk, boostIndex));
        const int now = juce::roundToInt (p->convertFrom0to1 (p->getValue()));
        sel.attachment->setValueAsCompleteGesture ((float) ((now + 1) % sel.values.size()));
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
    if (! thrownOpen)
        return false;

    thrownOpen = false;
    expandTag.expanded = false;

    applySpectrumResolution();
    resized();
    repaint();
    return true;
}

void CapturedBlockPanel::openTheater()
{
    if (theater != nullptr)
        return;

    // Whatever is up — thrown across the block or standing in its well. The EQ curve goes up as
    // TONE: the same curve over the same spectrum, to be read on the monitor rather than handled.
    const auto mode = modeOfPage (vizPick);

    const auto& from = *scopes[(size_t) mode];

    theater = std::make_unique<ScopeTheater> (
        block, [this] (double hz) { return eq.drawnDb (hz); },
        mode,
        halfTag.half, amp.currentSampleRate(),
        &toneTap, AmpProcessor::eqSpectrumOrder,
        from.deviceSpec(), from.paper(), from.creditLines(), from.picture(),
        [this] { closeTheater(); });
    theater->showSpectraBy (prefs::spectraCache (amp.product()));

    // The face's own copy stops while the theatre runs — the wave ribbon must have ONE
    // resolution-setting reader at a time, and the tone tap one reader of its frames.
    setPictureShown (false);

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
    // On the monitor the EQ page is TONE and gets the long window too; thrown across the block it is
    // still the curve, whose own pane reads the standing size.
    const bool big = (theater != nullptr && modeOfPage (vizPick) == DeviceScope::Mode::tone)
                  || (thrownOpen && vizPick == (int) DeviceScope::Mode::tone);

    const int order = big ? AmpProcessor::eqSpectrumOrderBig : AmpProcessor::eqSpectrumOrder;

    amp.blockSpectrumOrder[(size_t) link].store (order, std::memory_order_relaxed);
    scopes[(size_t) DeviceScope::Mode::tone]->setSpectrumTap (&toneTap, order);

    if (theater != nullptr)
        theater->setSpectrumOrder (order);
}

/** THE IN WALL'S TWO MARKS, from the player's own read-outs.

    The green zone describes the level at the MODEL'S DOOR, and between the meter's tap and that
    door stands a whole stack of gains — the pack's stated input level, the chain's trim past the
    ends of the captured range, the alias trim of the capture that is sounding, the drive of a
    device that has no captured axis. The wall used to be told only the first of them, so on a
    linked bottom rung the zone was being read against a number six decibels out.

    ON THE FACE'S OWN TICK rather than on a parameter callback, and that is deliberate: `selection()`
    and `plan()` are recomputed inside the PROCESSOR's pump, so a callback fired by the dial's own
    write would read the selection from before the move and the mark would sit one dial position
    behind the hand for as long as the hand stayed still. Thirty times a second buys a handful of
    atomic loads and two struct fields read, and it is right whatever moved — the dial, the Pack
    Level Comp switch, a selector, or a pack that has only just finished loading. */
void CapturedBlockPanel::refreshMeterOffsets()
{
    const auto fed = block.modelFeed();

    inMeter.fillOffsetDb  = (float) fed.db;
    inMeter.ghostOffsetDb = fed.single ? 0.0f : (float) fed.otherDb;

    // The WORDS, though, are rebuilt only when the numbers move — this runs for as long as the
    // window is open.
    if (saidFeed && fed.single == lastFeed.single
        && std::abs (fed.db - lastFeed.db) < 0.05
        && std::abs (fed.otherDb - lastFeed.otherDb) < 0.05)
        return;

    lastFeed = fed;
    saidFeed = true;

    // NO AVERAGE, EVER. Two models fed at two different levels are two numbers, and the one number
    // between them would be true of neither — so the marks become one model each and the hint says
    // which. The pre-model tone is missing from both on purpose: it is a filter, it has no decibel,
    // and a level that quietly included a spectrum would be the same lie in a longer coat.
    inMeter.offsetNote = ! fed.single
        ? "Two models are sounding, fed " + signed1 (fed.db) + " and " + signed1 (fed.otherDb)
          + " dB from what enters the block — the bar is one of them, the line the other. No single "
            "number is true of both."
        : std::abs (fed.db) < 0.05                       // …and the same nothing `signed1` prints
            ? juce::String ("The bar is what the model eats, and nothing stands between it and what "
                            "enters the block. Level only: whatever pre-model tone this "
                            "pack has is a filter, and a filter has no decibel to add.")
            : "The bar is what the model eats: " + signed1 (fed.db) + " dB from what enters the "
              "block, which is the line. Level only: whatever pre-model tone this pack has is a "
              "filter, and a filter has no decibel to add.";
}

void CapturedBlockPanel::deviceChanged()
{
    // A new pack, new levels — and the IN wall has to say them, because the player applies them
    // where the wall does not stand.
    refreshMeterOffsets();

    for (auto& sc : scopes)
        sc->setSampleRate (amp.currentSampleRate());

    const auto tones     = block.tones();
    const auto positions = block.gainPositions();

    // The combo names the DEVICE, in the colour of the channel that plays; the row of keys names
    // its channels, the playing one lit — and is not there for a device of one.
    shelf = device::DeviceLibrary::shelfOf (block.packs);
    {
        const int playing = AmpChannels (amp).playing();
        const auto* dev   = shelf.deviceOf (playing);

        juce::Array<VoicingSelector::Entry> entries;
        if (dev != nullptr)
            entries.add ({ dev->name, block.packs.getReference (playing).character, false });

        device.setEntries (std::move (entries));
        device.setSelection (0);

        if (dev != nullptr && dev->channels.size() > 1)
            showChannels (dev->channelNames, dev->channels.indexOf (playing));
        else
            showChannels ({}, -1);
    }

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

    if (const int n = block.gainCaptures(); n > 0)   // files, not the dial's detents (CapturedBlock::gainCaptures)
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
    if (! hasPicture() && vizPick == (int) DeviceScope::Mode::photo)
        setViz ((int) DeviceScope::Mode::device);

    // The gain knob's detents ARE the captured positions. Twenty-one for SM7, whatever the next
    // pack says for the next one. Detented where the pack has positions, continuous where it does
    // not — but always THERE: a knob with nothing to select drives instead, and a device without a
    // gain knob is not a device. Its name is the device's own for that dial.
    gain.setNotches (juce::jmax (0, positions.size()));
    gain.setTooltip (block.dialName().isNotEmpty() ? block.dialName().toUpperCase() : juce::String ("GAIN"));

    for (int i = 0; i < Block::numMeasured; ++i)
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

    for (int i = 0; i < Block::numMeasured && i < (int) tones.size(); ++i)
    {
        const auto& m = tones[(size_t) i];

        // A SWITCH — no sweep, or two named positions — is not a band: the row holds dials.
        // A switch, whatever its legends read: no rotation is the whole test since schema 4.
        if (m.sweep <= 0)
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

            // The swing is only how the strongest point is CHOSEN; it is not kept. It used to
            // be, to convert a drag of the dot into knob travel — and the dot does not drag any
            // more.
            band.anchorHz = m.grid.fLo * std::pow (m.grid.fHi / m.grid.fLo,
                                                   (double) bestIdx / (double) (m.grid.points - 1));
        }
        else if (! m.sections.empty())
        {
            const auto* best = &m.sections.front();
            for (const auto& sec : m.sections)
                if (std::abs (sec.dbAtMax - sec.dbAtMin) > std::abs (best->dbAtMax - best->dbAtMin))
                    best = &sec;

            if (best->hz > 0.0)
                band.anchorHz = best->hz;
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

void CapturedBlockPanel::resetToneSlotsToPackDefaults()
{
    const auto tones = block.tones();

    for (int i = 0; i < Block::numMeasured; ++i)
        if (auto* p = amp.apvts.getParameter (params::blockMeasured (blk, i)))
        {
            const float plain = (float) (i < (int) tones.size() ? Block::defaultNorm (tones[(size_t) i]) : 0.5);
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (plain));
            p->endChangeGesture();
        }
}

void CapturedBlockPanel::playPack (int index)
{
    if (AmpChannels (amp).play (index))
        deviceChanged();
}

/** The lever for the device that plays: rebuilt only when the count of its channels changes — its
    places are fixed for its life — and gone for a device of one.

    A PICK, and only a pick, goes through the channels' memory: a restored session, an undo or a host
    automating the device index move the parameter too, and what they set is what they meant — the
    attachment only follows them, and the lever with it. A lever's own pick never rebuilds it: it
    plays a channel of the device it already shows, so the count cannot change under its hand. */
void CapturedBlockPanel::showChannels (const juce::StringArray& names, int lit)
{
    if (names.size() < 2)
    {
        if (lever != nullptr)
        {
            removeChildComponent (lever.get());
            lever.reset();
        }
        return;
    }

    if (lever == nullptr || lever->numPlaces() != names.size())
    {
        if (lever != nullptr)
            removeChildComponent (lever.get());

        lever = std::make_unique<ChannelLever> (names.size());
        lever->onPick = [this] (int k)
        {
            if (const auto* d = shelf.deviceOf (AmpChannels (amp).playing()))
                if (juce::isPositiveAndBelow (k, d->channels.size()))
                    playPack (d->channels[k]);
        };
        addChildComponent (*lever);
    }

    lever->setChannels (names, lit);
    lever->setTooltip ("CHANNEL: " + names.joinIntoString (juce::String::fromUTF8 ("   \xc2\xb7   ")).toUpperCase());
}

void CapturedBlockPanel::showDeviceMenu()
{
    using Library = device::DeviceLibrary;

    shelf = Library::shelfOf (block.packs);   // the folder may have changed since the face was built

    AmpChannels channels (amp);
    const auto* playing = shelf.deviceOf (channels.playing());

    juce::PopupMenu menu;
    std::vector<int> picks;   // an item's id, less one, is the pack it plays

    const auto add = [&] (juce::PopupMenu& into, const Library::Device& d)
    {
        // A device opens on the channel it was last left on — the one its colour is.
        const int opens = channels.openingOf (d);

        juce::PopupMenu::Item item (d.name);
        item.itemID   = (int) picks.size() + 1;
        item.colour   = theme::characterColour (block.packs.getReference (opens).character);
        item.isTicked = &d == playing;
        into.addItem (item);
        picks.push_back (opens);
    };

    // The family's own on top, then the player's, a rule between: the list stays one list.
    for (const auto* part : { &shelf.factory, &shelf.own })
    {
        if (! part->empty() && menu.getNumItems() > 0)
            menu.addSeparator();

        for (const auto& d : *part)
            add (menu, d);
    }

    // ...and each folder of the player's as a submenu, as the disk has it — OLD ▸ preamps ▸ … — the
    // factory's OLD, where OrbitAmp puts away the V4, one with his (DeviceLibrary::Folder).
    std::function<bool (juce::PopupMenu&, const Library::Folder&)> addFolder;
    addFolder = [&] (juce::PopupMenu& into, const Library::Folder& f)
    {
        juce::PopupMenu sub;
        bool holdsPlaying = false;

        for (const auto& d : f.devices)
        {
            holdsPlaying = holdsPlaying || &d == playing;
            add (sub, d);
        }

        for (const auto& inner : f.folders)
            holdsPlaying = addFolder (sub, inner) || holdsPlaying;

        into.addSubMenu (f.name, sub, true, nullptr, holdsPlaying);
        return holdsPlaying;
    };

    if (! shelf.folders.empty() && menu.getNumItems() > 0)
        menu.addSeparator();

    for (const auto& f : shelf.folders)
        addFolder (menu, f);

    if (menu.getNumItems() == 0)
        return;

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&device),
                        [safe = juce::Component::SafePointer<CapturedBlockPanel> (this), picks] (int choice)
                        {
                            if (safe != nullptr && choice > 0 && choice <= (int) picks.size())
                                safe->playPack (picks[(size_t) (choice - 1)]);
                        });
}

void CapturedBlockPanel::buildSelectors()
{
    const auto list = block.selectors();

    // BOOST, by its name: the first selector a pack calls that is the key under the lever, and only
    // the first — a second of the same name is a pick like any other.
    boostIndex = -1;
    for (int i = 0; i < params::numSelectors && i < list.size(); ++i)
        if (list.getReference (i).name.trim().equalsIgnoreCase ("boost"))
        {
            boostIndex = i;
            break;
        }

    for (int i = 0; i < params::numSelectors; ++i)
    {
        auto& sel = selectors[(size_t) i];

        sel.attachment.reset();
        sel.steps.reset();
        sel.row = {};

        if (i >= list.size())
            continue;

        const auto& def = list.getReference (i);

        juce::StringArray labels;
        for (const auto& v : def.values)
            labels.add (v.toUpperCase());

        sel.name   = def.name.toUpperCase();
        sel.values = labels;
        sel.onOff  = labels.size() == 2 && labels[0] == "OFF" && labels[1] == "ON";

        const auto hint = sel.name + ":   " + labels.joinIntoString (juce::String::fromUTF8 ("   \xc2\xb7   "));

        if (i == boostIndex)
        {
            // The key, not a switch: lit while it stands anywhere but its first position.
            boostKey.setTooltip (hint);
            sel.attachment = std::make_unique<juce::ParameterAttachment> (
                *amp.apvts.getParameter (params::selectorId (blk, i)),
                [this] (float v) { boostKey.setLit (juce::roundToInt (v) > 0); });
            sel.attachment->sendInitialUpdate();
            continue;
        }

        sel.steps = std::make_unique<VSwitch>();
        sel.steps->accent = theme::orange;

        // Lying down in the dial's strip, one line high: the slot and its lever alone — the word
        // beside it is written by the panel, and the whole list is the hint.
        sel.steps->setHorizontal (true);
        sel.steps->setNamesShown (false);
        sel.steps->setItems (labels, 0);
        sel.steps->setTooltip (hint);
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
    setCornerAlpha (cornerAlpha);   // the frame has just dimmed — or lit — every child, the glyphs too
    inMeter.live = on;
    inMeter.repaint();
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

    // A thrown-open picture owns the WHOLE face: every control steps aside until the corner glyph
    // brings the room back.
    if (thrownOpen)
    {
        setControlsVisible (false);
        eq.setRowVisible (false);
        dividerX = { -1, -1 };
        layPicture (area);
        return;
    }

    setControlsVisible (true);
    eq.setRowVisible (true);

    // ---- the IN meter first, the block's whole height: what the model is fed comes before the dial
    //      that decides what it does with it. The component is wider than its bar; the margin, over
    //      GAIN's column, is where the dB appears while the hand drags, and click-through otherwise. ----
    {
        auto inCol = area.removeFromLeft (BlockMeter::designWidth);
        area.removeFromLeft (6);
        inMeter.setBounds (inCol.withWidth (BlockMeter::designWidth + BlockMeter::standingValueW));
        inMeter.toFront (false);    // over GAIN's column, so the dragged reading paints on top
    }

    // ---- THREE SECTIONS, each a tier over a strip. The two narrow ones are a FIXED width and the
    //      well takes the rest, so the widest thing on the block is the picture. ----
    auto s1 = area.removeFromLeft (section1W);
    const auto column1 = s1;   // GAIN's whole column: its strip is its own, not the shared one
    area.removeFromLeft (sectionGap);
    auto s2 = area.removeFromLeft (section2W);
    area.removeFromLeft (sectionGap);
    auto s3 = area;

    dividerX = { s1.getRight() + sectionGap / 2, s2.getRight() + sectionGap / 2 };

    // One line across the block where the tiers end and the strips begin.
    const auto strip = [] (juce::Rectangle<int>& s)
    {
        auto r = s.removeFromBottom (stripH);
        s.removeFromBottom (tierGap);
        return r;
    };

    auto strip2 = strip (s2), strip3 = strip (s3);

    // ---- section 3: the picture well, the console's row under it. ----
    eq.layOutRow (strip3);
    layPicture (s3);

    // ---- section 1: the GAIN dial, as BIG as the block lets it be. Its strip is not the shared one:
    //      it is only as tall as SMOOTH and the picks need, and the dial grows down into the rest.
    //
    //      ROOM FOR ONE PICK, whatever the pack brings — SMOOTH and one line under it — so the dial is
    //      the same size on every device of the set, the X88-IR that has no pick and the IR-X that has
    //      BRIGHT: a dial that changed size with the device would be a face to re-learn per device. A
    //      pack with two picks besides BOOST (none yet) takes its third line out of the dial. ----
    {
        std::vector<int> widths { SmoothTag::idealWidth() };
        for (auto& sel : selectors)
            if (sel.steps != nullptr)
                widths.push_back (pickSwitchW + 8 + juce::roundToInt (pickWordWidth (sel)));

        const int rows     = (int) widths.size();
        const int reserved = juce::jmax (2, rows);
        auto column = column1;
        auto strip1 = column.removeFromBottom (reserved * pickRowH + (reserved - 1) * pickRowGap);
        column.removeFromBottom (dialGap);

        const int gainSide = juce::jmin (maxGainSide, juce::jmin (column.getWidth(), column.getHeight()));
        gain.setBounds (column.withSizeKeepingCentre (gainSide, gainSide));

        // The lines stacked in the middle of their strip, left-aligned on one edge so the switches stand
        // in a column under the dial.
        const int stackH = rows * pickRowH + (rows - 1) * pickRowGap;
        const int stackW = juce::jmin (strip1.getWidth(), *std::max_element (widths.begin(), widths.end()));

        auto stack = strip1.withSizeKeepingCentre (stackW, juce::jmin (strip1.getHeight(), stackH));

        smoothTag.setBounds (stack.removeFromTop (pickRowH).withWidth (SmoothTag::idealWidth()));

        for (auto& sel : selectors)
        {
            if (sel.steps == nullptr)
                continue;

            stack.removeFromTop (pickRowGap);
            sel.row = stack.removeFromTop (pickRowH);
            sel.steps->setBounds (sel.row.withWidth (pickSwitchW).reduced (0, 1));
        }
    }

    // ---- section 2: the lever, as tall as the picture beside it, and BOOST under it — or nothing
    //      there, for a pack that has no boost: the place stays, the sections do not close up. ----
    if (lever != nullptr)
    {
        const int w = juce::jmin (s2.getWidth(), juce::roundToInt (ChannelLever::idealWidth (lever->channelNames())) + 2);
        lever->setBounds (s2.withSizeKeepingCentre (w, s2.getHeight()));
    }

    boostKey.setVisible (boostIndex >= 0);
    if (boostIndex >= 0)
        boostKey.setBounds (strip2.withSizeKeepingCentre (juce::jmin (strip2.getWidth(), boostKey.idealWidth()),
                                                         LatchKey::keyH));
}

float CapturedBlockPanel::pickWordWidth (const PickSlot& sel)
{
    float w = 0.0f;
    for (const auto& v : sel.onOff ? juce::StringArray (sel.name) : sel.values)
        w = juce::jmax (w, theme::trackedWidth (v, theme::displayFont (pickFontH), pickTracking));
    return w;
}

void CapturedBlockPanel::setControlsVisible (bool v)
{
    gain.setVisible (v);
    smoothTag.setVisible (v);
    inMeter.setVisible (v);
    device.setEnabled (v);

    if (lever != nullptr)
        lever->setVisible (v);

    boostKey.setVisible (v && boostIndex >= 0);

    for (auto& slot : slots)
    {
        if (slot.knob != nullptr)  slot.knob->setVisible (v);
        if (slot.steps != nullptr) slot.steps->setVisible (v);
    }

    for (auto& sel : selectors)
        if (sel.steps != nullptr)
            sel.steps->setVisible (v);
}

void CapturedBlockPanel::setPictureShown (bool shown)
{
    for (int i = 0; i < numViz; ++i)
        scopes[(size_t) i]->setVisible (shown && i == vizPick);

    eq.setCurveVisible (shown && vizPick == eqPage);
}

/** One page of the well, filling what it is handed, wearing its corner glyphs: the page's name at the
    top left, the one way to throw it open at the top right — and on WAVE, its half-wave switch beside
    that; on the EQ curve, whose tone it wears beside that. */
void CapturedBlockPanel::layPicture (juce::Rectangle<int> area)
{
    widgetArea = area;

    // EVERY scope gets the room, not only the one standing in it: the questions asked of the room —
    // does the pair fit as a card here — are asked of a scope, and the one up may be the curve.
    for (auto& sc : scopes)
        sc->setBounds (area);

    setPictureShown (theater == nullptr);

    constexpr int glyphW = 22, glyphH = 16, inset = 6;

    expandTag.setBounds (area.getRight() - inset - glyphW, area.getY() + 4, glyphW, glyphH);
    expandTag.setVisible (true);
    expandTag.toFront (false);

    int right = expandTag.getX() - 4;

    if (vizPick == eqPage)
        eq.layOutCurve (area, area.getRight() - right);

    const bool isWave = vizPick == (int) DeviceScope::Mode::wave;
    halfTag.setVisible (isWave);
    if (isWave)
    {
        halfTag.setBounds (right - 36, area.getY() + 4, 36, glyphH);
        halfTag.toFront (false);
    }

    static const char* const names[numPages] = { "SHAPE", "ENVELOPE", "TRANSFER", "TONE", "WAVE",
                                                "DEVICE", "PHOTO", "EQ" };
    pageTag.text = names[vizPick];
    pageTag.setBounds (area.getX() + inset, area.getY() + 4, pageTag.idealWidth(), 20);
    pageTag.setVisible (true);
    pageTag.toFront (false);
    pageTag.repaint();
}

/** A right-click on the PICTURE picks which picture; anywhere else on the block it still means the
    power menu, which is the one edit that must never need aiming for. */
void CapturedBlockPanel::mouseDown (const juce::MouseEvent& e)
{
    // A selecting control's line answers on its whole length: the lever goes where it is clicked,
    // and a click on the word beside it steps to the next position round the loop, so a two-way
    // switch is a click wherever the eye lands.
    if (! thrownOpen && ! e.mods.isPopupMenu())
        for (auto& sel : selectors)
            if (sel.steps != nullptr && sel.row.contains (e.getPosition())
                && ! sel.steps->getBounds().contains (e.getPosition()))
            {
                sel.steps->setSelectedIndex ((sel.steps->selectedIndex() + 1) % juce::jmax (1, sel.steps->count()));
                return;
            }

    // The curve takes its own clicks — its nodes are dragged — so whatever reaches here on the well
    // is a scope's.
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

/** THE FRAME DIMS EVERY CHILD — to one when the block is on (BlockFrame::dimChildren) — and the
    corner glyphs are children. So every child that arrived, a lever for a new device among them, used
    to light the glyphs at full strength over the picture, and the tick never put them out again: it
    only moves the fade when the fade has somewhere to go. Which is why they were always standing in
    the corner, whatever the hand was doing — and the page's name now stands on the curve's own
    decibel labels. The fade is put back over whatever the frame did. */
void CapturedBlockPanel::childrenChanged()
{
    BlockFrame::childrenChanged();
    setCornerAlpha (cornerAlpha);
}

void CapturedBlockPanel::setCornerAlpha (float a)
{
    cornerAlpha = a;

    const bool lit = a > 0.01f;
    const float dim = isBlockOn() ? 1.0f : theme::offAlpha;   // a dark block's glyphs are as dark as it is

    // Transparent is still clickable in JUCE, and a glyph nobody can see must not answer a click
    // meant for the picture underneath it.
    for (juce::Component* c : { (juce::Component*) &expandTag, (juce::Component*) &halfTag, (juce::Component*) &pageTag })
    {
        c->setAlpha (a * dim);
        c->setInterceptsMouseClicks (lit, lit);
    }
}

void CapturedBlockPanel::timerCallback()
{
    // What the model is being fed moves with the dial, with the Pack Level Comp switch, with a
    // selector and with a pack that has only just landed — none of which is one event to hang a
    // callback on, and the one that looks like it (the dial's own parameter) fires BEFORE the pump
    // that recomputes the answer. So it is read here, every tick.
    refreshMeterOffsets();

    // The theatre owns the whole screen and its own way out; while it runs the face beneath is not
    // being pointed at, whatever the coordinates say.
    const bool want = theater == nullptr && (handIsOnThePicture() || glyphsShownForStill);
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

/** Every scope is given the well's bounds, so any of them answers for the room — DEVICE is asked,
    because the question is about it, and the page that is up may be the EQ curve. */
bool CapturedBlockPanel::pairsAsCard() const
{
    const auto& sc = scopes[(size_t) DeviceScope::Mode::device];
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
    for (int i = 1; i <= numPages; ++i)
    {
        const int j = (from + i) % numPages;

        if (j == (int) DeviceScope::Mode::photo && (! hasPicture() || pairsAsCard()))
            continue;

        return j;
    }

    return from;
}

/** Both ways of choosing land here, so the choice is remembered whichever was used. */
void CapturedBlockPanel::setViz (int which)
{
    // Thrown open or standing in the well, it is the same page — the throw is a size, not a page.
    vizPick = juce::jlimit (0, numPages - 1, which);

    // Kept with the session rather than as a parameter: it is which picture you are looking at,
    // not something a host should be automating.
    amp.apvts.state.setProperty (vizProperty(), vizPick, nullptr);

    applySpectrumResolution();
    resized();
    repaint();
}

/** Which of them is up. A menu rather than a row of checkboxes: the well has room for one picture,
    and choosing is the honest verb — you look at the envelope INSTEAD of the EQ curve, not as well
    as. The EQ curve leads the list: it is the page with hands on it.

    Where the room seats the pair the list is one shorter, because DEVICE and PHOTO have become one
    card and offering them as two entries that draw the same thing is a choice with no difference in
    it. Where it does not, PHOTO stays on the list even when the pack ships none — greyed, so a
    missing photograph is a fact you can see rather than a page you cannot find. */
void CapturedBlockPanel::showVizMenu (juce::Point<int> screenPos)
{
    static const char* const names[numPages] = { "SHAPE", "ENVELOPE", "TRANSFER", "TONE", "WAVE",
                                                "DEVICE", "PHOTO", "EQ" };

    const int photoIdx  = (int) DeviceScope::Mode::photo;
    const int deviceIdx = (int) DeviceScope::Mode::device;
    const bool paired   = pairsAsCard();

    juce::PopupMenu m;

    // The way to the whole screen, in words. It used to live only in the corner brackets, and the
    // corner brackets now come and go with the hand — a door that is only visible while you are
    // already reaching for it is a door somebody never finds.
    m.addItem (numPages + 1, "WHOLE SCREEN");
    m.addSeparator();

    m.addSectionHeader ("PICTURE");
    m.addItem (eqPage + 1, names[eqPage], true, vizPick == eqPage);

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

                         if (r == numPages + 1)
                         {
                             safe->openTheater();
                             return;
                         }

                         safe->setViz (r - 1);
                     });
}

void CapturedBlockPanel::paintContent (juce::Graphics& g)
{
    // THE MODEL'S LOUDNESS TAG, and the one case worth saying aloud. Every player normalizes by it —
    // there is no switch here and there is not going to be one — so a model that carries NO tag plays
    // at whatever level the hardware happened to give it, some 8-10 dB away from every neighbour, and
    // nothing else on this face would report it. The number itself is indication; the missing one is
    // a warning, and it is written in the hand's own colour so it cannot be mistaken for furniture.
    if (block.isReady() && showsLoudnessTag)
    {
        // …OF THE MODEL THAT IS SOUNDING. The player used to answer for slot 0 whatever was audible,
        // and slots go by knot parity: on an odd capture the whole sound comes out of slot 1, so this
        // line named a silent neighbour's tag — or called a tagged model untagged. Between two
        // captures there are two tags in the sound and no single number is the answer, which is what
        // the "of two" says rather than picking one and keeping quiet about it.
        //
        // At reading size, on the box's top line right of the device, fieldset-style as the name
        // rides it on the left: the device's own word beside the device, and inside the box the tone
        // knobs and the cut filters' corners leave no row for it. Where the line is too short for
        // the "of two", it says what matters without it.
        const auto tag  = block.loudness();
        const auto font = theme::displayFont (13.0f);
        const auto run  = borderSlotArea().toFloat();
        const float room = run.getRight() - (float) borderSlotUsed.getRight() - 2.0f * legendGap;
        auto text = tag.tagged ? juce::String (tag.db, 1) + (tag.blended ? " dB tagged, of two" : " dB tagged")
                               : juce::String (tag.blended ? "no loudness tag on the louder of two - plays raw"
                                                           : "no loudness tag - plays raw");
        if (theme::trackedWidth (text, font, 0.09f) + 1.0f > room)
            text = tag.tagged ? juce::String (tag.db, 1) + " dB tagged" : juce::String ("no loudness tag - plays raw");
        const float w = theme::trackedWidth (text, font, 0.09f) + 1.0f;
        const auto line = juce::Rectangle<float> (w, 18.0f).withCentre ({ run.getRight() - w * 0.5f, run.getCentreY() });
        g.setColour (theme::capTop);
        g.fillRect (line.expanded (legendGap, 0.0f).withHeight (theme::blockBorder + 2.0f).withCentre (line.getCentre()));
        g.setColour (tag.tagged ? theme::txFaint.withAlpha (0.55f) : theme::orange);
        theme::drawTracked (g, text, line, font, 0.09f, juce::Justification::centredRight);
    }

    if (caption.isEmpty())
    {
        // ...and why, where it is that the family's factory folder is not there: what OrbitAmp plays out
        // of the box lies in it (ui/FactoryMissing.h). Looked at once in a process.
        static const bool noFactory = ! device::FactorySet::isThere();
        auto area = contentArea();
        if (noFactory)
        {
            auto below = area.removeFromBottom (area.getHeight() / 2).reduced (12, 0);
            g.setColour (theme::txDim);
            g.setFont (juce::FontOptions (13.0f));
            g.drawFittedText (ui::factoryMissingLine(), below, juce::Justification::centredTop, 4, 1.0f);
        }

        g.setColour (theme::txFaint.withAlpha (0.5f));
        theme::drawTracked (g, "No device loaded", area.toFloat(), theme::displayFont (13.0f),
                            0.1f, juce::Justification::centred);
    }

    if (thrownOpen)
        return;

    // The hairlines between the sections: three instruments on one plate, told apart by a line in
    // the panel's own violet, never a box round each.
    {
        const auto content = contentArea();
        g.setColour (theme::hair2);
        for (const int x : dividerX)
            if (x >= 0)
                g.fillRect (juce::Rectangle<float> ((float) x - 0.5f, (float) content.getY() + 4.0f,
                                                    1.0f, (float) content.getHeight() - 8.0f));
    }

    // Each selecting control's line: the switch, then its words — the name of an OFF/ON, lit
    // while on; the chosen position of anything else, in the hand's colour, the switch's name before
    // it where the position is bare (BRIGHT 1: device::positionWords).
    for (const auto& sel : selectors)
    {
        if (sel.steps == nullptr || sel.row.isEmpty())
            continue;

        auto row = sel.row.toFloat();
        row.removeFromLeft ((float) pickSwitchW + 8.0f);
        const int chosen = sel.steps->selectedIndex();

        const auto word = sel.onOff ? sel.name
                                    : (chosen < sel.values.size() ? device::positionWords (sel.name, sel.values[chosen])
                                                                  : juce::String());
        g.setColour (sel.onOff && chosen == 0 ? theme::txDim : theme::orange);
        theme::drawTracked (g, word, row, theme::displayFont (pickFontH), pickTracking,
                            juce::Justification::centredLeft);
    }
}

} // namespace orbitamp
