// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "PluginEditor.h"

#include "ui/Prefs.h"

namespace orbitamp
{

namespace
{
    /** The strip's block rows, in chain order: the name on the arrow, the faceplate's block,
        which side of the colour grammar it wears, and the parameter that IS its presence — one
        flag for standing-on-the-panel and being-in-the-sound, carried by the save, the history,
        and the registers alike. */
    struct LayoutBlock
    {
        const char* name;
        FaceplateView::Block block;
        bool captured;
        const char* onParam;
    };

    // The power amp is NOT in the table: it is not ready — no pack ships — so it is benched
    // whole: no tile in the chooser, no tab in Setup, its power forced out at open. It returns
    // here when it returns for real.
    const LayoutBlock layoutBlocks[] = {
        { "BOOST",  FaceplateView::Block::boost,   true,  params::boostOn  },
        { "PREAMP", FaceplateView::Block::preamp,  true,  params::preampOn },
        { "DELAY",  FaceplateView::Block::delay,   false, params::delayOn  },
        { "REVERB", FaceplateView::Block::reverb,  false, params::reverbOn },
        { "CAB IR", FaceplateView::Block::cabinet, true,  params::cabOn    },
    };

    std::vector<LayoutStrip::Row> layoutRows()
    {
        // The initial `on` is a placeholder: the attachments' first echo dresses every row and
        // the panel from the parameters the moment the strip stands.
        std::vector<LayoutStrip::Row> rows;
        for (const auto& b : layoutBlocks)
            rows.push_back ({ b.name, b.captured ? theme::orange : theme::violet, true });
        return rows;
    }

    // The strip's row order: the tuner listening at the door, the gate right after it, the
    // sound blocks, the limiter before the way out.
    constexpr int rowTuner = 0, rowGate = 1, rowFirstBlock = 2;
    inline int rowLimit() { return rowFirstBlock + (int) std::size (layoutBlocks); }
}

AmpEditor::AmpEditor (AmpProcessor& p)
    : juce::AudioProcessorEditor (&p), amp (p), chrome (p), faceplate (p),
      gateStrip (p.gateKeyDb, p.gateMeterDb, p.inClip, *p.apvts.getParameter (params::gateThreshold),
                 *p.apvts.getParameter (params::inTrim), *p.apvts.getParameter (params::gateOn),
                 *p.apvts.getParameter (params::gateDecay), *p.apvts.getParameter (params::gatePos)),
      outStrip (p.outDb, p.outClip, *p.apvts.getParameter (params::outTrim),
                *p.apvts.getParameter (params::limiterCeiling),
                *p.apvts.getParameter (params::limiterOn)),
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
    addAndMakeVisible (tunerStrip);
    addAndMakeVisible (footer);
    // The two TEMPORARY strips under the footer, off unless this player asked for them (the gear).
    // The demo needs its loops on disk — a machine without them has no player to show.
    showDemo   = params::demoLoopsPresent() && prefs::getBool (prefs::showDemo, false);
    showGlyphs = prefs::getBool (prefs::showGlyphs, false);

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
    addChildComponent (about);       // ...and so is ABOUT, from the same menu

    chrome.onGear = [this] (juce::Point<int> pos) { showGearMenu (pos); };

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

        const auto area = display->userArea;
        const float fit = juce::jlimit (AmpProcessor::minScale, AmpProcessor::maxScale,
                                        juce::jmin ((float) area.getWidth() / (float) baseWidth,
                                                    (float) (area.getHeight() - titleBarAllowance)
                                                        / (float) baseHeight()));
        const float cur = (float) getWidth() / (float) baseWidth;

        if (std::abs (cur - fit) < 0.01f && scaleBeforeFull > 0.0f)
        {
            setSize (juce::roundToInt (baseWidth * scaleBeforeFull),
                     juce::roundToInt (baseHeight() * scaleBeforeFull));
            scaleBeforeFull = -1.0f;
        }
        else
        {
            scaleBeforeFull = cur;
            setSize (juce::roundToInt (baseWidth * fit), juce::roundToInt (baseHeight() * fit));
        }

        if (auto* window = dynamic_cast<juce::ResizableWindow*> (tl))
            window->setCentrePosition (area.getCentre());
    };

    // The layout strip: always there, the ONE place anything is stood down and brought back —
    // the whole path as arrows, the service links wearing their own lights.
    tunerShown = prefs::getBool (prefs::showTuner, true);

    {
        std::vector<LayoutStrip::Row> rows;
        rows.push_back ({ "TUNER", theme::violet, tunerShown });
        rows.push_back ({ "GATE", theme::violet, true,
                          [this]
                          {
                              // Silence is not work: with nothing at the key (the -90 floor)
                              // the gate has nothing to press, and a light that burns all
                              // night means nothing by morning.
                              if (amp.gateKeyDb.load() <= -89.5f)
                                  return 0.0f;

                              return juce::jlimit (0.0f, 1.0f, -amp.gateMeterDb.load() / 40.0f);
                          },
                          [this] { return amp.gateWorked.load(); },
                          true, true });
        for (auto& r : layoutRows())
            rows.push_back (std::move (r));
        rows.push_back ({ "LIMIT", theme::violet, true,
                          [this] { return juce::jlimit (0.0f, 1.0f, -amp.limiterGrDb.load() / 6.0f); },
                          [this] { return amp.limiterWorked.load(); },
                          true, true });

        layoutStrip = std::make_unique<LayoutStrip> (std::move (rows));
    }

    layoutStrip->onToggle = [this] (int i, bool on)
    {
        if (i == rowTuner)
        {
            applyTunerToggle (on);
            layoutStrip->setRowOn (i, on);
            return;
        }

        // THE switch: the arrow writes the block's own parameter, so the save, the history,
        // and the registers all carry the click — the panel follows through the echo below.
        if (auto* p = amp.apvts.getParameter (layoutBlocks[(size_t) (i - rowFirstBlock)].onParam))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (on ? 1.0f : 0.0f);
            p->endChangeGesture();
        }
    };

    // The echo that makes it ONE fact: wherever a block's power moves — the strip, an undo, a
    // loaded preset, a register switch, host automation — the panel re-splits to match.
    for (int i = 0; i < (int) std::size (layoutBlocks); ++i)
    {
        blockRowAtts.push_back (std::make_unique<juce::ParameterAttachment> (
            *amp.apvts.getParameter (layoutBlocks[(size_t) i].onParam),
            [this, i] (float v)
            {
                const bool on = v > 0.5f;
                faceplate.setShown (layoutBlocks[(size_t) i].block, on);
                layoutStrip->setRowOn (rowFirstBlock + i, on);
                applyStripChoice();   // an emptied or refilled row moves the window with it
            }));

        blockRowAtts.back()->sendInitialUpdate();
    }

    // The guards' arrows ARE their consoles: any click opens the menu (OFF is its first item),
    // and the look clears the latched dot, the way a look at the badge used to.
    layoutStrip->onRowMenu = [this] (int i, juce::Point<int> pos)
    {
        if (i == rowGate)
        {
            amp.gateWorked.store (false);
            gateStrip.showPresetMenu (pos, false);   // the trim's RESET stays the column's door
        }
        else if (i == rowLimit())
        {
            amp.limiterWorked.store (false);
            showLimiterMenu (pos, false);
        }
    };

    // The guards' arrows dim with their own switches — a guard turned OFF in its menu reads
    // dark in the strip, the way a hidden block does.
    gateRowAtt = std::make_unique<juce::ParameterAttachment> (
        *amp.apvts.getParameter (params::gateOn),
        [this] (float v) { layoutStrip->setRowOn (rowGate, v > 0.5f); });
    limitRowAtt = std::make_unique<juce::ParameterAttachment> (
        *amp.apvts.getParameter (params::limiterOn),
        [this] (float v) { layoutStrip->setRowOn (rowLimit(), v > 0.5f); });
    gateRowAtt->sendInitialUpdate();
    limitRowAtt->sendInitialUpdate();

    // The end caps: the side columns, live with the real levels the rails read.
    inColShown  = prefs::getBool (prefs::showInCol, true);
    outColShown = prefs::getBool (prefs::showOutCol, true);
    layoutStrip->setCapOn (0, inColShown);
    layoutStrip->setCapOn (1, outColShown);
    layoutStrip->onCapToggle = [this] (int side, bool on) { applyColumnToggle (side, on); };

    // The rails' own scale (their floor is -80): the caps wear the same gradient, so they must
    // stand on the same ruler or the green lands in the wrong place.
    const auto levelOf = [] (const std::atomic<float>& db)
    {
        return juce::jlimit (0.0f, 1.0f, (db.load (std::memory_order_relaxed) + 80.0f) / 80.0f);
    };
    layoutStrip->capLevel[0] = [this, levelOf] { return levelOf (amp.gateKeyDb); };
    layoutStrip->capLevel[1] = [this, levelOf] { return levelOf (amp.outDb); };

    addAndMakeVisible (*layoutStrip);

    // The badges' clicks mean MENU — the whole device in one pick. There is nowhere to "open" any
    // more and nothing to open TO: every block wears its own face on the panel, and the gate's own
    // controls, position included, live in this menu.

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

    // The one place this plugin may reach the network without a click, and only because a player
    // ticked the switch in the version popover: once a day at most, on the message thread, never in
    // a constructor a scanner runs. Not ticked, or checked already today — nothing happens at all.
    amp.updateChecker().checkIfDue();
}

void AmpEditor::showGearMenu (juce::Point<int> screenPos)
{
    juce::PopupMenu m;
    m.addItem (1, "SETUP...");
    // Beside Setup, not inside it: the trademark notice is one click from anywhere, and Setup is
    // the pack manager rather than a place anybody goes to read.
    m.addItem (9, "ABOUT...");
    m.addSeparator();
    // A PARAMETER behind a menu item, deliberately: the comp changes the sound, so it belongs
    // to the session, not the machine — the tick just reads it, the click just writes it.
    m.addItem (8, "PACK LEVEL COMP",    true,
               amp.apvts.getParameter (params::packLevelComp)->getValue() > 0.5f);
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

                         if (r == 9)
                         {
                             safe->about.open();
                             return;
                         }

                         if (r == 5)
                         {
                             prefs::setSpectraShown (! prefs::spectraShown());
                             safe->repaint();
                             return;
                         }

                         if (r == 8)
                         {
                             if (auto* p = safe->amp.apvts.getParameter (params::packLevelComp))
                             {
                                 p->beginChangeGesture();
                                 p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
                                 p->endChangeGesture();
                             }
                             return;
                         }

                         bool& flag = r == 2 ? safe->showDemo : safe->showGlyphs;
                         flag = ! flag;
                         prefs::setBool (r == 2 ? prefs::showDemo : prefs::showGlyphs, flag);
                         safe->applyStripChoice();
                     });
}

void AmpEditor::applyColumnToggle (int side, bool on)
{
    prefs::setBool (side == 0 ? prefs::showInCol : prefs::showOutCol, on);
    (side == 0 ? inColShown : outColShown) = on;

    // The gate and the limiter keep working as set — a safety that dies with its meter is no
    // safety. Only the column's TRIM returns to unity: a hand nobody can see must not keep
    // pressing on the signal.
    if (! on)
        if (auto* p = amp.apvts.getParameter (side == 0 ? params::inTrim : params::outTrim))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (0.0f));
            p->endChangeGesture();
        }

    layoutStrip->setCapOn (side, on);
    resized();
    repaint();
}

void AmpEditor::applyTunerToggle (bool on)
{
    prefs::setBool (prefs::showTuner, on);
    tunerShown = on;

    applyStripChoice();
    resized();
    repaint();
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

void AmpEditor::showLimiterMenu (juce::Point<int> screenPos, bool withVolume)
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

    if (withVolume)
    {
        m.addSeparator();
        m.addSectionHeader ("VOLUME");
        m.addItem (7, "RESET");
    }

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

    // The side columns stand only when the strip's end caps say so; a hidden column hands its
    // width to the faceplate — all but the edge inset the badges keep, so the outermost block
    // never presses against the window's own edge.
    const int edgeInset = 12;
    const int colL = inColShown  ? GateStrip::designWidth + chromeGap : edgeInset;
    const int colR = outColShown ? OutStrip::designWidth  + chromeGap : edgeInset;

    gateStrip.setVisible (inColShown);
    outStrip.setVisible (outColShown);

    gateStrip.setBounds (margin, gutterY, GateStrip::designWidth, gutterH);
    gateStrip.setTransform (zoom);

    outStrip.setBounds (margin + FaceplateView::designWidth - OutStrip::designWidth, gutterY,
                        OutStrip::designWidth, gutterH);
    outStrip.setTransform (zoom);

    faceplate.setBounds (margin + colL, faceplateY,
                         FaceplateView::designWidth - colL - colR, faceplateH);
    faceplate.setTransform (zoom);

    // The always-on needle, full width, above the footer's facts — dropped a hair below its
    // gap's centre, which reads better than the arithmetic middle; the footer keeps its place,
    // so the window's height is untouched. The badges stand off the window's edge the way the
    // rails above them do, and the tuner takes whatever the badges leave it.
    const int tunerDrop  = 3;
    const int badgeInset = edgeInset;
    const int tunerY = faceplateY + faceplateH + chromeGap + tunerDrop;

    // The row is the tuner, whole: the guards' lights and menus live in the strip's arrows now.
    tunerStrip.setVisible (tunerShown);
    tunerStrip.setBounds (margin + badgeInset, tunerY,
                          FaceplateView::designWidth - 2 * badgeInset,
                          TunerStrip::designHeight);
    tunerStrip.setTransform (zoom);

    // The summoned rulers: the same vertical extent as their columns, standing toward the centre.
    inRuler.setBounds (margin + colL - chromeGap + 2, gutterY, 56, gutterH);
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

    // The row is as gone as the tuner: hidden, the footer moves up whole.
    const int footerY = tunerShown
                            ? tunerY - tunerDrop + TunerStrip::designHeight + chromeGap
                            : faceplateY + faceplateH + chromeGap;
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

    about.setBounds (0, 0, baseWidth, baseHeight());
    about.setTransform (zoom);
}

} // namespace orbitamp
