// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "PluginEditor.h"

#include "ui/HumMenu.h"
#include "ui/HumTuning.h"
#include "ui/Prefs.h"
#include "ui/WebFaceplate.h"

namespace orbitamp
{

AmpEditor::AmpEditor (AmpProcessor& p)
    : juce::AudioProcessorEditor (&p), amp (p), chrome (p), faceplate (p),
      inColumn  (VolumeColumn::Side::in,  p.gateKeyDb, p.inClip,
                 *p.apvts.getParameter (params::inTrim)),
      outColumn (VolumeColumn::Side::out, p.outDb,     p.outClip,
                 *p.apvts.getParameter (params::outTrim)),
      gateRail  (*p.apvts.getParameter (params::gateThreshold), *p.apvts.getParameter (params::gateOn),
                 -80.0f, 0.0f, true, -50.0f),
      limitRail (*p.apvts.getParameter (params::limiterCeiling), *p.apvts.getParameter (params::limiterOn),
                 params::limiterCeilingMin, params::limiterCeilingMax, false, -1.0f),
      // Silence is not work: with nothing at the key (the -90 floor) the gate has nothing to press, and
      // a light that burns all night means nothing by morning.
      gateLamp  ("GATE", *p.apvts.getParameter (params::gateOn),
                 [&p] { return p.gateKeyDb.load() <= -89.5f ? 0.0f : -p.gateMeterDb.load() / 40.0f; },
                 p.gateWorked),
      limitLamp ("LIMIT", *p.apvts.getParameter (params::limiterOn),
                 [&p] { return -p.limiterGrDb.load() / 6.0f; }, p.limiterWorked),
      gateConsole (p.apvts, p.gateKeyDb),
      humLearner (p.product(), p.humLearn, p.gateKeyDb, p.humTuned.quietHz),
      tunerStrip (p.tunerEar), footer (p), demoStrip (p), setup (p)
{
    setWantsKeyboardFocus (true);

    addAndMakeVisible (chrome);
    addAndMakeVisible (faceplate);

    // The hints wear the panel's own colours, not the stock yellow.
    tooltips.setColour (juce::TooltipWindow::backgroundColourId, theme::panel2);
    tooltips.setColour (juce::TooltipWindow::textColourId, theme::txDim);
    tooltips.setColour (juce::TooltipWindow::outlineColourId, theme::hair2);
    addAndMakeVisible (inColumn);
    addAndMakeVisible (outColumn);
    limitRail.inwardRight = false;   // its centre is to its left
    addAndMakeVisible (gateRail);
    addAndMakeVisible (limitRail);
    addAndMakeVisible (gateLamp);
    addAndMakeVisible (limitLamp);

    // THE GUARDS LIVE AT THE ENDS: the gate at the way in, the limiter at the way out. Their menus
    // open from their rails, their lamps and — the gate's — the IN column itself; a look at the lamp
    // clears its dot.
    const auto gateMenu  = [this] (juce::Point<int> pos) { amp.gateWorked.store (false);    gateConsole.showMenu (pos); };
    const auto limitMenu = [this] (juce::Point<int> pos) { amp.limiterWorked.store (false); showLimiterMenu (pos); };
    gateRail.onMenu   = gateMenu;
    gateLamp.onClick  = gateMenu;
    limitRail.onMenu  = limitMenu;
    limitLamp.onClick = limitMenu;
    outColumn.onMenu  = limitMenu;
    if constexpr (features::hum)   // the input column's menu is HUM's in the build that has it (Features.h)
        inColumn.onMenu = [this] (juce::Point<int> pos) { showInputMenu (pos); };
    else
        inColumn.onMenu = gateMenu;
    addAndMakeVisible (tunerStrip);
    tunerStrip.mute.attach (*p.apvts.getParameter (params::tunerMute));
    addAndMakeVisible (footer);
    // The two TEMPORARY strips under the footer, off unless this player asked for them (the gear).
    // The demo needs its loops on disk — a machine without them has no player to show.
    showDemo   = params::demoLoopsPresent() && prefs::getBool (amp.product(), prefs::showDemo, false);
    showGlyphs = prefs::getBool (amp.product(), prefs::showGlyphs, false);

    faceplate.setEqRowOnTop (prefs::getBool (amp.product(), prefs::eqRowOnTop, false));


    addChildComponent (demoStrip);
    addChildComponent (glyphs);
    addChildComponent (setup);       // hidden until the toolbar's gear opens it
    addChildComponent (devices);     // ...and DEVICES & TRADEMARKS, from the same menu

    // THE GEAR IS THE WINDOW. It used to drop a popup with a door to Setup, two doors to pages
    // nobody sets anything on, and four switches — which is how a settings menu becomes a place
    // things are hidden. One window, two pages, and the two pages that are only read reached
    // from where they are ABOUT: the version stamp and DEVICES, both at the bottom.
    chrome.onGear = [this] (juce::Point<int>) { setup.open(); };

    footer.onDevices = [this] { devices.open(); };

    setup.onViewChanged = [this] { applyViewPrefs(); };

    // FULL SCREEN, the honest kind: the aspect is locked, so a native fullscreen would only
    // letterbox the device in black. Instead the button jumps to the biggest fit the display
    // holds — the standalone window centred on its screen — and jumps BACK to where you were
    // on the second press.
    chrome.onFullScreen = [this]
    {
        auto* tl = getTopLevelComponent();
        const auto& displays = juce::Desktop::getInstance().getDisplays();
        const auto* display  = tl != nullptr ? displays.getDisplayForRect (tl->getScreenBounds())
                                             : displays.getPrimaryDisplay();
        if (display == nullptr)
            return;

        // The whole display: the zoom from its height (or from its width, on a display narrower than
        // the design), the width then as much of the display as the range allows.
        const auto area = display->userArea;
        const float fit = juce::jlimit (AmpProcessor::minScale, AmpProcessor::maxScale,
                                        juce::jmin ((float) area.getWidth() / (float) minBaseWidth,
                                                    (float) (area.getHeight() - titleBarAllowance)
                                                        / (float) baseHeight()));
        const int fullH = juce::roundToInt ((float) baseHeight() * fit);
        const int fullW = juce::jlimit (juce::roundToInt ((float) minBaseWidth * fit),
                                        juce::roundToInt ((float) (maxDesignWidth() + margin * 2) * fit),
                                        area.getWidth());

        if (! sizeBeforeFull.isEmpty() && std::abs (getHeight() - fullH) <= 1 && std::abs (getWidth() - fullW) <= 1)
        {
            setSize (sizeBeforeFull.getWidth(), sizeBeforeFull.getHeight());
            sizeBeforeFull = {};
        }
        else
        {
            sizeBeforeFull = getLocalBounds();
            setSize (fullW, fullH);
        }

        if (auto* window = dynamic_cast<juce::ResizableWindow*> (tl))
            window->setCentrePosition (area.getCentre());
    };

    // The layout strip: the chain, FIXED — every block's arrow is its STANDBY/ON, and nothing is
    // ever taken off the panel. The two ends and the tuner are not arrows: their columns and their
    // strip are always on the face.
    {
        std::vector<LayoutStrip::Row> rows;

        for (int i = 0; i < params::numChainRows; ++i)
        {
            const auto& link = params::chainLinks[(size_t) i];

            LayoutStrip::Row row { link.name, link.captured ? theme::orange : theme::violet };
            row.present = link.onParam != nullptr && i != params::rowTuner;
            rows.push_back (std::move (row));
        }

        layoutStrip = std::make_unique<LayoutStrip> (std::move (rows));
    }

    layoutStrip->onToggle = [this] (int i, bool on)
    {
        // THE switch: the arrow writes the link's own parameter, so the save, the history, and
        // the registers all carry the click — the panel follows through the echo below.
        if (const char* id = params::chainLinks[(size_t) i].onParam)
            if (auto* p = amp.apvts.getParameter (id))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (on ? 1.0f : 0.0f);
                p->endChangeGesture();
            }
    };

    // The echo that makes it ONE fact: wherever a link's switch moves — the strip, an undo, a
    // loaded preset, a register switch, host automation — the arrow follows. The blocks dim
    // themselves: each binds its own switch.
    for (int i = 0; i < params::numChainRows; ++i)
    {
        const auto& link = params::chainLinks[(size_t) i];

        if (link.onParam != nullptr && i != params::rowTuner)
            blockRowAtts.push_back (std::make_unique<juce::ParameterAttachment> (
                *amp.apvts.getParameter (link.onParam),
                [this, i] (float v) { layoutStrip->setRowOn (i, v > 0.5f); }));
    }

    for (auto& att : blockRowAtts)
        att->sendInitialUpdate();

    addAndMakeVisible (*layoutStrip);

    // The badges' clicks mean MENU — the whole device in one pick. There is nowhere to "open" any
    // more and nothing to open TO: every block wears its own face on the panel, and the gate's own
    // controls, position included, live in this menu.

    // The measurement, projected: the overlay reads the strip's own trace.
    // The hum's LEARN on the same overlay, in its own words and with no threshold to walk; only one of
    // the two runs at a time — starting either cancels the other.
    gateConsole.onLearnBegin = [this]
    {
        humLearner.cancel();
        learnOverlay.title      = "LEARN";
        learnOverlay.trace      = &gateConsole.learnTraceRef();
        learnOverlay.totalTicks = GateConsole::learnTotalTicks;
        learnOverlay.ticksNow   = nullptr;
        learnOverlay.pendingDb  = [this] { return gateConsole.learnPendingDb(); };
        learnOverlay.begin();
    };
    gateConsole.onLearnDone  = [this] (const juce::String& v) { learnOverlay.finish (v); };
    humLearner.onBegin = [this]
    {
        learnOverlay.title      = "HUM LEARN";
        learnOverlay.trace      = &humLearner.trace();
        learnOverlay.ticksNow   = [this] { return humLearner.ticksPlanned(); };
        learnOverlay.pendingDb  = nullptr;
        learnOverlay.begin();
    };
    humLearner.onDone = [this] (const HumLearner::Take& take) { learnOverlay.finish (HumLearner::words (take).toUpperCase()); };

    addChildComponent (learnOverlay);

    // The rulers the runners summon: IN's stands right of its column, OUT's and the ceiling's
    // left of theirs — over whatever lives there, gone the moment the hand opens.
    const auto trimLadder = []
    {
        std::vector<DragRuler::Mark> m;
        for (int db = -24; db <= 24; db += 3)
            m.push_back ({ (float) db, db % 6 == 0 });
        return m;
    }();

    const auto trimYOf = [] (juce::Rectangle<float> r, float db)
    {
        return r.getCentreY() - db / params::inTrimRangeDb * (r.getHeight() * 0.5f - 6.0f);
    };

    inRuler.ticksOnLeft  = true;
    outRuler.ticksOnLeft = false;
    inRuler.marks  = trimLadder;
    outRuler.marks = trimLadder;
    inRuler.yOfDb  = trimYOf;
    outRuler.yOfDb = trimYOf;
    inRuler.currentDb  = [this] { auto* p = amp.apvts.getParameter (params::inTrim);
                                  return p->convertFrom0to1 (p->getValue()); };
    outRuler.currentDb = [this] { auto* p = amp.apvts.getParameter (params::outTrim);
                                  return p->convertFrom0to1 (p->getValue()); };

    // The guards' own ladders, on their rails' scales: the gate's threshold over the IN column's
    // -80..0, a mark every 6 dB and a number every 12; the limiter's ceiling over its -3..-0.1, a mark
    // every half decibel and a number every one, with -0.5 at the top.
    const auto linearYOf = [] (float lo, float hi)
    {
        return [lo, hi] (juce::Rectangle<float> r, float db)
        {
            return r.getBottom() - r.getHeight() * (juce::jlimit (lo, hi, db) - lo) / (hi - lo);
        };
    };

    gateRuler.ticksOnLeft  = true;
    limitRuler.ticksOnLeft = false;
    gateRuler.accent  = theme::lilac;
    limitRuler.accent = theme::lilac;

    for (int db = -72; db <= 0; db += 6)
        gateRuler.marks.push_back ({ (float) db, db % 12 == 0 });

    for (int half = -6; half <= -1; ++half)
        limitRuler.marks.push_back ({ half * 0.5f, half % 2 == 0 || half == -1 });

    limitRuler.labelDecimals = 1;
    gateRuler.yOfDb  = linearYOf (-80.0f, 0.0f);
    limitRuler.yOfDb = linearYOf (params::limiterCeilingMin, params::limiterCeilingMax);
    gateRuler.currentDb  = [this] { auto* p = amp.apvts.getParameter (params::gateThreshold);
                                    return p->convertFrom0to1 (p->getValue()); };
    limitRuler.currentDb = [this] { auto* p = amp.apvts.getParameter (params::limiterCeiling);
                                    return p->convertFrom0to1 (p->getValue()); };

    addChildComponent (inRuler);    // went missing with the ceiling's ruler: a ladder with no
    addChildComponent (outRuler);   // parent cannot be shown, however often it is asked
    addChildComponent (gateRuler);
    addChildComponent (limitRuler);

    const auto summon = [] (DragRuler& ruler)
    {
        return [&ruler] (bool a) { ruler.setVisible (a); if (a) ruler.toFront (false); };
    };

    inColumn.onTrimDrag  = summon (inRuler);
    outColumn.onTrimDrag = summon (outRuler);
    gateRail.onDrag      = summon (gateRuler);
    limitRail.onDrag     = summon (limitRuler);

    // Devices came or went while the window was open: the engine re-reads the folder, then the
    // captured blocks rebuild their selectors from the lists that changed under them.
    setup.onDevicesChanged = [this]
    {
        amp.rescanDevices();
        faceplate.deviceChanged();
    };

    // The corner sets the zoom AND the width: the height decides the factor, and the aspect is free
    // between the design's own and `maxAspect` (AspectRange), the width laid out across.
    setConstrainer (&aspect);
    setResizable (true, true);
    applySizeLimits();

    // As large as the screen allows, up to the size the plugin WANTS to open at. Asking for 2x on a
    // display that cannot hold it does not give 2x — it gives whatever the window manager shrinks it
    // to, and reading that back as the next window's wish is how a plugin walks itself down to the
    // minimum over a few launches.
    float s = AmpProcessor::preferredScale;

    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto area = display->userArea;
        const float fits = juce::jmin ((float) area.getWidth()  / (float) minBaseWidth,
                                       (float) (area.getHeight() - titleBarAllowance) / (float) baseHeight());
        s = juce::jlimit (AmpProcessor::minScale, s, fits);
        amp.setEditorScale (s);
    }

    // It opens at the design's own width; the corner widens it from there.
    zoomNow = s;
    setSize (juce::roundToInt (minBaseWidth * s), juce::roundToInt (baseHeight() * s));

    // The one place this plugin may reach the network without a click, and only because a player
    // ticked the switch in the version popover: once a day at most, on the message thread, never in
    // a constructor a scanner runs. Not ticked, or checked already today — nothing happens at all.
    amp.updateChecker().checkIfDue();

    // THE SITE'S KIT: OrbitAmp's own face written out for darwinscat.com, when the environment names a
    // folder for it (WebFaceplate.h). A player never sets it, and without it nothing here happens.
    if (const auto folder = juce::SystemStats::getEnvironmentVariable ("ORBITAMP_WEB_EXPORT", {}); folder.isNotEmpty())
        WebFaceplate::exportAfterOpening (*this, juce::File (folder));

    // A live still of the face for a product page (WebFaceplate.h) — a dev tool, like the kit above.
    if (const auto folder = juce::SystemStats::getEnvironmentVariable ("ORBITAMP_LIVE_SHOT", {}); folder.isNotEmpty())
        WebFaceplate::liveShotAfterOpening (*this, juce::File (folder),
                                            juce::File (juce::SystemStats::getEnvironmentVariable ("ORBITAMP_LIVE_SHOT_DI", {})));
}

void AmpEditor::applyStripChoice()
{
    // The zoom and the width the window already has; only the height follows the rows.
    applySizeLimits();
    designW = juce::jlimit (minDesignWidth, maxDesignWidth(), designW);
    setSize (juce::roundToInt ((float) baseW() * zoomNow), juce::roundToInt ((float) baseHeight() * zoomNow));
}

void AmpEditor::applySizeLimits()
{
    const int h = baseHeight();

    aspect.lowAspect  = (double) minBaseWidth / (double) h;
    aspect.highAspect = juce::jmax (aspect.lowAspect, (double) (maxDesignWidth() + margin * 2) / (double) h);
    aspect.setSizeLimits (juce::roundToInt ((float) minBaseWidth * AmpProcessor::minScale),
                          juce::roundToInt ((float) h * AmpProcessor::minScale),
                          juce::roundToInt ((float) (maxDesignWidth() + margin * 2) * AmpProcessor::maxScale),
                          juce::roundToInt ((float) h * AmpProcessor::maxScale));
}

void AmpEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::ground);
}

bool AmpEditor::keyPressed (const juce::KeyPress& key)
{
    // Escape is the way out of anything that took the room over. The whole-screen theatre has
    // always answered it — it holds the keyboard while it runs — and the face-wide picture only
    // had its own fold glyph, which is a way out you have to go looking for.
    if (key == juce::KeyPress::escapeKey)
        return faceplate.foldPicture();

    // Space is the demo player's transport, like everywhere else sound is judged — but ONLY while
    // the player is on show. Hidden, it is not ours: in a DAW space is the host's play/stop, and a
    // plugin window that has the focus must not eat it — nor start a loop nobody can see, which
    // would replace the guitar at the input. Unclaimed, the key goes on to the host. Text editors
    // keep their spaces — a focused editor consumes the key before it ever reaches us.
    if (key == juce::KeyPress::spaceKey && showDemo)
    {
        amp.demo.setPlaying (! amp.demo.isPlaying());
        return true;
    }

    // H: the hum, OFF and back (prefs::humFlip) — Р under a Russian layout, the same key. G: the
    // gate's On, the sound's switch and so an undo step, as its menu sets it — П. H only while the build
    // has HUM (Features.h); without it the key is not ours, and goes on to the host.
    if (! key.getModifiers().isAnyModifierKeyDown())
    {
        const auto letter = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) key.getKeyCode());
        if (features::hum && (letter == 'H' || letter == 0x0420))
        {
            prefs::setHum (amp.product(), prefs::humFlip (amp.product()));
            return true;
        }
        if (letter == 'G' || letter == 0x041f)
        {
            if (auto* on = amp.apvts.getParameter (params::gateOn))
            {
                on->beginChangeGesture();
                on->setValueNotifyingHost (on->getValue() > 0.5f ? 0.0f : 1.0f);
                on->endChangeGesture();
            }
            return true;
        }
    }

    return false;
}

void AmpEditor::applyViewPrefs()
{
    faceplate.setEqRowOnTop (prefs::getBool (amp.product(), prefs::eqRowOnTop, false));
    showDemo   = params::demoLoopsPresent() && prefs::getBool (amp.product(), prefs::showDemo, false);
    showGlyphs = prefs::getBool (amp.product(), prefs::showGlyphs, false);
    applyStripChoice();
    resized();
    repaint();
}

void AmpEditor::showLimiterMenu (juce::Point<int> screenPos)
{
    auto* on   = amp.apvts.getParameter (params::limiterOn);
    auto* ceil = amp.apvts.getParameter (params::limiterCeiling);

    const bool  isOn = on->getValue() > 0.5f;
    const float c    = ceil->convertFrom0to1 (ceil->getValue());

    const auto matches = [&] (float v) { return isOn && std::abs (c - v) < 0.05f; };

    juce::PopupMenu m;
    // The number in the header, for the same reason the gate's carries one: the ceiling is
    // continuous and these are three points on it, so a value from a preset or an automation lane
    // can sit between them with no item ticked and nothing anywhere to say what it is.
    m.addSectionHeader (isOn ? "LIMITER  " + juce::String (c, 1) + " DB"
                             : juce::String ("LIMITER  OFF"));
    // OFF on top, ruled off, and only while it is on; a ceiling picked below switches it on (Oleh, 24.10).
    if (isOn)
    {
        m.addItem (1, "OFF");
        m.addSeparator();
    }

    m.addItem (2, "SAFETY  -0.3",  true, matches (-0.3f));
    m.addItem (3, "NORMAL  -1.0",  true, matches (-1.0f));
    m.addItem (4, "TIGHT   -3.0",  true, matches (-3.0f));


    m.showMenuAsync (juce::PopupMenu::Options()
                         .withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 }),
                     // The parameters outlive this window — the processor owns them — but the
                     // editor does not: closing it with the menu open would leave the RESET branch
                     // reaching through a dead `this`. Guarded the way the gear's menu already is.
                     [safe = juce::Component::SafePointer<AmpEditor> (this), on, ceil] (int r)
                     {
                         if (r == 0)
                             return;

                         const auto set = [] (juce::RangedAudioParameter* p, float plain)
                         {
                             p->beginChangeGesture();
                             p->setValueNotifyingHost (p->convertTo0to1 (plain));
                             p->endChangeGesture();
                         };

                         // OFF is offered only to a limiter that is on; a ceiling is the way back on.
                         if (r == 1)
                         {
                             set (on, 0.0f);
                             return;
                         }


                         set (on, 1.0f);
                         set (ceil, r == 2 ? -0.3f : r == 3 ? -1.0f : -3.0f);
                     });
}

/** A RIGHT CLICK ON THE INPUT COLUMN: the hum the input carries, and what to take off it. It is the
    machine's, not the sound's (prefs::hum) — a preset neither carries it nor moves it, undo steps over
    it, and every window and engine of this product follow it from their next look and their next
    block. AUTO's line says what it decided, once it has. */
void AmpEditor::showInputMenu (juce::Point<int> screenPos)
{
    // The switch, then the modes (ui/HumMenu.h), in this menu's capitals.
    const int  heard = amp.humHeard();
    const auto lines = prefs::humMenu (amp.product(), heard, humLearner.isLearning());

    juce::PopupMenu m;
    m.addSectionHeader ("HUM  " + prefs::humState (amp.product(), heard).toUpperCase());
    for (size_t i = 0; i < lines.size(); ++i)
        m.addItem ((int) i + 1, lines[i].text.toUpperCase(), true, lines[i].ticked);

    // How hard it works — TEMPORARY, for tuning by ear (ui/HumTuning.h): each a submenu, its value in
    // its title; knob k's values answer as 100 (k + 1) + their index.
    const auto knobs = prefs::humtune::knobs (amp.product());
    for (size_t k = 0; k < knobs.size(); ++k)
    {
        juce::PopupMenu values;
        for (int i = 0; i < knobs[k].count; ++i)
            values.addItem (100 * ((int) k + 1) + i, knobs[k].say (knobs[k].values[i]).toUpperCase(), true,
                            juce::exactlyEqual (knobs[k].values[i], knobs[k].now));
        m.addSubMenu ((juce::String (knobs[k].name) + "  " + knobs[k].say (knobs[k].now)).toUpperCase(), values);
    }

    // The answer reaches this window only to start the hum's LEARN, and through a SafePointer: a window
    // closed under the open menu is left alone.
    m.showMenuAsync (juce::PopupMenu::Options()
                         .withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 }),
                     [safe = juce::Component::SafePointer<AmpEditor> (this), lines, knobs, &product = amp.product()] (int r)
                     {
                         if (r > 0 && r <= (int) lines.size() && lines[(size_t) r - 1].isLearn)
                         {
                             if (safe != nullptr)
                             {
                                 safe->gateConsole.cancel();
                                 safe->humLearner.start();
                             }
                         }
                         else if (r > 0 && r <= (int) lines.size())
                             prefs::humPick (product, lines[(size_t) r - 1]);
                         else if (const int k = r / 100 - 1, i = r % 100;
                                  k >= 0 && k < (int) knobs.size() && i < knobs[(size_t) k].count)
                             knobs[(size_t) k].choose (knobs[(size_t) k].values[i]);
                     });
}

void AmpEditor::resized()
{
    // The HEIGHT is the zoom; the width, in design units, is what is left to lay out across.
    const float s = (float) getHeight() / (float) baseHeight();
    amp.setEditorScale (s);
    zoomNow = s;
    designW = juce::jlimit (minDesignWidth, maxDesignWidth(),
                            juce::roundToInt ((float) getWidth() / s) - margin * 2);

    // Bounds stay in design units; the transform does all the scaling, margins included.
    const auto zoom = juce::AffineTransform::scale (s);

    chrome.setBounds (margin, margin, designW, Chrome::designHeight);
    chrome.setTransform (zoom);

    // The gate's IN sliver takes the faceplate row's left edge; the faceplate wears the rest.
    const int faceplateY = margin + Chrome::designHeight + headerGap;

    // The layout strip: the chain laid flat on the header's second line, beside the brand's marks,
    // always there. In front of the header, whose component spans both lines. Its first arrow's
    // tail stands under the wordmark's first letter: no lane inset, the strip that tail's air left.
    // The last arrow's point ends under the gear, the first line's last button.
    {
        layoutStrip->leftPad = 0;
        const int x = chrome.secondLineX() - LayoutStrip::tailAir;
        layoutStrip->arrowsEndX = chrome.gearBounds().reduced (2).getRight() - x;
        layoutStrip->setBounds (margin + x, margin + Chrome::firstLineH, designW - x,
                                LayoutStrip::designHeight);
        layoutStrip->setTransform (zoom);
    }

    // The faceplate is only as tall as its LAYOUT stands it — a collapsed row is not here.
    const int faceplateH = faceplate.currentHeight();

    // The gutters stand between the SAME two lines the blocks' frames do, not between the
    // faceplate's own edges — a block is inset inside the lane and its box starts lower still,
    // under the switch that rides the top border.
    const int gutterY = faceplateY + FaceplateView::contentTop;
    // Never negative: with every block stood down the panel closes up, and a column asked for a
    // negative height is a component with no honest size.
    const int gutterH = juce::jmax (0, faceplateH - FaceplateView::contentTop
                                                  - FaceplateView::contentBottom);

    // The side columns always stand: the way in and the way out, each with its guard's rail on the
    // inner side and its guard's lamp under both — the lamp's foot on the blocks' foot.
    const int edgeInset = 12;
    const int sideW = VolumeColumn::designWidth + GuardRail::designWidth;
    const int colL  = sideW + chromeGap;
    const int colR  = sideW + chromeGap;
    const int lampGap = 6;
    const int railH = juce::jmax (0, gutterH - GuardLamp::designHeight - lampGap);
    const int lampY = gutterY + gutterH - GuardLamp::designHeight;

    inColumn.setBounds (margin, gutterY, VolumeColumn::designWidth, railH);
    inColumn.setTransform (zoom);
    gateRail.setBounds (margin + VolumeColumn::designWidth, gutterY, GuardRail::designWidth + GuardRail::reach, railH);
    gateRail.setTransform (zoom);
    gateLamp.setBounds (margin, lampY, sideW, GuardLamp::designHeight);
    gateLamp.setTransform (zoom);

    outColumn.setBounds (margin + designW - VolumeColumn::designWidth, gutterY,
                        VolumeColumn::designWidth, railH);
    outColumn.setTransform (zoom);
    limitRail.setBounds (margin + designW - sideW - GuardRail::reach, gutterY,
                         GuardRail::designWidth + GuardRail::reach, railH);
    limitRail.setTransform (zoom);
    limitLamp.setBounds (margin + designW - sideW, lampY, sideW, GuardLamp::designHeight);
    limitLamp.setTransform (zoom);

    faceplate.setBounds (margin + colL, faceplateY,
                         designW - colL - colR, faceplateH);
    faceplate.setTransform (zoom);

    // The bottom row: the always-on needle in the left half, the footer's facts in the right. Both
    // stand off the window's edges the way the rails above them do.
    const int badgeInset = edgeInset;
    const int rowY   = faceplateY + faceplateH + chromeGap;
    const int rowW   = designW - 2 * badgeInset;
    const int tunerW = (rowW - chromeGap) / 2;

    tunerStrip.setBounds (margin + badgeInset, rowY + (bottomRowH - TunerStrip::designHeight) / 2,
                          tunerW, TunerStrip::designHeight);
    tunerStrip.setTransform (zoom);

    // The summoned rulers: the same vertical extent as their columns, standing toward the centre,
    // clear of the guards' grips that reach that way.
    for (auto* ruler : { &inRuler, &gateRuler })
    {
        ruler->setBounds (margin + sideW + GuardRail::reach + 2, gutterY, 56, railH);
        ruler->setTransform (zoom);
    }

    for (auto* ruler : { &outRuler, &limitRuler })
    {
        ruler->setBounds (margin + designW - sideW - GuardRail::reach - 58, gutterY, 56, railH);
        ruler->setTransform (zoom);
    }

    // The learn sheet: half the plugin, centred over the faceplate.
    learnOverlay.setBounds (margin + designW / 6, faceplateY + 60,
                            designW * 2 / 3, 330);
    learnOverlay.setTransform (zoom);

    footer.setBounds (margin + badgeInset + tunerW + chromeGap, rowY + (bottomRowH - Footer::designHeight) / 2,
                      rowW - tunerW - chromeGap, Footer::designHeight);
    footer.setTransform (zoom);

    // TEMPORARY — the audition player and the glyph review strip, under the bottom row, each only
    // when this player switched it on; the window is as tall as what it shows.
    int stripY = rowY + bottomRowH + chromeGap;

    demoStrip.setVisible (showDemo);
    if (showDemo)
    {
        demoStrip.setBounds (margin, stripY, designW, DemoStrip::designHeight);
        demoStrip.setTransform (zoom);
        stripY += DemoStrip::designHeight;
    }

    glyphs.setVisible (showGlyphs);
    if (showGlyphs)
    {
        glyphs.setBounds (margin, stripY, designW, GlyphPreview::designHeight);
        glyphs.setTransform (zoom);
    }

    // The overlay covers the whole editor, margins included, in the same design units.
    setup.setBounds (0, 0, baseW(), baseHeight());
    setup.setTransform (zoom);
    devices.setBounds (0, 0, baseW(), baseHeight());
    devices.setTransform (zoom);

}

// OrbitAmp itself — its name, this face, its first start — is said in OrbitAmpStart.cpp.

} // namespace orbitamp

