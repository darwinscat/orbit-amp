// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "PluginEditor.h"

#include "ui/Prefs.h"

namespace orbitamp
{

namespace
{
    /** The LAYOUT popup's subjects, in chain order: the pref that remembers the choice, the name
        on the row, the faceplate's block, which side of the colour grammar its switch wears, the
        power to put out when it hides, and whether it ships shown. */
    struct LayoutBlock
    {
        const juce::Identifier& pref;
        const char* name;
        FaceplateView::Block block;
        bool captured;
        const char* onParam;
        bool defaultShown;
    };

    // The power amp is NOT in the table: it is not ready — no pack ships — so it is benched
    // whole: no tile in the chooser, no tab in Setup, its power forced out at open. It returns
    // here when it returns for real.
    const LayoutBlock layoutBlocks[] = {
        { prefs::showBoost,  "BOOST",  FaceplateView::Block::boost,   true,  params::boostOn,  true  },
        { prefs::showPreamp, "PREAMP", FaceplateView::Block::preamp,  true,  params::preampOn, true  },
        { prefs::showDelay,  "DELAY",  FaceplateView::Block::delay,   false, params::delayOn,  false },
        { prefs::showReverb, "REVERB", FaceplateView::Block::reverb,  false, params::reverbOn, true  },
        { prefs::showCab,    "CAB IR", FaceplateView::Block::cabinet, true,  params::cabOn,    true  },
    };

    std::vector<LayoutStrip::Row> layoutRows()
    {
        std::vector<LayoutStrip::Row> rows;
        for (const auto& b : layoutBlocks)
            rows.push_back ({ b.name, b.captured ? theme::orange : theme::violet,
                              prefs::getBool (b.pref, b.defaultShown) });
        return rows;
    }
}

AmpEditor::AmpEditor (AmpProcessor& p)
    : juce::AudioProcessorEditor (&p), amp (p), chrome (p), faceplate (p),
      gateStrip (p.gateKeyDb, p.gateMeterDb, p.inClip, *p.apvts.getParameter (params::gateThreshold),
                 *p.apvts.getParameter (params::inTrim), *p.apvts.getParameter (params::gateOn),
                 *p.apvts.getParameter (params::gateDecay), *p.apvts.getParameter (params::gatePos)),
      outStrip (p.outDb, p.outClip, *p.apvts.getParameter (params::outTrim),
                *p.apvts.getParameter (params::limiterCeiling),
                *p.apvts.getParameter (params::limiterOn)),
      gateBadge ("Gate", *p.apvts.getParameter (params::gateOn), p.gateMeterDb, 40.0f),
      limitBadge ("Limit", *p.apvts.getParameter (params::limiterOn), p.limiterGrDb, 6.0f),
      tunerStrip (p.tunerEar), footer (p), demoStrip (p)
{
    setWantsKeyboardFocus (true);

    addAndMakeVisible (chrome);
    addAndMakeVisible (faceplate);

    // The hints wear the panel's own colours, not the stock yellow.
    tooltips.setColour (juce::TooltipWindow::backgroundColourId, theme::panel2);
    tooltips.setColour (juce::TooltipWindow::textColourId, theme::txDim);
    tooltips.setColour (juce::TooltipWindow::outlineColourId, theme::hair2);
    addAndMakeVisible (gateStrip);
    addAndMakeVisible (outStrip);
    addAndMakeVisible (gateBadge);
    addAndMakeVisible (limitBadge);
    addAndMakeVisible (tunerStrip);
    addAndMakeVisible (footer);
    // The two TEMPORARY strips under the footer, off unless this player asked for them (the gear).
    // The demo needs its loops on disk — a machine without them has no player to show.
    showDemo   = params::demoLoopsPresent() && prefs::getBool (prefs::showDemo, false);
    showGlyphs = prefs::getBool (prefs::showGlyphs, false);

    for (const auto& b : layoutBlocks)
        faceplate.setShown (b.block, prefs::getBool (b.pref, b.defaultShown));

    // The benched power amp: never shown, and its power put out even if a session saved it on —
    // an invisible block must not colour the sound either.
    if (auto* p = amp.apvts.getParameter (params::powerOn); p != nullptr && p->getValue() > 0.5f)
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (0.0f);
        p->endChangeGesture();
    }

    addChildComponent (demoStrip);
    addChildComponent (glyphs);
    addChildComponent (setup);       // hidden until the toolbar's gear opens it

    chrome.onGear = [this] (juce::Point<int> pos) { showGearMenu (pos); };

    // The layout strip: always there, the ONE place blocks are stood down and brought back.
    layoutStrip = std::make_unique<LayoutStrip> (layoutRows());
    layoutStrip->onToggle = [this] (int i, bool on)
    {
        applyLayoutToggle (i, on);
        layoutStrip->setRowOn (i, on);
    };
    addAndMakeVisible (*layoutStrip);

    // The badges' clicks mean MENU — the whole device in one pick. There is nowhere to "open" any
    // more and nothing to open TO: every block wears its own face on the panel, and the gate's own
    // controls, position included, live in this menu.
    gateBadge.onOpen = [this] (juce::Point<int> pos) { gateStrip.showPresetMenu (pos); };
    gateBadge.latch  = &amp.gateWorked;

    // The measurement, projected: the overlay reads the strip's own trace.
    learnOverlay.trace      = &gateStrip.learnTraceRef();
    learnOverlay.totalTicks = GateStrip::learnTotalTicks;
    learnOverlay.pendingDb  = [this] { return gateStrip.learnPendingDb(); };
    gateStrip.onLearnBegin  = [this] { learnOverlay.begin(); };
    gateStrip.onLearnDone   = [this] (const juce::String& v) { learnOverlay.finish (v); };
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

    // The ceiling's own ladder: -0.1 at the top of the rail's top third, halves down to -3.
    ceilRuler.ticksOnLeft   = false;
    ceilRuler.labelDecimals = 1;
    ceilRuler.accent        = theme::lilac;
    ceilRuler.marks = { { -0.1f, true }, { -0.5f, false }, { -1.0f, true }, { -1.5f, false },
                        { -2.0f, true }, { -2.5f, false }, { -3.0f, true } };
    ceilRuler.yOfDb = [] (juce::Rectangle<float> r, float v)
    {
        const float t = (params::limiterCeilingMax - v)
                      / (params::limiterCeilingMax - params::limiterCeilingMin);
        return r.getY() + 14.0f + t * (r.getHeight() / 3.0f);
    };
    ceilRuler.currentDb = [this] { auto* p = amp.apvts.getParameter (params::limiterCeiling);
                                   return p->convertFrom0to1 (p->getValue()); };

    addChildComponent (inRuler);
    addChildComponent (outRuler);
    addChildComponent (ceilRuler);

    gateStrip.onTrimDrag = [this] (bool a) { inRuler.setVisible (a); if (a) inRuler.toFront (false); };
    outStrip.onTrimDrag  = [this] (bool a) { outRuler.setVisible (a); if (a) outRuler.toFront (false); };
    outStrip.onCeilDrag  = [this] (bool a) { ceilRuler.setVisible (a); if (a) ceilRuler.toFront (false); };
    limitBadge.onOpen = [this] (juce::Point<int> pos) { showLimiterMenu (pos); };
    limitBadge.latch  = &amp.limiterWorked;
    outStrip.onMenu   = [this] (juce::Point<int> pos) { showLimiterMenu (pos); };

    // Devices came or went while the window was open: the engine re-reads the folder, then the
    // captured blocks rebuild their selectors from the lists that changed under them.
    setup.onDevicesChanged = [this]
    {
        amp.rescanDevices();
        faceplate.deviceChanged();
    };

    // Dragging the corner IS the zoom: the aspect is locked, so width alone determines the factor.
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) baseWidth / (double) baseHeight());
    setResizeLimits (juce::roundToInt (baseWidth    * AmpProcessor::minScale),
                     juce::roundToInt (baseHeight() * AmpProcessor::minScale),
                     juce::roundToInt (baseWidth    * AmpProcessor::maxScale),
                     juce::roundToInt (baseHeight() * AmpProcessor::maxScale));

    // As large as the screen allows, up to the size the plugin WANTS to open at. Asking for 2x on a
    // display that cannot hold it does not give 2x — it gives whatever the window manager shrinks it
    // to, and reading that back as the next window's wish is how a plugin walks itself down to the
    // minimum over a few launches.
    float s = AmpProcessor::preferredScale;

    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto area = display->userArea;
        const float fits = juce::jmin ((float) area.getWidth()  / (float) baseWidth,
                                       (float) (area.getHeight() - titleBarAllowance) / (float) baseHeight());
        s = juce::jlimit (AmpProcessor::minScale, s, fits);
        amp.setEditorScale (s);
    }

    setSize (juce::roundToInt (baseWidth * s), juce::roundToInt (baseHeight() * s));
}

void AmpEditor::showGearMenu (juce::Point<int> screenPos)
{
    juce::PopupMenu m;
    m.addItem (1, "SETUP...");
    m.addSeparator();
    m.addItem (5, "SHOW SPECTRA",       true, prefs::spectraShown());
    if (params::demoLoopsPresent())     // no loops on disk — no player, and no offer of one
        m.addItem (2, "SHOW DEMO PLAYER", true, showDemo);
    m.addItem (3, "SHOW DEVICE GLYPHS", true, showGlyphs);

    m.showMenuAsync (juce::PopupMenu::Options()
                         .withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 }),
                     [safe = juce::Component::SafePointer<AmpEditor> (this)] (int r)
                     {
                         if (safe == nullptr || r == 0)
                             return;

                         if (r == 1)
                         {
                             safe->setup.open();
                             return;
                         }

                         if (r == 5)
                         {
                             prefs::setSpectraShown (! prefs::spectraShown());
                             safe->repaint();
                             return;
                         }

                         bool& flag = r == 2 ? safe->showDemo : safe->showGlyphs;
                         flag = ! flag;
                         prefs::setBool (r == 2 ? prefs::showDemo : prefs::showGlyphs, flag);
                         safe->applyStripChoice();
                     });
}

void AmpEditor::applyLayoutToggle (int index, bool on)
{
    const auto& b = layoutBlocks[(size_t) index];
    prefs::setBool (b.pref, on);
    faceplate.setShown (b.block, on);
    applyStripChoice();   // a row that emptied (or refilled) collapses the window with it

    // A hidden block must not colour the sound: its power goes out with it — but it REMEMBERS.
    // A block that was playing when it left the panel comes back playing; one that stood dark
    // comes back dark. A block this session never hid comes back ON: you just added it to the
    // chain, and a chain link that arrives dead is a puzzle, not a feature.
    if (auto* p = amp.apvts.getParameter (b.onParam))
    {
        if (! on)
        {
            blockWasOn[(size_t) index] = p->getValue() > 0.5f;
            p->beginChangeGesture();
            p->setValueNotifyingHost (0.0f);
            p->endChangeGesture();
        }
        else
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (blockWasOn[(size_t) index] ? 1.0f : 0.0f);
            p->endChangeGesture();
        }
    }
}

void AmpEditor::applyStripChoice()
{
    const float s = (float) getWidth() / (float) baseWidth;

    getConstrainer()->setFixedAspectRatio ((double) baseWidth / (double) baseHeight());
    setResizeLimits (juce::roundToInt (baseWidth    * AmpProcessor::minScale),
                     juce::roundToInt (baseHeight() * AmpProcessor::minScale),
                     juce::roundToInt (baseWidth    * AmpProcessor::maxScale),
                     juce::roundToInt (baseHeight() * AmpProcessor::maxScale));
    setSize (juce::roundToInt (baseWidth * s), juce::roundToInt (baseHeight() * s));
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

    // Space is the transport, like everywhere else sound is judged. Text editors keep their
    // spaces — a focused editor consumes the key before it ever reaches us.
    if (key == juce::KeyPress::spaceKey)
    {
        amp.demo.setPlaying (! amp.demo.isPlaying());
        return true;
    }

    return false;
}

void AmpEditor::showLimiterMenu (juce::Point<int> screenPos)
{
    auto* on   = amp.apvts.getParameter (params::limiterOn);
    auto* ceil = amp.apvts.getParameter (params::limiterCeiling);

    const bool  isOn = on->getValue() > 0.5f;
    const float c    = ceil->convertFrom0to1 (ceil->getValue());

    const auto matches = [&] (float v) { return isOn && std::abs (c - v) < 0.05f; };

    juce::PopupMenu m;
    m.addSectionHeader ("LIMITER");
    m.addItem (1, "OFF",           true, ! isOn);
    m.addItem (2, "SAFETY  -0.3",  true, matches (-0.3f));
    m.addItem (3, "NORMAL  -1.0",  true, matches (-1.0f));
    m.addItem (4, "TIGHT   -3.0",  true, matches (-3.0f));

    m.addSeparator();
    m.addSectionHeader ("VOLUME");
    m.addItem (7, "RESET");

    m.showMenuAsync (juce::PopupMenu::Options()
                         .withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 }),
                     [this, on, ceil] (int r)
                     {
                         if (r == 0)
                             return;

                         const auto set = [] (juce::RangedAudioParameter* p, float plain)
                         {
                             p->beginChangeGesture();
                             p->setValueNotifyingHost (p->convertTo0to1 (plain));
                             p->endChangeGesture();
                         };

                         // OFF is a toggle here too — a menu that can only kill is no menu.
                         if (r == 1)
                         {
                             set (on, on->getValue() > 0.5f ? 0.0f : 1.0f);
                             return;
                         }

                         if (r == 7)
                         {
                             set (amp.apvts.getParameter (params::outTrim), 0.0f);
                             return;
                         }

                         set (on, 1.0f);
                         set (ceil, r == 2 ? -0.3f : r == 3 ? -1.0f : -3.0f);
                     });
}

void AmpEditor::resized()
{
    const float s = (float) getWidth() / (float) baseWidth;
    amp.setEditorScale (s);

    // Bounds stay in design units; the transform does all the scaling, margins included.
    const auto zoom = juce::AffineTransform::scale (s);

    chrome.setBounds (margin, margin, FaceplateView::designWidth, Chrome::designHeight);
    chrome.setTransform (zoom);

    // The gate's IN sliver takes the faceplate row's left edge; the faceplate wears the rest.
    int faceplateY = margin + Chrome::designHeight + headerGap;

    // The layout strip: the chain laid flat between the toolbar and the device, always there.
    layoutStrip->setBounds (margin, faceplateY, FaceplateView::designWidth,
                            LayoutStrip::designHeight);
    layoutStrip->setTransform (zoom);
    faceplateY += LayoutStrip::designHeight;

    // The faceplate is only as tall as its LAYOUT stands it — a collapsed row is not here.
    const int faceplateH = faceplate.currentHeight();

    // The gutters stand between the SAME two lines the blocks' frames do, not between the
    // faceplate's own edges — a block is inset inside the lane and its box starts lower still,
    // under the switch that rides the top border.
    const int gutterY = faceplateY + FaceplateView::contentTop;
    const int gutterH = faceplateH - FaceplateView::contentTop - FaceplateView::contentBottom;

    gateStrip.setBounds (margin, gutterY, GateStrip::designWidth, gutterH);
    gateStrip.setTransform (zoom);

    outStrip.setBounds (margin + FaceplateView::designWidth - OutStrip::designWidth, gutterY,
                        OutStrip::designWidth, gutterH);
    outStrip.setTransform (zoom);

    faceplate.setBounds (margin + GateStrip::designWidth + chromeGap, faceplateY,
                         FaceplateView::designWidth - GateStrip::designWidth - OutStrip::designWidth
                             - 2 * chromeGap,
                         faceplateH);
    faceplate.setTransform (zoom);

    // The always-on needle, full width, above the footer's facts.
    const int tunerY = faceplateY + faceplateH + chromeGap;
    gateBadge.setBounds (margin, tunerY, TallyBadge::designWidth, TunerStrip::designHeight);
    gateBadge.setTransform (zoom);

    limitBadge.setBounds (margin + FaceplateView::designWidth - TallyBadge::designWidth, tunerY,
                          TallyBadge::designWidth, TunerStrip::designHeight);
    limitBadge.setTransform (zoom);

    // The summoned rulers: the same vertical extent as their columns, standing toward the centre.
    inRuler.setBounds (margin + GateStrip::designWidth + 2, gutterY, 56, gutterH);
    inRuler.setTransform (zoom);
    outRuler.setBounds (margin + FaceplateView::designWidth - OutStrip::designWidth - 58, gutterY,
                        56, gutterH);
    outRuler.setTransform (zoom);
    ceilRuler.setBounds (outRuler.getBounds());
    ceilRuler.setTransform (zoom);

    // The learn sheet: half the plugin, centred over the faceplate.
    learnOverlay.setBounds (margin + FaceplateView::designWidth / 6, faceplateY + 60,
                            FaceplateView::designWidth * 2 / 3, 330);
    learnOverlay.setTransform (zoom);

    tunerStrip.setBounds (margin + TallyBadge::designWidth + chromeGap, tunerY,
                          FaceplateView::designWidth - 2 * (TallyBadge::designWidth + chromeGap),
                          TunerStrip::designHeight);
    tunerStrip.setTransform (zoom);

    const int footerY = tunerY + TunerStrip::designHeight + chromeGap;
    footer.setBounds (margin, footerY, FaceplateView::designWidth, Footer::designHeight);
    footer.setTransform (zoom);

    // TEMPORARY — the audition player and the glyph review strip, under the footer, each only
    // when this player switched it on; the window is as tall as what it shows.
    int stripY = footerY + Footer::designHeight;

    demoStrip.setVisible (showDemo);
    if (showDemo)
    {
        demoStrip.setBounds (margin, stripY, FaceplateView::designWidth, DemoStrip::designHeight);
        demoStrip.setTransform (zoom);
        stripY += DemoStrip::designHeight;
    }

    glyphs.setVisible (showGlyphs);
    if (showGlyphs)
    {
        glyphs.setBounds (margin, stripY, FaceplateView::designWidth, GlyphPreview::designHeight);
        glyphs.setTransform (zoom);
    }

    // The overlay covers the whole editor, margins included, in the same design units.
    setup.setBounds (0, 0, baseWidth, baseHeight());
    setup.setTransform (zoom);
}

} // namespace orbitamp
