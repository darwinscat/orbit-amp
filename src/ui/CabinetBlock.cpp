// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CabinetBlock.h"

#include "../PluginProcessor.h"
#include "../device/Cabinets.h"
#include "../device/IrLibrary.h"
#include "BassControls.h"
#include "CabWindow.h"
#include "Prefs.h"
#include "OnFirst.h"

#include <felitronics/appkit/TextPrompt.h>

namespace orbitamp
{

struct CabinetBlock::BassRow
{
    explicit BassRow (AmpProcessor& p) : di (*p.apvts.getParameter (params::cabMix), 0.0f, 100.0f) {}

    /** DI/CAB, A FADER as every BLEND and MIX on the bass's face (the owner, 01.11): its name over it, its percent under
        it, typed there, a double click its default, one undo step a drag. No switch of its own, as the AMP's BLEND: moved, it mixes
        and nothing else. */
    GuardRail di;

    void addTo (CabinetBlock& owner, AmpProcessor& p)
    {
        // The cuts are not here: the bass has ONE pair, on the AMP's switches (EqSection::setStandard). DI/CAB is the
        // cabinet's — with the cabinet out there is nothing to mix — and stands by with it.
        owner.addAndMakeVisible (di);
        bass::asBassFader (di, "MIX", [] (float v) { return juce::String (juce::roundToInt (v)) + "%"; });   // percent already
        bass::hint (di, "MIX: WET/DRY");
        di.onGesture = [&p] (bool begin) { if (begin) p.history.beginGesture(); else p.history.endGesture(); };
        di.onMenu    = [&owner] (juce::Point<int>) { if (owner.onBlockMenu != nullptr) owner.onBlockMenu(); };   // the CAB's menu
    }
};

CabinetBlock::CabinetBlock (AmpProcessor& processor)
    // CAB: what it plays is mostly its own cabinets, drawn, and an IR only when the player brings one — the bass's and,
    // since 08.11, the guitar's too.
    : BlockFrame ("Cab", BlockFrame::Kind::captured), amp (processor),
      cabs (device::Cabinets::of (processor.product().chain == Chain::bass ? device::CabKind::bass : device::CabKind::guitar))
{
    CabWindow::useOurLook();   // the miniature over the IR, in our theme
    // The panel draws this amp's builder's boxes — the sketch over the picture, the window (device/CabTables.h).
    device::drawCabinetsOf (processor.product().chain == Chain::bass ? device::CabKind::bass : device::CabKind::guitar);
    startTimerHz (30);
    attachPower (*amp.apvts.getParameter (params::cabOn));

    if (processor.product().chain == Chain::bass)
    {
        bassRow = std::make_unique<BassRow> (processor);
        bassRow->addTo (*this, processor);

    }

    // The IR's name on the border beside the block's, set like a name and in the block's colour.
    // The combo shows one entry — whatever plays — and opens a menu this block builds.
    ir.fontHeight = 16.0f;
    ir.tracking   = 0.15f;
    ir.boxed      = false;
    ir.tint       = theme::orange;
    addAndMakeVisible (ir);

    irAtt = std::make_unique<juce::ParameterAttachment> (
        *amp.apvts.getParameter (params::cabIr),
        [this] (float) { refreshIr (false, true); });

    // The row under the picture chooses — the twelve on its slider, CABINETS (the twelve by name, all of the builder's by
    // style and size, the player's own, EDIT… and SAVE AS…) — and the bass's MY IRS, the player's own, while there are any
    // (the owner, 06.11); the bass's name only says what plays. The guitar's (the owner, 10.11) is its one list on the
    // slider, the TOP 12 and the player's own IRs after them, and the same list on its name: no CABINETS, no window.
    {
        ir.opens = guitar();   // the guitar's name opens its IRs, as it always did: the block has no room for a door of them
        if (guitar())
            ir.onOpen = [this] { showIrMenu(); };
        factoryIrs.text = "CABINETS";
        factoryIrs.onOpen = [this] { showCabinetsMenu(); };
        slots.notches = (int) cabs.dozen().size();
        slots.notch = [this]
        {
            if (guitar())
            {
                const int at = guitarAt();
                return at < (int) cabs.dozen().size() ? at : -1;
            }
            const auto playing = AmpProcessor::cabSynthOf (amp.cabChoice());
            if (! playing.valid)
                return -1;
            const auto which = whichPlays (playing.state, cabs.own());
            return which.list == Pick::dozen ? which.at : -1;
        };
        slots.onMove  = [this] (int i) { amp.requestCabSynth (cabs.dozen()[(size_t) i].state, true); };
        slots.onLetGo = [this] (int i)
        {
            if (guitar())
                return pickGuitar (i);
            lastPick = { Pick::dozen, i, {} };
            amp.requestCabSynth (cabs.dozen()[(size_t) i].state);
        };
        addAndMakeVisible (slots);
        stepBack.by = -1;
        for (auto* s : { &stepBack, &stepOn })
        {
            s->onStep = [this] (int by) { if (guitar()) stepList (false, by); else stepCabinet (by); };
            addAndMakeVisible (*s);
        }

        // The guitar's own IRs on a slider of their own over the cuts, while there are any (lookAtLibrary). One heard where
        // the hand lets go: each read on the way would stay in the store.
        ownSlots.notch  = [this] { const int at = guitarAt() - (int) cabs.dozen().size(); return at >= 0 ? at : -1; };
        ownSlots.onLetGo = [this] (int i) { pickGuitar ((int) cabs.dozen().size() + i); };
        ownBack.by = -1;
        for (auto* s : { &ownBack, &ownOn })
            s->onStep = [this] (int by) { stepList (true, by); };
        for (juce::Component* c : { (juce::Component*) &ownSlots, (juce::Component*) &ownBack, (juce::Component*) &ownOn })
            addChildComponent (*c);
        myIrs.text = "MY IRS";
        myIrs.onOpen = [this] { showIrMenu (&myIrs); };
        if (guitar())
            addChildComponent (factoryIrs);
        else
            addAndMakeVisible (factoryIrs);
        addChildComponent (myIrs);
        lookAtLibrary();

        if (! guitar())
        {
            pictureClick.onClick = [this] { editCabinet(); };
            wave.addMouseListener (&pictureClick, false);
        }
    }

    // The picture: the IR, the cuts' curve and the trim's handle drawn on it, in the block's colour. The guitar's line under
    // it short and its cabinet the box alone at its right (the owner, 10.11: Sketch::boxOnly).
    wave.shortMetrics = guitar();
    sketch.boxOnly = guitar();
    wave.setAccent (theme::orange);
    wave.waveTint = theme::violet;   // the impulse itself in violet — the curve keeps the orange voice
    // The handle exists only in MANUAL — pushToWave keeps this current with the combo's mode.
    wave.setEqVisible (true);

    // The spectra behind the impulse, in the consoles' own hand: what walks into the IR as the
    // ground, what leaves it as the line — the same liquid columns, the same tilt, one renderer.
    wave.paintSpectrumUnder = [this] (juce::Graphics& g, juce::Rectangle<float> r)
    {
        if (! prefs::spectraShown (amp.product()))
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
    addAndMakeVisible (sketch);   // over the picture

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
    // The cabinets' doors stand in its row (the bass trims nothing; the guitar's trim is on the picture's right click).
    addChildComponent (trimCombo);

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

    refreshIr (true);
    pushToWave();
}

CabinetBlock::~CabinetBlock() = default;

void CabinetBlock::refreshIr (bool force, bool keepTrimWord)
{
    const auto choice = amp.cabChoice();

    // The NAME is part of what is shown: two files with the same bytes are one sound and two names.
    if (! force && choice.identity() + "|" + choice.name == shownIr)
        return;

    shownIr = choice.identity() + "|" + choice.name;

    // ...and a number the product does not play (Product::playsCabIr) is no cabinet: a bass amp with none of
    // its own plays no IR at all, and says so rather than a guitar cabinet's name. One of the bass's cabinets says
    // the mics heard too (CabWindow::labelOf).
    const auto synth = AmpProcessor::cabSynthOf (choice);
    const bool factoryPlays = ! choice.isUser() && amp.product().playsCabIr (choice.factory);
    juce::String shown = choice.isUser() ? choice.name : factoryPlays ? juce::String (params::cabIrNames[choice.factory]) : juce::String (noIr);
    if (synth.valid)   // the dozen's by its number, a player's own by its name, else its box and its mics — the one picked
    {
        const auto own = cabs.own();
        const auto named = nameOf (whichPlays (synth.state, own), own);
        shown = named.isNotEmpty() ? named : CabWindow::labelOf (synth);
    }
    slots.refresh();
    ownSlots.refresh();
    shownText = shown;
    ir.setEntries ({ { shown, 0, false } });
    ir.setSelection (0);

    // The bass's door whose IR plays, lit.
    factoryIrs.lit = synth.valid || factoryPlays;
    myIrs.lit = choice.isUser() && ! synth.valid;
    factoryIrs.repaint();
    myIrs.repaint();

    // The sketch of the bass's cabinet over the IR says which plays, and where its mics stand; none for any other IR. The
    // window's picture follows the state on its own (CabWindow).
    {
        const auto [cab, setup] = CabWindow::drawnOf (synth);
        sketch.cab = cab;
        sketch.setup = setup;
        sketch.repaint();
    }

    // The same bytes the engine convolves, whichever shelf they came off.
    if (choice.isUser())
        wave.setFromMemory (choice.bytes->getData(), choice.bytes->getSize());
    else
    {
        const auto& bytes = AmpProcessor::cabIrBytes (amp.product(), choice.factory);
        wave.setFromMemory (bytes.data, (size_t) bytes.size);
    }

    // A fixed window keeps its WORD across an IR swap: the parameter is a fraction of the shot's
    // length, so 50 ms of the old cab is not 50 ms of the new — re-assert the milliseconds.
    // A shot too short to hold the window shows all of itself and keeps the word anyway — see the
    // STILL OURS test in `deriveTrimMode`, which is what makes that survive the swap.
    //
    // ONLY WHEN A PICK ASKS. A recall — a register, an undo, a preset — brings its own trim with it,
    // and an IR of the player's own arrives without a parameter to call anyone: the timer finds it
    // after the recall has landed, and a write from there would carry the OLD shot's milliseconds
    // over the recalled fraction, and leave an undo step nobody made.
    if (keepTrimWord && trimMode == TrimMode::fixed && wave.lengthMs() > 0.0)
        trimAtt->setValueAsCompleteGesture (
            (float) juce::jlimit (0.001, 1.0, trimModeMs / wave.lengthMs()));

    pushToWave();
    resized();   // the name on the border is as wide as the name
}

namespace
{
    int irsBeneath (const device::IrLibrary::Node& n)
    {
        if (! n.folder)
            return 1;

        int total = 0;
        for (const auto& c : n.children)
            total += irsBeneath (c);
        return total;
    }
}

void CabinetBlock::lookAtLibrary()
{
    // The guitar's: the player's own IRs after the twelve on the slider, as the library stands — looked at now and then,
    // the player adding them in SETUP, renaming them, moving them about.
    if (guitar())
    {
        std::vector<juce::File> now;
        for (const auto& f : device::IrLibrary::listed (device::IrLibrary::directory (amp.product().irsFolder())))
            now.push_back (f);
        if (now != ownIrs)
        {
            ownIrs = std::move (now);
            ownSlots.notches = (int) ownIrs.size();
            ownSlots.refresh();
            ownSlots.repaint();
            const bool any = ! ownIrs.empty();
            if (any != ownSlots.isVisible())
            {
                for (juce::Component* c : { (juce::Component*) &ownSlots, (juce::Component*) &ownBack, (juce::Component*) &ownOn })
                    c->setVisible (any);
                resized();   // the row over the cuts comes, or goes and gives the picture its height
            }
        }
        return;
    }

    // MY IRS stands while the library holds any — looked at now and then, the player adding them in SETUP.
    const bool any = irsBeneath (device::IrLibrary::scan (device::IrLibrary::directory (amp.product().irsFolder()))) > 0;
    if (any != myIrs.isVisible())
    {
        myIrs.setVisible (any);
        resized();
    }
}

int CabinetBlock::Door::idealWidth() const
{
    return juce::roundToInt (theme::trackedWidth (text, theme::displayFont (13.0f), 0.06f)) + 18;
}

void CabinetBlock::Step::paint (juce::Graphics& g)
{
    // A small solid triangle pointing its way, dim, orange under the hand.
    const auto c = getLocalBounds().toFloat().getCentre();
    const float w = 5.0f, h = 4.5f;
    juce::Path t;
    const float d = (float) by * w * 0.5f;
    t.addTriangle (c.x + d, c.y, c.x - d, c.y - h, c.x - d, c.y + h);
    g.setColour (isMouseOver() ? theme::orange : theme::txDim);
    g.fillPath (t);
}

void CabinetBlock::Door::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (lit ? theme::orange : theme::txDim);
    theme::drawTracked (g, text, r.withTrimmedRight (14.0f), theme::displayFont (13.0f), 0.06f, juce::Justification::centredLeft);

    // The chevron that says "this opens".
    juce::Path v;
    const float cx = r.getRight() - 6.0f, cy = r.getCentreY() - 1.0f;
    v.startNewSubPath (cx - 3.0f, cy);
    v.lineTo (cx, cy + 3.0f);
    v.lineTo (cx + 3.0f, cy);
    g.strokePath (v, juce::PathStrokeType (1.2f));
}

juce::PopupMenu CabinetBlock::irMenu (const AmpProcessor& amp, std::vector<juce::File>& files)
{
    const auto choice  = amp.cabChoice();
    const auto library = device::IrLibrary::scan (device::IrLibrary::directory (amp.product().irsFolder()));
    const auto root    = library.file;

    const auto itemFor = [] (const juce::String& name, int id, bool ticked)
    {
        juce::PopupMenu::Item item (name);
        item.itemID   = id;
        item.colour   = theme::orange;
        item.isTicked = ticked;
        return item;
    };

    // THE SHELF, BY ITS FOLDERS — the ones the family lays it out in (device/FactoryShelf.h): a submenu
    // for each, in the order params::cabShelves first comes to it, and inside it each of its cabinets
    // in turn, a line that says what these IRs are and then their names. A shelf IR keeps its number in
    // the parameter whichever folder shows it, so its item is that number and one more, as it always
    // was. The folder holding the IR that plays wears the tick, so the way to it can be followed. Only the
    // cabinets the product plays: a bass amp's menu holds no guitar cabinet (Product::playsCabIr).
    juce::PopupMenu menu;

    // A GUITAR AMP'S: the TOP 12 in place of the shelf (the owner, 10.11), the one that plays ticked.
    const bool guitar = amp.product().chain != Chain::bass;
    if (guitar)
    {
        const auto& twelve = device::Cabinets::of (device::CabKind::guitar).dozen();
        const auto synth = AmpProcessor::cabSynthOf (choice);
        const int plays = synth.valid ? device::Cabinets::of (device::CabKind::guitar).dozenOf (synth.state) : -1;
        for (int i = 0; i < (int) twelve.size(); ++i)
            menu.addItem (itemFor (twelve[(size_t) i].name, firstOfTwelve + i, i == plays));
    }

    struct Folder
    {
        juce::String name;
        juce::PopupMenu items;
        bool holdsChoice = false;
    };
    std::vector<Folder> folders;

    for (const auto& shelf : params::cabShelves)
    {
        if (guitar || ! amp.product().playsCabIr (shelf.first))
            continue;

        const juce::String name (AmpProcessor::cabIrBytes (amp.product(), shelf.first).folder);
        auto at = std::find_if (folders.begin(), folders.end(), [&name] (const Folder& f) { return f.name == name; });
        if (at == folders.end())
            at = folders.insert (folders.end(), Folder { name, {}, false });

        at->items.addSectionHeader (juce::String::fromUTF8 (shelf.about));

        for (int i = shelf.first; i < shelf.first + shelf.count; ++i)
        {
            const bool ticked = ! choice.isUser() && choice.factory == i;
            at->items.addItem (itemFor (params::cabIrNames[i], 1 + i, ticked));
            at->holdsChoice = at->holdsChoice || ticked;
        }
    }

    for (auto& folder : folders)
        menu.addSubMenu (folder.name, folder.items, true, nullptr, folder.holdsChoice);

    // The player's files are numbered from firstFile, in the order the tree lists them.
    files.clear();

    if (irsBeneath (library) > 0)
    {
        // The shelf keeps its place however the library grows — the top — and the library is laid
        // out below it the way SETUP shows it: folders first, each a submenu, then the files. A
        // folder with nothing playable anywhere under it is a shelf with nothing on it, and stays
        // out. A folder holding what plays wears the tick too, so the way to it can be followed.
        if (menu.getNumItems() > 0)   // the shelf's folders, or the guitar's twelve
            menu.addSeparator();

        std::function<bool (juce::PopupMenu&, const device::IrLibrary::Node&)> addTree =
            [&] (juce::PopupMenu& into, const device::IrLibrary::Node& folder)
        {
            bool holdsChoice = false;

            for (const auto& n : folder.children)
            {
                if (n.folder)
                {
                    if (irsBeneath (n) == 0)
                        continue;

                    juce::PopupMenu sub;
                    const bool inside = addTree (sub, n);
                    into.addSubMenu (n.name(), sub, true, nullptr, inside);
                    holdsChoice = holdsChoice || inside;
                }
                else
                {
                    const auto from = n.file.getRelativePathFrom (root).replaceCharacter ('\\', '/');
                    const bool ticked = choice.isUser() && from == choice.from;

                    files.push_back (n.file);
                    into.addItem (itemFor (n.file.getFileNameWithoutExtension(),
                                           firstFile + (int) files.size() - 1, ticked));
                    holdsChoice = holdsChoice || ticked;
                }
            }

            return holdsChoice;
        };

        addTree (menu, library);
    }

    // Nothing to pick — a bass amp, with no cabinet of the bass's on the shelf yet and none of the player's:
    // a line that says so, where an empty menu would say nothing. SETUP's IR tab takes them in.
    if (menu.getNumItems() == 0)
        menu.addItem ("No IRs yet", false, false, [] {});

    return menu;
}

void CabinetBlock::showIrMenu (juce::Component* from)
{
    if (guitar())
        lookAtLibrary();   // the slider the same list as the menu, at once
    auto files = std::make_shared<std::vector<juce::File>>();
    auto menu = irMenu (amp, *files);

    onfirst::show (menu, this, juce::PopupMenu::Options().withTargetComponent (from != nullptr ? from : &ir),
                        [safe = juce::Component::SafePointer<CabinetBlock> (this), files] (int r)
                        {
                            if (safe == nullptr || r <= 0)
                                return;

                            if (r >= firstFile)
                            {
                                if ((size_t) (r - firstFile) < files->size())
                                    safe->amp.chooseCabFile ((*files)[(size_t) (r - firstFile)]);
                            }
                            else if (r >= firstOfTwelve)
                            {
                                return safe->pickGuitar (r - firstOfTwelve);
                            }
                            else
                            {
                                safe->amp.chooseCabFactory (r - 1);
                            }

                            safe->refreshIr (false, true);
                        });
}

int CabinetBlock::guitarAt() const
{
    const auto choice = amp.cabChoice();
    if (const auto playing = AmpProcessor::cabSynthOf (choice); playing.valid)
    {
        // One of the twelve, or a cabinet of none of them a session or a preset kept: off the list.
        const auto which = whichPlays (playing.state, {});
        return which.list == Pick::dozen ? which.at : -1;
    }
    if (choice.isUser())
    {
        const auto root = device::IrLibrary::directory (amp.product().irsFolder());
        for (size_t i = 0; i < ownIrs.size(); ++i)
            if (ownIrs[i].getRelativePathFrom (root).replaceCharacter ('\\', '/') == choice.from)
                return (int) (cabs.dozen().size() + i);
    }
    return -1;
}

void CabinetBlock::pickGuitar (int at)
{
    const auto& twelve = cabs.dozen();
    if (at >= 0 && at < (int) twelve.size())
    {
        lastPick = { Pick::dozen, at, {} };
        amp.requestCabSynth (twelve[(size_t) at].state);
        refreshIr (true);   // one the same as the last changes no sound, and the name must follow all the same
    }
    else if (const auto i = (size_t) (at - (int) twelve.size()); at >= 0 && i < ownIrs.size())
    {
        amp.chooseCabFile (ownIrs[i]);
        refreshIr (false, true);
    }
}

bool CabinetBlock::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (! guitar())
        return false;
    for (const auto& f : files)
        if (device::IrLibrary::looksLikeImport (juce::File (f)))
            return true;
    return false;
}

void CabinetBlock::fileDragEnter (const juce::StringArray&, int, int) { dropOver = true;  repaint(); }
void CabinetBlock::fileDragExit  (const juce::StringArray&)           { dropOver = false; repaint(); }

void CabinetBlock::filesDropped (const juce::StringArray& files, int, int)
{
    dropOver = false;
    repaint();

    // Into the library's top, as the IR editor's + puts them; the first of them that landed, in the editor's order, plays.
    const auto root = device::IrLibrary::directory (amp.product().irsFolder());
    const auto before = device::IrLibrary::listed (root);
    int landed = 0;
    for (const auto& f : files)
        landed += device::IrLibrary::importPath (juce::File (f), root);
    if (landed == 0)
        return;

    lookAtLibrary();
    for (size_t i = 0; i < ownIrs.size(); ++i)
        if (! before.contains (ownIrs[i]))
            return pickGuitar ((int) (cabs.dozen().size() + i));
}

void CabinetBlock::paintOverChildren (juce::Graphics& g)
{
    // IRs held over the block: an orange line round it, where they will land.
    if (! dropOver)
        return;
    g.setColour (theme::orange);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.5f), 6.0f, 2.0f);
}

void CabinetBlock::stepList (bool own, int by)
{
    // Round the one list, the twelve's or the player's own: from where it plays, or into it from its first (its last, back).
    lookAtLibrary();
    const int twelve = (int) cabs.dozen().size();
    const int count = own ? (int) ownIrs.size() : twelve;
    if (count == 0)
        return;
    const int at = guitarAt();
    const int here = own ? (at >= twelve ? at - twelve : -1) : (at >= 0 && at < twelve ? at : -1);
    const int next = here < 0 ? (by > 0 ? 0 : count - 1) : ((here + by) % count + count) % count;
    pickGuitar ((own ? twelve : 0) + next);
}

void CabinetBlock::stepCabinet (int by)
{
    if (guitar())   // the slider that plays: the player's own while one of them does, else the twelve
    {
        stepList (guitarAt() >= (int) cabs.dozen().size(), by);
        return;
    }

    const auto choice  = amp.cabChoice();
    const auto playing = AmpProcessor::cabSynthOf (choice);
    const auto next    = [by] (int at, size_t count) { const int n = (int) count; return (size_t) (((at + by) % n + n) % n); };

    if (playing.valid)   // one of the builder's: the twelve, all of them, or the player's own — the list of the one that plays
    {
        const auto own = cabs.own();
        const auto which = whichPlays (playing.state, own);
        if (which.list == Pick::dozen)
        {
            const auto i = next (which.at, cabs.dozen().size());
            lastPick = { Pick::dozen, (int) i, {} };
            amp.requestCabSynth (cabs.dozen()[i].state);
        }
        else if (which.list == Pick::all)
        {
            const auto i = next (which.at, cabs.all().size());
            lastPick = { Pick::all, (int) i, {} };
            amp.requestCabSynth (cabs.all()[i].state);
        }
        else if (which.list == Pick::own)
        {
            const auto i = next (which.at, own.size());
            lastPick = { Pick::own, (int) i, own[i].file };
            amp.requestCabSynth (own[i].state);
        }
        refreshIr (true);   // a step to one the same as the last changes no sound, and the name must follow all the same
        return;
    }

    if (choice.isUser())   // a file of the player's own: its folder's IRs, in the order the menu lists them
    {
        const auto library = device::IrLibrary::scan (device::IrLibrary::directory (amp.product().irsFolder()));
        const auto file    = library.file.getChildFile (choice.from);
        std::vector<juce::File> siblings;
        const std::function<void (const device::IrLibrary::Node&)> look = [&] (const device::IrLibrary::Node& n)
        {
            if (n.file == file.getParentDirectory())
                for (const auto& c : n.children)
                    if (! c.folder)
                        siblings.push_back (c.file);
            for (const auto& c : n.children)
                if (c.folder)
                    look (c);
        };
        look (library);
        if (const auto at = std::find (siblings.begin(), siblings.end(), file); at != siblings.end())
            amp.chooseCabFile (siblings[next ((int) (at - siblings.begin()), siblings.size())]);
    }
    else   // the factory's: the ones this product plays, by their numbers
    {
        std::vector<int> plays;
        for (int i = 0; i < params::cabIrNames.size(); ++i)
            if (amp.product().playsCabIr (i))
                plays.push_back (i);
        if (const auto at = std::find (plays.begin(), plays.end(), choice.factory); at != plays.end())
            amp.chooseCabFactory (plays[next ((int) (at - plays.begin()), plays.size())]);
    }
    refreshIr (false, true);
}

CabinetBlock::Pick CabinetBlock::whichPlays (const cabir::State& state, const std::vector<device::Cabinets::Cabinet>& own) const
{
    const auto& twelve = cabs.dozen();
    const auto& whole = cabs.all();
    switch (lastPick.list)   // the one picked, while it still sounds as it did
    {
        case Pick::dozen:
            if (lastPick.at >= 0 && lastPick.at < (int) twelve.size() && twelve[(size_t) lastPick.at].state == state)
                return lastPick;
            break;
        case Pick::all:
            if (lastPick.at >= 0 && lastPick.at < (int) whole.size() && whole[(size_t) lastPick.at].state == state)
                return lastPick;
            break;
        case Pick::own:   // by its file: its place in the list moves as the player's own are added and renamed
            for (size_t i = 0; i < own.size(); ++i)
                if (own[i].file == lastPick.file && own[i].state == state)
                    return { Pick::own, (int) i, own[i].file };
            break;
        case Pick::none:
            break;
    }
    if (const int i = cabs.dozenOf (state); i >= 0)
        return { Pick::dozen, i, {} };
    if (const int i = cabs.allOf (state); i >= 0)
        return { Pick::all, i, {} };
    for (size_t i = 0; i < own.size(); ++i)
        if (own[i].state == state)
            return { Pick::own, (int) i, own[i].file };
    return {};
}

juce::String CabinetBlock::nameOf (const Pick& p, const std::vector<device::Cabinets::Cabinet>& own) const
{
    switch (p.list)
    {
        case Pick::dozen: return cabs.dozen()[(size_t) p.at].name;
        case Pick::all:   return cabs.all()[(size_t) p.at].name;
        case Pick::own:   return own[(size_t) p.at].name;
        case Pick::none:  break;
    }
    return {};
}

void CabinetBlock::editCabinet()
{
    // The player's own that plays opens in the window; the dozen's, and one of no name, are made the player's own first.
    const auto playing = AmpProcessor::cabSynthOf (amp.cabChoice());
    if (! playing.valid)
        return;
    if (const auto which = whichPlays (playing.state, cabs.own()); which.list == Pick::own)
        editIn (which.file);
    else
        newCabinet();
}

void CabinetBlock::newCabinet()
{
    // A copy of what plays, named at once and edited (the owner, 10.11: no question before the window, RENAME names it
    // otherwise): the one it copies without its number in the twelve or its style, "2x10 GUTTER COPY 01", the first
    // number free; one of no name MY CABINET and the first number free.
    const auto playing = AmpProcessor::cabSynthOf (amp.cabChoice());
    if (! playing.valid)
        return;
    const auto own = cabs.own();
    const auto taken = [&own] (const juce::String& name)
    {
        return std::any_of (own.begin(), own.end(), [&name] (const device::Cabinets::Cabinet& c) { return c.name == name; });
    };
    auto from = nameOf (whichPlays (playing.state, own), own);   // "6: 2x10 GUTTER", "ROCK · 8x10 CLASSIC", "TOP 12: 4x12 SELFISH GENE"
    if (const auto dot = juce::String::fromUTF8 (" \xc2\xb7 "); from.contains (dot))
        from = from.fromFirstOccurrenceOf (dot, false, false);
    else if (from.contains (": "))
        from = from.fromFirstOccurrenceOf (": ", false, false);
    juce::String name;
    for (int n = from.isEmpty() ? (int) own.size() + 1 : 1; name.isEmpty(); ++n)
    {
        name = from.isEmpty() ? "MY CABINET " + juce::String (n) : from + " COPY " + juce::String (n).paddedLeft ('0', 2);
        if (taken (name))
            name = {};
    }
    const auto file = cabs.save (name, playing.state);
    if (file == juce::File())
        return;
    lastPick = { Pick::own, -1, file };
    refreshIr (true);
    editIn (file);
}

void CabinetBlock::editIn (const juce::File& own)
{
    // Every change the window keeps is written into the player's own file at once — no SAVE AS… after it.
    CabWindow::open (amp, *this, [safe = juce::Component::SafePointer<CabinetBlock> (this), own, &list = cabs] (const juce::String& from)
    {
        if (const auto synth = AmpProcessor::cabSynthOf (from); synth.valid && list.keep (own, synth.state)
                                                                && safe != nullptr)
            safe->refreshIr (true);
    });
}

void CabinetBlock::showCabinetsMenu()
{
    // The dozen by name, ALL of them under it by style and by size, the player's own under those, then what makes and
    // keeps one: EDIT… opens the player's own that plays in the cabinets' window, NEW FROM… copies a factory one first
    // (they are fixed), SAVE AS… keeps what plays as the player's own, by a name; RENAME and DELETE for those kept.
    const auto playing = AmpProcessor::cabSynthOf (amp.cabChoice());
    const auto& dozen = cabs.dozen();
    const auto& every = cabs.all();
    const auto own = cabs.own();
    // One ticked, the one that plays (whichPlays): never every one the same as it.
    const auto which = playing.valid ? whichPlays (playing.state, own) : Pick {};
    const auto plays = [&which] (Pick::List list, int i) { return which.list == list && which.at == i; };
    enum { pickDozen = 1, pickOwn = 100, edit = 300, saveAs = 301, renameOwn = 400, deleteOwn = 600, pickAll = 1000 };
    const auto named = nameOf (which, own);
    const auto editWord = ! playing.valid || which.list == Pick::own ? juce::String ("EDIT...")
                        : named.isNotEmpty()                         ? "NEW FROM " + named + "..."
                                                                     : juce::String ("NEW FROM THIS...");

    juce::PopupMenu menu;
    for (int i = 0; i < (int) dozen.size(); ++i)
        menu.addItem (pickDozen + i, dozen[(size_t) i].name, true, plays (Pick::dozen, i));

    // ALL (the owner, 06.11): the IR packs' 190 — by style, each under its name without the style's word; by size, the
    // sizes in the packs page's order, each named with its style.
    if (! every.empty())
    {
        const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");
        // Without its style's word: the bass's "ROCK · 8x10 STACK", the guitar's "METAL: 4x12 ANVIL".
        const auto shortName = [&dot] (const device::Cabinets::Cabinet& c)
        {
            for (const auto& lead : { c.style.toUpperCase() + dot, c.style.toUpperCase() + ": " })
                if (c.name.startsWithIgnoreCase (lead))
                    return c.name.substring (lead.length());
            return c.name;
        };
        std::vector<std::pair<juce::String, juce::PopupMenu>> styles, sizes;
        const auto sizeWords = guitar() ? juce::StringArray { "4x12", "2x12", "1x12", "4x10", "2x10", "1x10" }
                                        : juce::StringArray { "1x15", "1x12", "2x10", "4x10", "6x10", "8x10", "2x12", "2x15", "4x6", "8x8" };
        for (const auto& word : sizeWords)
            sizes.push_back ({ word, {} });
        const auto into = [] (std::vector<std::pair<juce::String, juce::PopupMenu>>& list, const juce::String& key) -> juce::PopupMenu&
        {
            for (auto& [k, m] : list)
                if (k == key)
                    return m;
            list.push_back ({ key, {} });
            return list.back().second;
        };
        for (int i = 0; i < (int) every.size(); ++i)
        {
            const auto& c = every[(size_t) i];
            const bool ticked = plays (Pick::all, i);
            into (styles, c.style.toUpperCase()).addItem (pickAll + i, shortName (c), true, ticked);
            into (sizes, CabPicker::wordOf (CabPicker::setupOf (c.state).first)).addItem (pickAll + i, c.style.toUpperCase() + dot + shortName (c), true, ticked);
        }
        juce::PopupMenu byStyle, bySize, all;
        for (auto& [word, m] : styles)
            byStyle.addSubMenu (word, m);
        for (auto& [word, m] : sizes)
            if (m.getNumItems() > 0)
                bySize.addSubMenu (word, m);
        all.addSubMenu ("BY STYLE", byStyle);
        all.addSubMenu ("BY SIZE", bySize);
        menu.addSubMenu ("ALL", all);
    }
    menu.addSeparator();
    menu.addSectionHeader ("MY CABINETS");
    if (own.empty())
        menu.addItem (-1, "none yet: SAVE AS... keeps the one that plays", false, false);
    for (int i = 0; i < (int) own.size(); ++i)
        menu.addItem (pickOwn + i, own[(size_t) i].name, true, plays (Pick::own, i));
    menu.addSeparator();
    menu.addItem (edit, editWord, playing.valid, false);
    menu.addItem (saveAs, "SAVE AS...", playing.valid, false);
    if (! own.empty())
    {
        juce::PopupMenu renames, deletes;
        for (int i = 0; i < (int) own.size(); ++i)
        {
            renames.addItem (renameOwn + i, own[(size_t) i].name);
            deletes.addItem (deleteOwn + i, own[(size_t) i].name);
        }
        menu.addSubMenu ("RENAME", renames);
        menu.addSubMenu ("DELETE", deletes);
    }

    onfirst::show (menu, this, juce::PopupMenu::Options().withTargetComponent (&factoryIrs),
                   [safe = juce::Component::SafePointer<CabinetBlock> (this), own] (int r)
                   {
                       if (safe == nullptr || r <= 0)
                           return;
                       auto& self = *safe;
                       const auto& twelve = self.cabs.dozen();
                       const auto& whole = self.cabs.all();
                       // A pick is remembered as which one, so a copy the same as a factory one plays as itself.
                       const auto pick = [&self] (Pick p, const cabir::State& state)
                       {
                           self.lastPick = p;
                           self.amp.requestCabSynth (state);
                           self.refreshIr (true);
                       };
                       if (r >= pickDozen && r < pickDozen + (int) twelve.size())
                           pick ({ Pick::dozen, r - pickDozen, {} }, twelve[(size_t) (r - pickDozen)].state);
                       else if (r >= pickAll && r < pickAll + (int) whole.size())
                           pick ({ Pick::all, r - pickAll, {} }, whole[(size_t) (r - pickAll)].state);
                       else if (r >= pickOwn && r < pickOwn + (int) own.size())
                           pick ({ Pick::own, r - pickOwn, own[(size_t) (r - pickOwn)].file }, own[(size_t) (r - pickOwn)].state);
                       else if (r == edit)
                           self.editCabinet();
                       else if (r == saveAs)
                           self.saveCabinetAs();
                       else if (r >= renameOwn && r < renameOwn + (int) own.size())
                           self.renameCabinet (own[(size_t) (r - renameOwn)].file, own[(size_t) (r - renameOwn)].name);
                       else if (r >= deleteOwn && r < deleteOwn + (int) own.size())
                       {
                           self.cabs.remove (own[(size_t) (r - deleteOwn)].file);
                           self.refreshIr (true);
                       }
                   });
}

void CabinetBlock::saveCabinetAs()
{
    const auto playing = AmpProcessor::cabSynthOf (amp.cabChoice());
    if (! playing.valid)
        return;
    const auto named = cabs.nameOf (playing.state);
    const auto initial = named.isNotEmpty() && cabs.dozenOf (playing.state) < 0
                             ? named : "MY CABINET " + juce::String ((int) cabs.own().size() + 1);
    felitronics::appkit::textPrompt ("Save the cabinet as", initial,
                                     [safe = juce::Component::SafePointer<CabinetBlock> (this), state = playing.state, &list = cabs] (juce::String name)
                                     {
                                         if (const auto file = list.save (name, state); file != juce::File() && safe != nullptr)
                                         {
                                             safe->lastPick = { Pick::own, -1, file };   // what plays is now that one
                                             safe->refreshIr (true);
                                         }
                                     });
}

void CabinetBlock::renameCabinet (const juce::File& file, const juce::String& name)
{
    felitronics::appkit::textPrompt ("Rename the cabinet", name,
                                     [safe = juce::Component::SafePointer<CabinetBlock> (this), file, &list = cabs] (juce::String renamed)
                                     {
                                         if (! list.rename (file, renamed) || safe == nullptr)
                                             return;
                                         // Renamed is a new file: the one picked follows it, by the name it took.
                                         if (safe->lastPick.list == Pick::own && safe->lastPick.file == file)
                                             for (const auto& c : list.own())
                                                 if (c.name == device::Cabinets::cleaned (renamed))
                                                     safe->lastPick.file = c.file;
                                         safe->refreshIr (true);
                                     });
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

    // ...and the same when the shot on show is not yet the one the state names: a recall moves the
    // trim first and the IR of the player's own a tick later, and milliseconds read off the
    // outgoing picture would latch a window nobody chose.
    if (shownIr.upToFirstOccurrenceOf ("|", false, false) != amp.cabChoice().identity())
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

    onfirst::show (m, this, juce::PopupMenu::Options().withMousePosition(),
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

    // THE BASS'S DI/CAB at the block's right edge, the content's whole height, as COMP's BLEND stands in its block.
    if (bassRow != nullptr)
    {
        bassRow->di.setBounds (area.removeFromRight (bassRow->di.columnWidth));
        area.removeFromRight (gap);
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
    const auto wholeRow = row;   // the bass's doors take it edge to edge: its cuts' seats stand empty
    area.removeFromBottom (gap);

    // A slider with a step at each end of it (the owner, 10.11).
    const auto placeSteps = [] (Step& back, CabinetSlots& slider, Step& on, juce::Rectangle<int> r)
    {
        back.setBounds (r.removeFromLeft (stepW));
        on.setBounds (r.removeFromRight (stepW));
        slider.setBounds (r);
    };
    const auto placeSlider = [this, placeSteps] (juce::Rectangle<int> r) { placeSteps (stepBack, slots, stepOn, r); };

    // The guitar's own IRs on a row of their own over the cuts, the block's whole width, while there are any (the owner,
    // 10.11): with none, the picture has the row.
    if (guitar() && ownSlots.isVisible())
    {
        placeSteps (ownBack, ownSlots, ownOn, area.removeFromBottom (switchRow).reduced (gap, 0));
        area.removeFromBottom (gap);
    }

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

    // The bass: no cut words (its one pair is the AMP's).
    if (bassRow != nullptr)
        for (int i = 0; i < 2; ++i)
        {
            switches[(size_t) i].sw.setVisible (false);
            switches[(size_t) i].label.setVisible (false);
        }

    trimCombo.setBounds (row.withSizeKeepingCentre (juce::jmin (90, row.getWidth() - 12),
                                                    row.getHeight()));

    // The row where the trim stood: the IRs' door at the right (the bass's MY IRS while there are any), CABINETS before
    // it, and the bass's dozen's slider across what is left, edge to edge. The guitar's the TOP 12's slider between its
    // cuts (the owner, 10.11).
    if (guitar())
        placeSlider (row.reduced (gap, 0));
    else
    {
        auto r = wholeRow;
        if (myIrs.isVisible())
        {
            myIrs.setBounds (r.removeFromRight (juce::jmin (r.getWidth(), myIrs.idealWidth())));
            r.removeFromRight (gap);
        }
        factoryIrs.setBounds (r.removeFromRight (juce::jmin (r.getWidth(), factoryIrs.idealWidth())));
        r.removeFromRight (gap);
        placeSlider (r);
    }

    wave.setBounds (area);

    // The cabinet's sketch over the whole picture, drawn at its right (CabPicker::paintSketch) — over the time and dB
    // marks there and the IR's line, the tolex letting them through.
    sketch.setBounds (area);
}

void CabinetBlock::paintContent (juce::Graphics&) {}

GuardRail* CabinetBlock::diRail() noexcept { return bassRow != nullptr ? &bassRow->di : nullptr; }

/** The consoles' feeding rule, twice: a fresh window into its pane, a starve when the audio
    stopped — then one repaint, and the hook above draws both. */
void CabinetBlock::timerCallback()
{
    refreshIr();   // before the early return: what plays is shown whether or not the block is on

    if (isShowing() && --libraryLook <= 0)
    {
        libraryLook = 90;   // three seconds
        lookAtLibrary();
    }

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
