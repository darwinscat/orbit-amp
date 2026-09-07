// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "PluginEditor.h"

#include "ui/Prefs.h"

namespace orbitamp
{

namespace
{
    /** Which face on the panel a link owns, if any. `params::chainLinks` is deliberately free of
        anything visual, so this is the ONE place that knows about both — and it is a switch over
        NAMED rows, not a sum over positions, so a row standing down moves nothing. */
    std::optional<FaceplateView::Block> faceplateBlockFor (int row)
    {
        switch (row)
        {
            case params::rowBoost:  return FaceplateView::Block::boost;
            case params::rowPreamp: return FaceplateView::Block::preamp;
            case params::rowDelay:  return FaceplateView::Block::delay;
            case params::rowReverb: return FaceplateView::Block::reverb;
            case params::rowCab:    return FaceplateView::Block::cabinet;
            default:                return {};
        }
    }
}

AmpEditor::AmpEditor (AmpProcessor& p)
    : juce::AudioProcessorEditor (&p), amp (p), chrome (p), faceplate (p),
      inColumn  (VolumeColumn::Side::in,  p.gateKeyDb, p.inClip,
                 *p.apvts.getParameter (params::inTrim)),
      outColumn (VolumeColumn::Side::out, p.outDb,     p.outClip,
                 *p.apvts.getParameter (params::outTrim)),
      gateConsole (p.apvts, p.gateKeyDb),
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
    addAndMakeVisible (tunerStrip);
    addAndMakeVisible (footer);
    // The two TEMPORARY strips under the footer, off unless this player asked for them (the gear).
    // The demo needs its loops on disk — a machine without them has no player to show.
    showDemo   = params::demoLoopsPresent() && prefs::getBool (prefs::showDemo, false);
    showGlyphs = prefs::getBool (prefs::showGlyphs, false);

    // Before the first layout: whether an emptied row hands its height to whoever is left, or
    // takes it out of the window. See prefs::growBlocks.
    faceplate.setFillsHeight (prefs::getBool (prefs::growBlocks, true));
    dimRatherThanRemove = prefs::getBool (prefs::dimStandby, false);
    faceplate.setEqRowOnTop (prefs::getBool (prefs::eqRowOnTop, false));


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
    // the whole path as arrows, the two ends among them.

    {
        // Straight off the one list. The initial `on` is a placeholder for everyone the
        // parameters answer for: the attachments' first echo dresses the row and the panel the
        // moment the strip stands.
        std::vector<LayoutStrip::Row> rows;

        for (int i = 0; i < params::numChainRows; ++i)
        {
            const auto& link = params::chainLinks[(size_t) i];

            LayoutStrip::Row row { link.name,
                                   link.captured ? theme::orange : theme::violet,
                                   i == params::rowTuner ? tunerStands : true };

            // A link with no face on the panel has nothing for a click to reveal, so its click
            // opens its menu instead — the law is READ off the list rather than written out for
            // the two rows it happens to catch today.
            row.hasMenu = row.clickIsMenu = ! link.hasTile;

            if (i == params::rowGate)
            {
                // Silence is not work: with nothing at the key (the -90 floor) the gate has
                // nothing to press, and a light that burns all night means nothing by morning.
                row.depth = [this]
                {
                    if (amp.gateKeyDb.load() <= -89.5f)
                        return 0.0f;

                    return juce::jlimit (0.0f, 1.0f, -amp.gateMeterDb.load() / 40.0f);
                };
                row.dot = [this] { return amp.gateWorked.load(); };
            }
            else if (i == params::rowLimit)
            {
                row.depth = [this] { return juce::jlimit (0.0f, 1.0f, -amp.limiterGrDb.load() / 6.0f); };
                row.dot   = [this] { return amp.limiterWorked.load(); };
            }

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
    // loaded preset, a register switch, host automation — the arrow and the panel follow. ONE
    // loop for every link that has a switch, guards included: a guard turned off in its menu
    // reads dark in the strip the way a stood-down block does, and that used to be two more
    // attachments written out by hand.
    for (int i = 0; i < params::numChainRows; ++i)
    {
        const auto& link = params::chainLinks[(size_t) i];

        if (link.onParam != nullptr)
            blockRowAtts.push_back (std::make_unique<juce::ParameterAttachment> (
                *amp.apvts.getParameter (link.onParam),
                [this, i] (float v) { layoutStrip->setRowOn (i, v > 0.5f); applyRowStates(); }));

        if (link.presentParam != nullptr)
            blockRowAtts.push_back (std::make_unique<juce::ParameterAttachment> (
                *amp.apvts.getParameter (link.presentParam),
                [this, i] (float v) { layoutStrip->setRowPresent (i, v > 0.5f); applyRowStates(); }));
    }

    for (auto& att : blockRowAtts)
        att->sendInitialUpdate();

    // The guards' arrows ARE their consoles: any click opens the menu (OFF is its first item),
    // and the look clears the latched dot, the way a look at the badge used to.
    layoutStrip->onRowMenu = [this] (int i, juce::Point<int> pos)
    {
        if (i == params::rowGate)
        {
            amp.gateWorked.store (false);
            gateConsole.showMenu (pos);
        }
        else if (i == params::rowLimit)
        {
            amp.limiterWorked.store (false);
            showLimiterMenu (pos);
        }
    };

    layoutStrip->onChainMenu = [this] (juce::Component& anchor) { showChainMenu (anchor); };

    addAndMakeVisible (*layoutStrip);

    // The badges' clicks mean MENU — the whole device in one pick. There is nowhere to "open" any
    // more and nothing to open TO: every block wears its own face on the panel, and the gate's own
    // controls, position included, live in this menu.

    // The measurement, projected: the overlay reads the strip's own trace.
    learnOverlay.trace       = &gateConsole.learnTraceRef();
    learnOverlay.totalTicks  = GateConsole::learnTotalTicks;
    learnOverlay.pendingDb   = [this] { return gateConsole.learnPendingDb(); };
    gateConsole.onLearnBegin = [this] { learnOverlay.begin(); };
    gateConsole.onLearnDone  = [this] (const juce::String& v) { learnOverlay.finish (v); };

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

    addChildComponent (inRuler);    // went missing with the ceiling's ruler: a ladder with no
    addChildComponent (outRuler);   // parent cannot be shown, however often it is asked

    inColumn.onTrimDrag  = [this] (bool a) { inRuler.setVisible (a); if (a) inRuler.toFront (false); };
    outColumn.onTrimDrag = [this] (bool a) { outRuler.setVisible (a); if (a) outRuler.toFront (false); };

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

/** What the two switches of every row mean to the WINDOW, read in one place after any of them
    moves. A tile, a column and the tuner's row are all "stands on the panel or does not", and the
    answer is the same sentence for all three: in the rig, and either on or dimmed rather than
    removed. */
void AmpEditor::applyRowStates()
{
    const auto stands = [this] (params::ChainRow row)
    {
        const auto* in = amp.apvts.getParameter (params::chainLinks[(size_t) row].presentParam);
        const auto* on = amp.apvts.getParameter (params::chainLinks[(size_t) row].onParam);

        if (in != nullptr && in->getValue() <= 0.5f)
            return false;                               // out of the rig: gone, whatever the eye prefers

        return on == nullptr || on->getValue() > 0.5f || dimRatherThanRemove;
    };

    for (int i = 0; i < params::numChainRows; ++i)
        if (const auto block = faceplateBlockFor (i))
            faceplate.setShown (*block, stands ((params::ChainRow) i));

    inColStands  = stands (params::rowIn);
    outColStands = stands (params::rowOut);
    tunerStands  = stands (params::rowTuner);

    // Standing on the panel is not the same as WORKING. A link that kept its place while standing
    // by has to READ as standing by: dimmed, and its instrument stopped where it stood. The blocks
    // do this for themselves — every one of them binds its own switch — and these three are the
    // ones that are not blocks.
    const auto works = [this] (params::ChainRow row)
    {
        const auto* in = amp.apvts.getParameter (params::chainLinks[(size_t) row].presentParam);
        const auto* on = amp.apvts.getParameter (params::chainLinks[(size_t) row].onParam);

        return (in == nullptr || in->getValue() > 0.5f) && (on == nullptr || on->getValue() > 0.5f);
    };

    inColumn .setLive (works (params::rowIn));
    outColumn.setLive (works (params::rowOut));
    tunerStrip.setLive (works (params::rowTuner));

    // BOTH, and in this order. `applyStripChoice` only asks for a new SIZE, and a column leaving
    // does not change the window's height — so `setSize` was a no-op and the column stayed on
    // screen until something else resized the window. (Which is why switching the TUNER appeared
    // to switch off a column: the tuner DOES change the height, and the pending relayout landed
    // with it.)
    applyStripChoice();
    resized();
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

/** The checklist menu's two window switches. The links take 1..numChainRows — off their own
    indices, so the ids stay right whatever the chain grows into — and these sit well clear. */
static constexpr int itemGrowBlocks = 101, itemDimStandby = 102;

void AmpEditor::applyViewPrefs()
{
    faceplate.setFillsHeight (prefs::getBool (prefs::growBlocks, true));
    faceplate.setEqRowOnTop (prefs::getBool (prefs::eqRowOnTop, false));
    dimRatherThanRemove = prefs::getBool (prefs::dimStandby, false);
    showDemo   = params::demoLoopsPresent() && prefs::getBool (prefs::showDemo, false);
    showGlyphs = prefs::getBool (prefs::showGlyphs, false);
    applyRowStates();
    repaint();
}

void AmpEditor::showChainMenu (juce::Component& anchor)
{
    // The rig itself, straight off the one list and in chain order, so the menu reads as the strip
    // it edits. The tick is PRESENCE — in the rig or not at all — which is a different question
    // from standby: that one stays the arrow's own click, and the two must not look alike.
    juce::PopupMenu m;

    for (int i = 0; i < params::numChainRows; ++i)
    {
        const auto& link = params::chainLinks[(size_t) i];

        if (auto* p = amp.apvts.getParameter (link.presentParam))
            m.addItem (i + 1, link.name, true, p->getValue() > 0.5f);
    }

    m.addSeparator();

    // What an emptied row does — the eye's business, this machine only, so these two write the
    // preferences rather than any parameter. They live HERE and nowhere else: they only mean
    // anything while a link is being taken out, which is the list above.
    m.addItem (itemGrowBlocks, "KEEP WINDOW HEIGHT", true, prefs::getBool (prefs::growBlocks, true));
    m.addItem (itemDimStandby, "STANDBY KEEPS ITS PLACE", true, prefs::getBool (prefs::dimStandby, false));

    // ATTACHED to the button, not hung at a point: a menu that does not know its launcher
    // dismisses synchronously and lets the click through to it, so a second press on the checklist
    // would close the menu and reopen it in the same breath — JUCE spells this out at
    // juce_PopupMenu.cpp:724. With the target named, the second press just closes it.
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor),
                     // Guarded like the limiter's: the parameters outlive this window, the editor
                     // does not, and this callback reaches back through `this` twice.
                     [safe = juce::Component::SafePointer<AmpEditor> (this),
                      home = juce::Component::SafePointer<juce::Component> (&anchor)] (int r)
                     {
                         if (safe == nullptr || r == 0)
                             return;

                         if (r <= params::numChainRows)
                         {
                             const auto& link = params::chainLinks[(size_t) (r - 1)];

                             if (auto* p = safe->amp.apvts.getParameter (link.presentParam))
                             {
                                 // THE switch: a link's place in the rig is a parameter, so the
                                 // pick travels in the preset, the undo history and the registers
                                 // exactly as the Setup page's does.
                                 const bool on = p->getValue() > 0.5f;
                                 p->beginChangeGesture();
                                 p->setValueNotifyingHost (on ? 0.0f : 1.0f);
                                 p->endChangeGesture();
                             }
                         }
                         else if (r == itemGrowBlocks || r == itemDimStandby)
                         {
                             const auto& key = r == itemGrowBlocks ? prefs::growBlocks : prefs::dimStandby;
                             const bool fallback = r == itemGrowBlocks;

                             prefs::setBool (key, ! prefs::getBool (key, fallback));
                             safe->applyViewPrefs();
                         }

                         // Ten ticks in one list: a menu that shuts on every pick would have to be
                         // opened ten times to build a rig. It comes straight back, on the same
                         // button, so the whole visit is one visit. Through the message queue, not
                         // straight from here: JUCE does delete the old menu window before this
                         // callback runs, but that is its own dismissal order, and a reopen has no
                         // business depending on it.
                         juce::MessageManager::callAsync ([safe, home]
                         {
                             if (safe != nullptr && home != nullptr)
                                 safe->showChainMenu (*home);
                         });
                     });
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
    m.addItem (1, "OFF",           true, ! isOn);
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

                         // OFF is a toggle here too — a menu that can only kill is no menu.
                         if (r == 1)
                         {
                             set (on, on->getValue() > 0.5f ? 0.0f : 1.0f);
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
    // Never negative: with every block stood down the panel closes up, and a column asked for a
    // negative height is a component with no honest size.
    const int gutterH = juce::jmax (0, faceplateH - FaceplateView::contentTop
                                                  - FaceplateView::contentBottom);

    // The side columns stand only when the strip's end caps say so; a hidden column hands its
    // width to the faceplate — all but the edge inset the badges keep, so the outermost block
    // never presses against the window's own edge.
    const int edgeInset = 12;
    const int colL = inColStands  ? VolumeColumn::designWidth + chromeGap : edgeInset;
    const int colR = outColStands ? VolumeColumn::designWidth  + chromeGap : edgeInset;

    inColumn.setVisible (inColStands);
    outColumn.setVisible (outColStands);

    inColumn.setBounds (margin, gutterY, VolumeColumn::designWidth, gutterH);
    inColumn.setTransform (zoom);

    outColumn.setBounds (margin + FaceplateView::designWidth - VolumeColumn::designWidth, gutterY,
                        VolumeColumn::designWidth, gutterH);
    outColumn.setTransform (zoom);

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
    tunerStrip.setVisible (tunerStands);
    tunerStrip.setBounds (margin + badgeInset, tunerY,
                          FaceplateView::designWidth - 2 * badgeInset,
                          TunerStrip::designHeight);
    tunerStrip.setTransform (zoom);

    // The summoned rulers: the same vertical extent as their columns, standing toward the centre.
    inRuler.setBounds (margin + colL - chromeGap + 2, gutterY, 56, gutterH);
    inRuler.setTransform (zoom);
    outRuler.setBounds (margin + FaceplateView::designWidth - VolumeColumn::designWidth - 58, gutterY,
                        56, gutterH);
    outRuler.setTransform (zoom);

    // The learn sheet: half the plugin, centred over the faceplate.
    learnOverlay.setBounds (margin + FaceplateView::designWidth / 6, faceplateY + 60,
                            FaceplateView::designWidth * 2 / 3, 330);
    learnOverlay.setTransform (zoom);

    // The row is as gone as the tuner: hidden, the footer moves up whole.
    const int footerY = tunerStands
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
    devices.setBounds (0, 0, baseWidth, baseHeight());
    devices.setTransform (zoom);

}

} // namespace orbitamp
