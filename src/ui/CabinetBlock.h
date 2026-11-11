// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../Parameters.h"
#include "../device/Cabinets.h"
#include "BlockFrame.h"
#include "CabWindow.h"
#include "CabinetSlots.h"
#include "GuardRail.h"
#include "VoicingSelector.h"
#include "ZoneSwitch.h"

#include <felitronics/analysis/PlotMap.h>
#include <felitronics/analysis/SpectrumPane.h>
#include <felitronics/appkit/IrWaveView.h>

#include <array>
#include <memory>
#include <vector>

namespace orbitamp
{

class AmpProcessor;

/** The cabinet, filling the rest of the lower row: ONE impulse response, drawn.

    Half of OrbitCab: one IR, no mix. Its name stands on the border beside the block's; the box is
    the IR itself — the impulse with its tail, the two cuts' curve over it with a handle per cut,
    the trim's handle — and under it the four switches: HPF, LPF, TRIM, Ø. What the switches do is
    baked into the IR off the pump; the picture is the family's IrWaveView, fed the same bytes the
    engine convolves. */
class CabinetBlock final : public BlockFrame,
                           public juce::FileDragAndDropTarget,
                           private juce::Timer
{
public:
    explicit CabinetBlock (AmpProcessor&);
    ~CabinetBlock() override;

    /** The editor's LEFT and RIGHT (the owner, 08.11: to hear cabinets one after another): the one before or after
        the cabinet that plays, round the list it is in — the slider that plays (the owner, 10.11), the twelve or the
        player's own IRs as the IR editor lists them (stepList). */
    void stepCabinet (int by);

    /** The CAB takes IRs dropped on it (the owner, 10.11; OrbitBass's too, 11.11): a file, a folder of them or a zip,
        into the player's own `IRs` (the bass's `Bass IRs`), as the IR editor's + does — on the slider and in the editor
        after the twelve at once, the first of them playing. */
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void paintOverChildren (juce::Graphics&) override;

private:
    void timerCallback() override;
    void layOutContent (juce::Rectangle<int>) override;
    void paintContent (juce::Graphics&) override;

    /** Shows what the cabinet plays — the name on the border, the picture — when that has changed
        since the last look, or always when `force`. Asked by the IR parameter's echo and by the
        timer: an IR of the player's own is a state property, and a preset, an undo or a register
        switch moves it without any parameter to call anyone.

        Writes nothing unless `keepTrimWord` — a pick's own request to keep a fixed trim window's
        milliseconds across the swap. The timer never asks: what it finds is a recall. */
    void refreshIr (bool force = false, bool keepTrimWord = false);

    /** The IR list (irMenu), opened from the block's name. */
    void showIrMenu();

    juce::String shownIr;   // CabChoice::identity + "|" + name of what the face last showed
    juce::String shownText; // ...and the name it showed for it (shownName)

    /** The picture wears what the parameters say — the cuts, the trim — so it draws the sound. */
    void pushToWave();

    static constexpr int switchRow = 20;
    static constexpr int gap       = 8;

    AmpProcessor& amp;

    /** Its instrument's cabinets (device/Cabinets.h): OrbitBass's the bass's, OrbitAmp's the guitar's (the owner, 08.11). */
    const device::Cabinets& cabs;
    bool guitar() const noexcept { return bassRow == nullptr; }

    /** OrbitBass's own (ui/BassControls.h): DI/CAB's rail at the block's right edge — the cabinet's own HPF and LPF
        left off, the bass's cuts being a stage after DI/CAB. On the bass's chain alone. */
    struct BassRow;
    std::unique_ptr<BassRow> bassRow;

    /** The twelve on a slider (CabinetSlots), in the row where the guitar's trim stood. The name on the border says what
        plays: the twelve's by its number, "7: 1x15 THUMP", a player's own IR by its name. */
    CabinetSlots slots;

    /** The slider's two steps (the owner, 10.11): a small triangle at each end of it, the cabinet before and the one
        after, as LEFT and RIGHT step it (stepCabinet). */
    struct Step final : public juce::Component
    {
        int by = 1;
        std::function<void (int)> onStep;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override { if (onStep) onStep (by); }
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }
    };
    Step stepBack, stepOn;
    static constexpr int stepW = 16;

    /** Two sliders (the owner, 10.11; OrbitBass's as OrbitAmp's, 11.11): the twelve on `slots` — the guitar's between its
        cuts — the player's own IRs on `ownSlots` over them while there are any. The one whose list plays shows its cap; a hand on the other goes there. LEFT and
        RIGHT step the one that plays, each slider's steps its own (stepList). */
    CabinetSlots ownSlots;
    Step ownBack, ownOn;
    void stepList (bool own, int by);

    /** WHICH OF THE BUILDER'S PLAYS (the owner, 10.11: two the same were both ticked): the one the hand last picked here
        while it still sounds as picked; else the first the same, the twelve's, all's, then the player's own cabinets
        (savedCabinets) — the menu's BUILDER picks any of them, and a session or a preset may keep any. Its name on the
        border and its notch on the slider follow it. */
    struct Pick
    {
        enum List { none, dozen, all, own } list = none;
        int at = -1;       // its slot in the twelve, in all, or among the player's own as own() lists them
        juce::File file;   // a player's own: its file
    };
    Pick lastPick;
    void pickCabinet (const Pick&, const cabir::State&);
    /** The player's own cabinets, by file: the ones kept in `Guitar Cabinets` (`Bass Cabinets`), then the builder app's
        MY PRESETS (device::Cabinets::builderSaves). */
    std::vector<device::Cabinets::Cabinet> savedCabinets() const;
    Pick whichPlays (const cabir::State&, const std::vector<device::Cabinets::Cabinet>& own) const;
    juce::String nameOf (const Pick&, const std::vector<device::Cabinets::Cabinet>& own) const;

    int libraryLook = 0;   // timer ticks to the next look at whether the library holds any IRs
    void lookAtLibrary();

    /** THE LIST (the owner, 10.11: "в CAB остаются эти 12 и свои IR"; the bass's as the guitar's, 11.11): the twelve, then
        every IR of the player's own in the IR editor's order — the twelve on one slider and the player's on another
        (ownSlots), and both in the menu on the name. No CABINETS, no window, none of the builder's others and none of the
        shelf: the builder app edits cabinets, and one made there comes here as an IR. `ownIrs` is the library as last
        looked at (lookAtLibrary). */
    std::vector<juce::File> ownIrs;
    int  listAt() const;        // the place of the one that plays in that list, -1 for none of it
    void pickAt (int at);
    bool dropOver = false;      // IRs held over the block

    VoicingSelector ir;
    felitronics::appkit::IrWaveView wave;

    /** The cabinet that plays, over the picture's right — where a cabinet's IR is only its tail. Clicks go through it to
        the picture; none while an IR of the player's own plays. */
    struct Sketch final : public juce::Component
    {
        int cab = -1;
        CabPicker::Setup setup;
        /** The box alone at the picture's right, as tall as the picture, its mics twice the size, a little see-through (the
            owner, 10.11; OrbitBass's too, 11.11) — its line under the picture short ("100ms 48/24"), at the left, leaving
            the right free to the floor. Else the box and its mics small (CabPicker::paintSketch). */
        bool boxOnly = false;
        Sketch() { setInterceptsMouseClicks (false, false); setBufferedToImage (true); }
        void paint (juce::Graphics& g) override
        {
            // Under the picture's time marks along its top; OrbitBass's over the IR's line along its bottom (IrWaveView's own).
            const auto area = getLocalBounds().toFloat().withTrimmedTop (14.0f);
            if (boxOnly)
                CabPicker::paintBoxSketch (g, cab, setup, area.withTrimmedBottom (8.0f));
            else
                CabPicker::paintSketch (g, cab, setup, area.withTrimmedBottom (24.0f));
        }
    };
    Sketch sketch;

public:
    juce::Component& waveView() noexcept { return wave; }

    /** OrbitBass: a right click on the waveform opens the block's menu (setBlockMenu) INSTEAD of the trim menu — one door,
        not two at once (a listener beside the picture's own handler opened both). The guitar's keeps its trim menu. */
    void pictureMenuIsBlockMenu() { wave.onMenuRequested = [this] { if (onBlockMenu != nullptr) onBlockMenu(); }; }

    /** The bass's cabinet's width that leaves `waveWidth` to its waveform: the frame's insets and DI/CAB's fader. */
    static int bassWidthForWave (int waveWidth) noexcept { return waveWidth + fader::columnW + gap + 2 * contentInsetX(); }

    /** OrbitBass's DI/CAB rail, or none on the guitar's (a gate reads it). */
    GuardRail* diRail() noexcept;

    /** What the border names as the cabinet that plays, as last shown — "1: 8x10 STACK", a player's own IR's name, NO IR
        (a gate reads it). */
    const juce::String& shownName() const noexcept { return shownText; }

    /** THE IR MENU, as the block opens it (showIrMenu): the product's twelve — the guitar's TOP 12, the bass's dozen —
        item `firstOfTwelve + i`; BUILDER, its builder app's menu (the owner, 11.11): its presets by style, item
        `firstOfBuilder + i` for device::Cabinets::all()[i], and MY PRESETS, item `firstOfSaved + i` for `saves[i]`; then
        the player's own IRs out of the product's folder of them, item `firstFile + i` for the file `files[i]`. A gate
        reads it. */
    static juce::PopupMenu irMenu (const AmpProcessor&, std::vector<juce::File>& files,
                                   std::vector<device::Cabinets::Cabinet>* saves = nullptr);
    static constexpr int firstOfTwelve = 500;
    static constexpr int firstOfBuilder = 600;
    static constexpr int firstOfSaved = 800;
    static constexpr int firstFile = 1000;

    /** What the border says where no cabinet plays: a bass amp with no IR of its own. */
    static constexpr const char* noIr = "NO IR";

private:

    struct Switch
    {
        ZoneSwitch  sw;
        juce::Label label;
        std::unique_ptr<juce::ParameterAttachment> att;
    };

    std::array<Switch, 3> switches;   // HPF, LPF, Ø — TRIM became the combo below

    /** The trim's whole story in one cell: OFF, a fixed window, or MANUAL. A combo in behaviour,
        our own face in pixels — the consoles' SlopeCombo grammar. */
    struct TrimCombo final : public juce::Component
    {
        std::function<juce::String()> getText;
        std::function<void()>         onOpen;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override { if (onOpen) onOpen(); }
    };

    enum class TrimMode { off, fixed, manual };

    void showTrimMenu();
    void applyTrimPick (int itemId);

    /** Reads the mode off the parameters — for init and outside writes (automation, recall).
        An explicitly chosen MANUAL survives landing exactly on a mark: the magnet snaps there,
        and a handle that vanished under the hand would be the bug, not the feature. */
    void deriveTrimMode();

    TrimCombo trimCombo;
    TrimMode  trimMode   = TrimMode::off;
    double    trimModeMs = 0.0;

    /** The mode is HELD — nothing may re-derive it until the writes underneath have landed.

        A parameter attachment does not silence its own write: each `setValueAsCompleteGesture`
        comes straight back through `pushToWave` as it lands, so the FIRST of a pair is read while
        the second value is still yesterday's. That was enough to lose a mode. Asking for a fixed
        window from OFF turned the switch on, the mode was derived from the old fraction, missed
        every mark, landed on MANUAL — and MANUAL is sticky by design, so the value that arrived a
        line later could not take it back. It read as "sometimes it works", because sometimes the
        old fraction WAS on a mark.

        A chosen mode is not derivable in the first place — MANUAL sitting exactly on a mark is the
        same two numbers as a fixed pick — so while it is held, the mode is what it was set to, and
        derivation is left to what it is for: recall and automation. */
    bool modeIsHeld = false;

    /** Whether the trim was on at the last push. The view window has to be squared with the mode
        on the OFF->ON edge and only there — see `pushToWave`. */
    bool trimWasOn = false;

    std::unique_ptr<juce::ParameterAttachment> irAtt, hpfHzAtt, lpfHzAtt, trimAtt, trimOnAtt,
                                               hpfSlopeAtt, lpfSlopeAtt;

    /** The same liquid analyser the consoles run — one pane for the IR's door, one for its exit. */
    std::array<felitronics::analysis::SpectrumPane, 2> panes;

    friend class WebFaceplate;   // the site's kit reads where the parts stand (WebFaceplate.h)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CabinetBlock)
};

} // namespace orbitamp
