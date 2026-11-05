// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Features.h"
#include "Product.h"
#include "Parameters.h"
#include "ui/Prefs.h"
#include "ui/HumTuning.h"
#include "core/CapturedBlock.h"
#include "core/DemoPlayer.h"
#include "core/ScopeTap.h"
#include "core/TunerEar.h"
#include "core/TunerPoly.h"
#include "core/TunerStrobe.h"
#include "core/TunerTap.h"
#include "core/WaveRibbon.h"
#include "core/EqLink.h"
#include "core/HumLearnLink.h"
#include "core/BassComp.h"
#include "core/OctaveTap.h"
#include "core/BassCuts.h"
#include "core/FilterTap.h"
#include "core/ChorusTap.h"
#include "core/BypassFade.h"
#include "core/BypassWire.h"
#include "core/CabinetIr.h"
#include "core/DoorLimiter.h"
#include "core/LateWire.h"
#include "device/EmbeddedIrs.h"
#include "worlds/Ear.h"

#include <felitronics/analysis/RollingSpectrumTap.h>
#include <felitronics/chorus/BassChorus.h>
#include <felitronics/envfilter/EnvelopeFilter.h>
#include <felitronics/octave/Octaver.h>
#include <felitronics/appkit/CompareHistory.h>
#include <felitronics/appkit/UpdateChecker.h>
#include <felitronics/dehum/HumRemover.h>
#include <felitronics/delay/TapeDelay.h>
#include <felitronics/dynamics/NoiseGate.h>
#include <felitronics/reverb/AmpReverb.h>
#include "core/SpaceTap.h"
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <mutex>

namespace orbitamp
{

/** WHAT A PRODUCT ADDS TO THE ENGINE THAT IS NOT ITS FACE. A face is there while its window is open;
    this is there for as long as the processor is — what a host can reach with the window shut. The
    cockpit keeps its four channels in one (channels/Engine.h); OrbitAmp has none. */
struct ProductEngine
{
    virtual ~ProductEngine() = default;

    /** On the processor's own timer, on the message thread, before anything else that tick. */
    virtual void pump() = 0;

    /** The state changed under the processor — a session opened, a register recalled, a step of the
        history taken. Said from inside the landing itself: it may look, and must not write. */
    virtual void stateLanded() = 0;
};

/** The plugin shell. Deliberately thin: the chain (preamp -> EQ -> delay -> reverb -> cabinet) lands
    in src/core/ behind small engines, and this class only pumps buffers into them and owns state. */
class AmpProcessor final : public juce::AudioProcessor,
                           private juce::Timer,
                           private juce::ValueTree::Listener
{
public:
    /** A processor of `product`, which it keeps: which amp it is (Product.h). The product outlives it —
        every product is made once in a process and never goes. */
    explicit AmpProcessor (const Product& product);
    /** Takes the saved session's listener off the tree, and nothing else — worth writing down WHY
        nothing else, because this looks like the place a weak reference has to be cleared and it
        is not.

        `setStateInformation` can arrive on any thread, so it marshals the restore to the message
        thread behind a `juce::WeakReference<AmpProcessor>`, and the host may destroy the plugin
        while that call is still queued. The guard against that is real — but it is already armed:
        `JUCE_DECLARE_WEAK_REFERENCEABLE` does not declare a bare `WeakReference::Master` (whose
        destructor only asserts). It declares a `WeakRefMaster` whose destructor calls `clear()`,
        and declares it LAST, so it is the first member destroyed. Adding a clear here changes
        nothing at all. I wrote one anyway, on a reading of the wrong destructor, and the review
        caught it. */
    ~AmpProcessor() override { apvts.state.removeListener (this); }

    /** The hum stage's type (removeHum): room for 128 harmonics, 6.4 kHz at 50 Hz. */
    using HumStage = felitronics::dehum::HumRemover<128>;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processorLayoutsChanged() override;

    /** Where the amp itself puts that parameter back, normalised — see params::engineDefault. */
    float engineDefault (const juce::RangedAudioParameter& p) const { return params::engineDefault (p, enginePlace); }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    /** The chain, over at most the block size prepareToPlay promised — see processBlock. */
    void processChunk (juce::AudioBuffer<float>&);

    /** The largest block prepareToPlay was told to expect; 0 before the first prepare. */
    std::atomic<int> preparedBlock { 0 };

    /** A parameter's audio-thread mirror, and its default — see sanitiseParameters. One that `holdsLast` (the bass's
        effects' sliders) falls back to the last number it held instead: the audio thread's own copy. */
    struct ParameterGuard { std::atomic<float>* value; float fallback; bool holdsLast; float last; };
    std::vector<ParameterGuard> parameterGuards;

    /** Puts a parameter that is not a number back on its default before the chain reads it. */
    void sanitiseParameters() noexcept;

public:

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                          { return true; }

    const juce::String getName() const override              { return ownProduct.name; }

    /** WHICH AMP THIS IS (Product.h): what everything that is one amp's and not another's is read off —
        handed down from here, never looked up anywhere else. */
    const Product& product() const noexcept                  { return ownProduct; }

    /** WHAT THIS IS, AND WHERE, for a line of the family's logs (shaders/GpuLog.h): the product, the format it
        was loaded as — VST3, AU, CLAP or Standalone — and, a plug-in, its host: by JUCE's name for it where
        JUCE knows it, by its program's file name where not. */
    juce::String loadedAs() const;

    bool acceptsMidi() const override                        { return false; }
    bool producesMidi() const override                       { return false; }
    bool isMidiEffect() const override                       { return false; }
    /** HOW LONG THIS GOES ON SOUNDING after the last note went in — asked by a host that is
        rendering offline, freezing a track, or deciding when it may stop calling us.

        It is not about the bypass, though that is where it was noticed. It is about every moment
        the input goes quiet while something in here is still ringing, which is most of the time a
        player stops playing.

        It used to be a flat eight seconds, and eight is the wrong number in both directions. Too
        short: a HALL at DECAY ×2 rings for nearly ten, and a two-second echo at 95% repeats takes
        over four minutes to reach a thousandth of itself. Too long: with the room and the echo out
        of the rig there is nothing here to ring at all, and a host was still rendering eight
        seconds of silence onto the end of every bounce. So the two links that HAVE tails are asked
        what theirs currently is, and only while they are in the rig.

        THEY ADD UP, they do not compete. The links are in SERIES: an echo that leaves the delay
        two seconds after the note is what the room then has to ring about, and the speaker is
        still saying its piece after that. Taking the longest of the three would report two seconds
        for a rig that is audibly going for six.

        Capped, because a delay at a hundred per cent repeats never decays and no honest number
        exists for it: thirty seconds is far past any musical use of an echo and still nothing to
        render. Floored at a tenth for the ramps and the models' own few milliseconds. */
    double getTailLengthSeconds() const override
    {
        double tail = 0.1;   // every ramp in here, and the models' handful of milliseconds

        // Every chain holds the echo and the room now; the bass's may have them out of its strip, and a host that
        // asks once must not cut one added later — so their tails count either way.
        const bool echoes = chainHolds (params::rowDelay) || chainHolds (params::rowReverb);

        if (chainHolds (params::rowChorus))
            tail += (double) felitronics::chorus::BassChorus::tailSeconds();
        if (chainHolds (params::rowDelay))
            tail += (double) delay.tailSeconds();
        if (chainHolds (params::rowReverb))
            tail += (double) reverb.tailSeconds();
        tail += (double) cab.tailSeconds();

        // A FLOOR, BECAUSE NOTHING CAN TELL A HOST THIS NUMBER MOVED. There is no such message in
        // any of the three wrappers — JUCE's change notifications carry latency, programs, state
        // and parameter info, and nothing for a tail — so a host that asks once, when it activates
        // the plugin, keeps whatever answer it got. If it asked while the echo was at its default
        // and the player then dials two seconds at ninety per cent, the honest live number never
        // reaches it, and one host at least stops calling a plugin at all after `tail` seconds of
        // silence: that would cut a tail while PLAYING, not only in a bounce.
        //
        // So the answer is never below the flat eight this used to be: the echo and the room are
        // always in the chain, and either can be set to ring long. A hall breathing at double is
        // no longer cut at eight, and the one case that stays wrong in a caching host is one where
        // the old constant was exactly as wrong. Lifting the floor is a thing to do after watching
        // a real session in Cubase and Logic, not before.
        // A chain with neither would floor at the longest IR the cabinet takes (5 s), so a host that asks once never
        // cuts one that is loaded later.
        tail = juce::jmax (tail, echoes ? 8.0 : 5.0);

        return juce::jmin (tail, 30.0);
    }

    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    /** THE SESSION AS A HOST WILL READ IT, kept ready for a save that does not come from the
        message thread.

        Everything a session is made of — the history's registers, the parameter tree, the names
        beside the numbers — is the message thread's, and hosts that save from a worker thread (a
        real habit: autosave, project save in the background) used to walk all of it while the
        message thread was changing it. Now the message thread writes the session down whenever it
        has changed — at most a few times a second, from the pump — and a save from anywhere else
        hands out that copy under a lock. It is never torn. It is as fresh as the message thread's
        last chance to write it: a third of a second while the host keeps its message loop running,
        longer if the host holds that loop still — the one thing a copy cannot know about.

        `force` writes it now whatever has changed. Message thread only. */
    void refreshSavedState (bool force = false);

    // FIRST OF ALL, the product: everything after it is made for it.
    const Product& ownProduct;

    // BEFORE THE PARAMETERS, and so before everything: what the product does once in a process before
    // it reads anything off the disk (Product::firstStart) — the list of devices a parameter offers is
    // read when the parameters are made. A member, because the constructor's body would be too late.
    struct FirstStart { explicit FirstStart (const Product&); } firstStarted;

    juce::AudioProcessorValueTreeState apvts;

    /** Undo/redo + the A/B/C/D registers, from felitronics-appkit. It lives on the PROCESSOR, not
        the editor: the history is part of the session, so it has to survive closing the window and
        be saved with the plugin state.

        PerRegister topology — each register carries its own undo history and switching registers is
        NOT an undo step (its inverse is simply selecting the other slot). That is the deliberate
        opposite of OrbitCab, whose whole workspace is one timeline. */
    felitronics::appkit::CompareHistory history;

    /** The release check behind the footer's version badge. OPT-IN and offline-safe: nothing reaches
        the network until a player presses "Check for updates" in the badge's popover — the dot only
        reports a release seen by an earlier, deliberate check. It sits on the processor for the same
        reason the history does: the window comes and goes, and this outlives it. */
    felitronics::appkit::UpdateChecker& updateChecker() noexcept { return updater; }

    /** Which wrapper is actually running — VST3 / AU / CLAP / Standalone — the badge's second line. */
    juce::String pluginFormat() const { return juce::AudioProcessor::getWrapperTypeDescription (wrapperType); }

    /** The editor's zoom, 50-400%. It lives here rather than in the editor so it survives closing
        and reopening the window; it is message-thread only and never read by the audio path.
        Persisting it across sessions comes with the state work. */
    float getEditorScale() const noexcept { return editorScale; }
    void setEditorScale (float s) noexcept { editorScale = juce::jlimit (minScale, maxScale, s); }

    static constexpr float minScale = 0.5f;
    static constexpr float maxScale = 4.0f;

    /** What a window OPENS at, before the screen has its say. Separate from editorScale on purpose:
        editorScale follows the window, and the window is set by whoever hosts it — so an editor that
        opened from it was reading back whatever the last host had shrunk it to, and once anything
        shrank it the value stuck. A wanted size and a current size are two different facts. */
    static constexpr float preferredScale = 1.0f;

    /** The captured blocks, addressed by number, so the aim, the name pump and the seed can all walk
        them the same way. There is one — the preamp — and the walk stays a walk, so a block that
        joins it is a row here and not a second copy of every loop. */
    static constexpr size_t numCaptured = 1;

    core::CapturedBlock& blockAt (size_t) noexcept { return preamp; }
    static const char*   deviceIdOf (size_t) noexcept { return params::preampDevice; }
    static juce::String  measuredIdOf (size_t, int i) { return params::preampMeasured (i); }

    /** THE STATE AS IT SHOULD BE WRITTEN DOWN — the live tree with its names reconciled first.

        Anything that copies `apvts.copyState()` straight out can catch the one-tick gap between a
        control moving and the pump writing its name: a preset saved in that window carries a new
        NUMBER beside an old NAME, and the name wins on the way back, so the preset reopens on the
        device the player had just moved away from. Every path that persists the tree goes through
        here. Message thread only.

        `forgetIdentity` is for a factory reset: every parameter is about to be set to its default,
        and a name left over from the outgoing device would be aimed straight back over it. */
    juce::ValueTree stateForSaving (bool forgetIdentity = false)
    {
        pumpDeviceWork();
        pumpSwitchNames();

        auto tree = apvts.copyState();

        if (forgetIdentity)
            for (size_t b = 0; b < numCaptured; ++b)
            {
                tree.removeProperty (juce::Identifier (juce::String (deviceIdOf (b)) + deviceAimSuffix), nullptr);

                for (int i = 0; i < core::CapturedBlock::numMeasured; ++i)
                    tree.removeProperty (juce::Identifier (measuredIdOf (b, i) + switchAimSuffix), nullptr);
            }

        // A cabinet IR of the player's own is identity the same way: the parameters going to their
        // defaults would leave it playing over the default cabinet.
        if (forgetIdentity)
            for (const auto* id : { params::cabIrUserKey, params::cabIrUserName, params::cabIrUserFrom })
                tree.removeProperty (id, nullptr);

        return tree;
    }

    /** A PRESET AS IT GOES TO DISK: the state as `stateForSaving` writes it, with the cabinet IR it
        plays embedded whole when that IR is the player's own — so the preset sounds the same
        wherever it is opened. Message thread only. */
    juce::ValueTree presetForSaving()
    {
        auto tree = stateForSaving();
        embedCabIrs (tree);
        stamp (tree);
        return tree;
    }

    /** WHOSE STATE IT IS. A session and a preset carry the product that wrote them and its chain; a guitar amp's
        never fits the bass's chain, nor the bass's a guitar amp's, and one that does not fit is not loaded. A
        state with no stamp was written before there was a bass — a guitar amp's: the guitar amps load it as
        they always have, OrbitBass refuses it. */
    void stamp (juce::ValueTree& tree) const
    {
        tree.setProperty (stampProduct, juce::String (ownProduct.name), nullptr);
        tree.setProperty (stampChain, ownProduct.chain == Chain::bass ? "bass" : "guitar", nullptr);
    }

    bool belongs (const juce::ValueTree& tree) const
    {
        const bool bassState = tree.getProperty (stampChain).toString() == "bass";
        return bassState == (ownProduct.chain == Chain::bass);
    }

    static void unstamp (juce::ValueTree& tree)
    {
        tree.removeProperty (stampProduct, nullptr);
        tree.removeProperty (stampChain, nullptr);
    }

    static constexpr const char* stampProduct = "product";
    static constexpr const char* stampChain   = "chain";

    /** The other half: a tree read from disk gives its embedded IRs to the store and comes back a
        plain state, before anything applies it. Message thread only. */
    void takeEmbeddedIrs (juce::ValueTree& tree) { cabIrEmbeds.unpack (tree); }

    /** A STATE OF 0.8.0'S, made what it means now. Up to 0.8.0 the cockpit's and the rovers' face folded
        the lever's zero, a machine setting (-9 dB unless the player chose another), into the input trim,
        and noted beside it which zero it had folded in (Product::formerInputZeroId). The trim is unity at
        the lever's middle now and the level is the pack's own (`chain[].input_db`), so every tree of the
        state's type in `tree` — the live one, each register, a preset — that carries that note N has
          - its input trim made the trim less N, within the trim's range: what the lever adds to the
            pack's level, so that with the pack's -9 dB the model is fed what it was fed then; and
          - its gate's threshold raised by -N, within the gate's range: the gate's key hears the guitar
            after the input trim and before the pack's level, where 0.8.0's heard it after the folded-in
            zero, so it opens on the same guitar it opened on then;
        and the note taken away. A state with no such note — OrbitAmp's, or one no 0.8.0 window ever
        saw — is left as it is. Any thread: it touches nothing but `tree`. */
    void takeFormerTrims (juce::ValueTree& tree) const;

    /** A STATE SAVED UNDER ANOTHER OPENING CABINET, on the one every amp opens on now (params::cabIrDefault,
        RED GIANT). Every state carries a note of the cabinet a fresh instance opened on when it was saved
        (`cabOpeningId`, the catalog's number); one with no note was saved before the note, when that was
        DARK MATTER. A state cannot say whether it plays its opening cabinet because the player chose it or
        because nobody chose another, and the old one is taken as nobody's choice: in every tree of the
        state's type in `tree` — the live one, each register, a preset — whose note names another cabinet
          - `cabIr` on that cabinet's number is moved to the opening one's;
          - where the product's channels play the build's cabinets as IRs of their own (a ProductEngine:
            the cockpit and the rovers), the cabinet playing under the old one's file name is made the new
            one — the new one's bytes into the IR store, their key, name and `from` on the state — and
            every channel's memory that names the old one's file names the new one's;
        and the note is made the opening cabinet's. Any other cabinet — another of the shelf's, an IR of
        the player's, one the state carries — is kept, and so is the old opening cabinet in a state whose
        note says it was the player's pick. Any thread: it touches `tree`, and the IR store, which takes
        bytes from any. */
    void takeFormerCabinet (juce::ValueTree& tree);

    /** A BASS STATE ON NO CABINET OF THE BASS'S, on the bass's own. A bass amp plays no guitar cabinet
        (Product::playsCabIr), and the catalog holds no bass cabinet yet, so in every tree of the state's type
        in `tree` — a fresh instance's, INIT's, a session or a preset saved while the bass opened on RED
        GIANT, one whose IR of the player's did not survive — that plays neither an IR of the player's the
        store holds nor a cabinet the bass plays, the cabinet is made THE FIRST IR OF THE PLAYER'S `Bass IRs`
        that plays, in the order the tree lists them (device::IrLibrary::listed): its bytes into the store,
        and its key, name and `from` on the state, as a pick of it leaves them — so a session keeps what it
        played wherever it opens. With none there it is left as it is, on no cabinet: no bytes, and nothing
        loads, as with the factory folder missing. A guitar amp's state is left as it is. Any thread: it
        touches `tree`, the IR store, which takes bytes from any, and reads the player's folder. */
    void takeBassCabinet (juce::ValueTree& tree);

    /** The note (takeFormerCabinet): on every state, from the first moment of the processor's (its
        constructor) and of every state that lands. */
    static constexpr const char* cabOpeningId = "cab_opening";

    /** Where a channel's memory names its cabinet, by its file's name without the extension
        (channels/Channels.cpp) — said here, for takeFormerCabinet to read it by. */
    static constexpr const char* recordCabinetId = "cabinet";

    /** The state tree's type — a constant, so code off the message thread can recognise a state
        inside a saved envelope without touching the live tree. */
    static constexpr const char* stateType = "state";

    /** The suffix a switch slot's saved position name wears in the state tree. */
    static constexpr const char* switchAimSuffix = "_pos";

    /** And the one a captured block's saved DEVICE name wears, beside its index parameter. */
    static constexpr const char* deviceAimSuffix = "_dev";

private:
    static constexpr int aimWindowFrames = 150;   // ~5 s at the 30 Hz pump

    int switchAimFrames = 0;                      // ticks left to keep aiming after a state change

    /** WHAT THE NAME PUMP LAST SAW. A name is written when the PARAMETER moves and at no other
        moment, which is the whole of what keeps this out of the undo timeline: a pack finishing
        its load, a rescan, a block coming back into the rig — none of them are edits, and none of
        them may leave a mark the player then has to press Cmd-Z through. Writing on a move also
        puts the name in the same settle burst as the move itself, so the two can never be
        committed apart and can never disagree inside one undo step.

        `namesPrimed` is the first tick, which only takes the baseline. */
    bool  namesPrimed = false;
    float lastDeviceValue[numCaptured] { -1.0f };
    float lastSwitchValue[numCaptured][core::CapturedBlock::numMeasured] { };

    /** What the aim itself last wrote, so a value that no longer reads it can be told from one
        that does — which is how a hand on the control wins against an aim still in flight. */
    float aimWroteDevice[numCaptured] { -1.0f };
    float aimWroteSwitch[numCaptured][core::CapturedBlock::numMeasured] { };

    // MUST precede `updater`: the checker takes the store by reference and is destroyed before it.
    std::shared_ptr<prefs::UpdateStore> updateStore = prefs::UpdateStore::of (ownProduct);   // the product's own, shared by its processors
    felitronics::appkit::UpdateChecker updater;   // built in the ctor: it needs the store above

    /** Pumps the history's settle timer — a burst of edits that has been quiet for a moment commits
        as ONE undo step, so dragging a knob is not fifty of them. */
    void timerCallback() override
    {
        // The product's own half first — what it does it does as a hand would, between two ticks.
        if (productEngine != nullptr)
            productEngine->pump();

        history.tick();

        // THE AIM GOES FIRST, and the order is load-bearing. It writes the device NUMBER; the pump
        // below is what notices and loads it. Run the other way round, every restore of a session
        // whose numbering had drifted played the wrong pack for a whole tick — a real load and a
        // real crossfade — before being corrected. This way the number is right before anyone
        // reads it, and the only cost is that the aim's first tick judges `selectedName()` from
        // the previous one, which it already had to.
        applySwitchAims();
        pumpDeviceWork();
        pumpTuner();
        pumpSwitchNames();

        refreshSavedState();   // last: the pumps above write names the session carries
    }

    /** A DEVICE AND ITS SWITCHES ARE SAVED BY NAME, and this is the half that puts them back.

        Both parameters a host sees are numbers, and both numbers are places in a list that belongs
        to this machine on this day. The device's is an index into whatever packs are on disk,
        in the AMP menu's order — drop one new pack in and everything
        after it renumbers. A switch's is a fraction, an index into the pack's own position list —
        repack a device one position shorter and every old session reopens on a different position.
        Both drift silently, and the second is the one a player would never think to check.

        So the state carries NAMES beside the numbers, and on the way back the name decides. The
        device first, because the positions belong to its pack; then the switches, as soon as that
        pack is actually here, which is not the moment the state arrives — models load on the pool.
        Until then the aim stands.

        It stops standing after a few seconds. A player who reaches for that switch before the pack
        lands must win: an aim that outlived its window and overwrote a live hand would be worse than
        the drift it was fixing. */
    void applySwitchAims()
    {
        if (switchAimFrames <= 0)
            return;

        --switchAimFrames;
        bool waiting = false;

        // Everything this pass wants to change, decided before anything is written. Two reasons.
        // A suppression scope flushes whatever burst is open when it starts, so opening one on a
        // tick with nothing to write would chop a player's knob move into one undo step per tick
        // for five seconds. And the writes have to go in TOGETHER, because they are one
        // reconciliation, not twelve.
        std::vector<std::pair<juce::RangedAudioParameter*, float>> writes;

        // A HAND OUTRANKS THE AIM. If a parameter no longer reads what this aim last put there,
        // somebody moved it — a player auditioning devices the moment a session opens, or host
        // automation — and the aim stands down for that parameter rather than dragging it back
        // five seconds later. The document promised this; the code did not do it.
        const auto stolen = [] (const juce::RangedAudioParameter* p, float known)
        {
            return p != nullptr && ! juce::approximatelyEqual (p->getValue(), known);
        };

        // Standing down is not enough on its own. The name pump is quiet while an aim is running
        // and takes each tick's values as its baseline, so a hand that moved a control mid-window
        // would leave the STORED name still describing what the aim wanted — the player's choice
        // would sound now and be gone at the next load. So the theft is written down where it
        // happens, and ONLY for the thing that was taken: the other block may still be waiting for
        // its pack, and recording what it is playing at this instant would replace the name it is
        // still trying to reach with the name of whatever the stale number loaded.

        // Whether this block's DEVICE is where its name says it should be. The switch half must
        // not run until it is: the positions being aimed at belong to the named pack, and asking
        // the pack that happens to be loaded whether it has a position called "Lead" is asking
        // the wrong device a question about the right one.
        bool deviceSettled[numCaptured] = { true };

        const auto aimDevice = [&] (size_t b)
        {
            auto& block = blockAt (b);
            const auto* id = deviceIdOf (b);

            const juce::Identifier key (juce::String (id) + deviceAimSuffix);

            if (! apvts.state.hasProperty (key))
                return;

            const auto want = apvts.state.getProperty (key).toString();

            if (want.isEmpty())
                return;

            auto* p = apvts.getParameter (id);

            if (stolen (p, aimWroteDevice[b]))
            {
                // A hand picked a different device. What it picked is the answer now — including
                // for the switches, whose stored positions belong to the device that just left,
                // which is why this drops them and writes the new pack's instead. That also
                // leaves the switch half below nothing of the old device's to aim.
                if (noteBlockNames (b, true))
                {
                    aimWroteDevice[b] = p->getValue();
                }
                else
                {
                    // The hand's device has not been loaded yet, so this block's switches must not
                    // be aimed either: the names below still describe the pack that is leaving, and
                    // the pack standing here may spell a position the same way.
                    deviceSettled[b] = false;
                    waiting = true;
                }

                return;
            }

            const int index = block.indexOfName (want);

            if (index < 0)
            {
                // ...and its switch positions are not asked about either. They belong to the pack
                // that is missing, and the pack standing in for it may spell a position the same
                // way — aiming them would move a control on a device the player never chose.
                deviceSettled[b] = false;

                // This machine has no such device — TODAY. The name STAYS: it is the identity the
                // player chose, and a folder is a thing that gets filled in later. Deleting it
                // here would mean that opening a project before installing its pack, or on a
                // machine whose Devices folder is still empty, silently replaces the chosen device
                // with the fallback for ever. The number stands, the block prints whose voice is
                // actually playing, and the moment the pack arrives the name resolves on its own.
                return;
            }

            if (p != nullptr)
            {
                const float v = p->convertTo0to1 ((float) index);

                if (! juce::approximatelyEqual (p->getValue(), v))
                {
                    writes.emplace_back (p, v);
                    aimWroteDevice[b] = v;
                }
            }

            // Satisfied only when the block is actually PLAYING the named device, not when the
            // number has been written: setting the parameter is one tick and the pump that loads
            // the pack is the next. A window that closed in between would let the half that
            // writes names back record the device it was aiming away from.
            if (block.selectedName() != want)
            {
                waiting = true;
                deviceSettled[b] = false;
            }
        };

        for (size_t b = 0; b < numCaptured; ++b)
            aimDevice (b);

        const auto aim = [&] (size_t b)
        {
            auto& block = blockAt (b);

            if (! deviceSettled[b])
            {
                waiting = true;   // ask again once the named device is the one that is playing
                return;
            }

            for (int i = 0; i < core::CapturedBlock::numMeasured; ++i)
            {
                const auto id = measuredIdOf (b, i);
                const juce::Identifier key (id + switchAimSuffix);

                if (! apvts.state.hasProperty (key))
                    continue;

                const auto want = apvts.state.getProperty (key).toString();

                if (want.isEmpty())
                    continue;

                auto* p = apvts.getParameter (id);

                if (stolen (p, aimWroteSwitch[b][(size_t) i]))
                {
                    noteSwitchName (b, i);            // the hand's answer, written where it happened
                    aimWroteSwitch[b][(size_t) i] = p->getValue();
                    continue;
                }

                // ALREADY THERE, judged by the position rather than by the number. A switch's
                // parameter is quantised to a thousandth, and a three-position switch's middle is
                // a third — a number the grid cannot hold. Comparing floats, the aim found a
                // difference on every single restore and wrote one every time, and each write
                // opens a suppression scope, which commits whatever burst is open and can drop a
                // redo. Asking which POSITION the value lands on is the question that was meant.
                if (block.isReady() && block.switchValueAt (i, p->getValue()) == want)
                    continue;

                const float v = block.switchParameterFor (i, want);

                if (v < 0.0f)
                {
                    // Nothing loaded at all: ask again next tick. (This is a narrower window than
                    // it looks — `RigPlayer::load` reads the manifest synchronously, so a block
                    // knows its positions the instant its device is selected, long before the
                    // model bytes arrive.) A pack that IS here and has no position by that name —
                    // one updated since the session was saved — keeps the name where it is, for
                    // the same reason the device's stays: it is what the player chose, it costs
                    // nothing to keep, and a pack put back as it was resolves it again. These
                    // never leak to another device: changing the device drops them.
                    if (! block.isReady())
                        waiting = true;

                    continue;
                }

                if (p != nullptr && ! juce::approximatelyEqual (p->getValue(), v))
                {
                    writes.emplace_back (p, v);

                    // The baseline is what the parameter will BECOME, not what we asked for: a
                    // switch's range snaps to a thousandth, and a three-position middle is a third,
                    // so asking for 0.33333 leaves 0.333 behind. Storing the ask made the next tick
                    // read a theft that never happened. It rewrote the same name and was harmless,
                    // and it was still a lie in a variable whose whole job is to tell the truth.
                    aimWroteSwitch[b][(size_t) i] = p->convertTo0to1 (p->convertFrom0to1 (v));
                }
            }
        };

        for (size_t b = 0; b < numCaptured; ++b)
            aim (b);

        if (! writes.empty())
        {
            // NOT an edit, and not a gesture. Putting a number back where its name says it belongs
            // is the plugin agreeing with the state it was handed — so it must not become an undo
            // step the player has to press Cmd-Z through on a session they just opened, and it
            // must not look to a host in automation Write mode like a hand on the control. The
            // suppression scope absorbs the drift into the baseline and leaves the workspace
            // marked dirty, which is honest: what is in memory no longer matches what is on disk.
            const felitronics::appkit::CompareHistory::ScopedSuppress hush (history);

            for (auto& [p, v] : writes)
                p->setValueNotifyingHost (v);
        }

        if (! waiting)
            switchAimFrames = 0;
    }

    /** WRITES DOWN WHAT A BLOCK IS PLAYING — its device, and where each of its switches stands —
        and moves the pump's baselines with it, so what was just written counts as recorded rather
        than still pending. The one place any of these names is produced.

        `dropPositions` is for a device CHANGE: a position name is only ever about the device it
        was read from, so the old device leaving takes all of them with it, which is what stops a
        name resolving later on some other pack that spells a position the same way. They are put
        back from the new pack in the same pass — `RigPlayer` reads its manifest synchronously, so
        they are knowable at once, and a device that arrives with no names is a device saved by
        number until every one of its switches has been touched. */
    bool noteBlockNames (size_t b, bool dropPositions)
    {
        auto& block = blockAt (b);
        const auto* devId = deviceIdOf (b);

        // WHAT IS LOADED HAS TO BE AN ANSWER ABOUT THIS NUMBER. The device parameter can move from
        // the audio thread between the pass that loads it and this one, and naming the block in
        // between writes the leaving device's name beside the arriving device's number — a
        // disagreement nothing afterwards would notice. The caller asks again next tick.
        // Read ONCE. Reading again for the baseline lets automation change the number between the
        // two, so the name written is about the number that was here and the baseline is about the
        // number that arrived — the disagreement this guard exists to prevent, moved four lines.
        const float dv = apvts.getRawParameterValue (devId)->load();

        if (juce::roundToInt (dv) != block.selectedIndex())
            return false;

        const juce::Identifier devKey (juce::String (devId) + deviceAimSuffix);

        if (const auto name = block.selectedName(); name.isNotEmpty())
            apvts.state.setProperty (devKey, name, nullptr);
        else
            apvts.state.removeProperty (devKey, nullptr);

        lastDeviceValue[b] = dv;

        for (int i = 0; i < core::CapturedBlock::numMeasured; ++i)
        {
            const auto id = measuredIdOf (b, i);
            const juce::Identifier key (id + switchAimSuffix);
            const float v = apvts.getRawParameterValue (id)->load();

            if (dropPositions)
                apvts.state.removeProperty (key, nullptr);

            if (const auto n = block.switchValueAt (i, v); n.isNotEmpty())
                apvts.state.setProperty (key, n, nullptr);
            else if (! dropPositions)
                apvts.state.removeProperty (key, nullptr);   // a swept knob owns no name

            lastSwitchValue[b][(size_t) i] = v;
        }

        return true;
    }

    /** One switch slot, when that slot alone is what moved. */
    void noteSwitchName (size_t b, int i)
    {
        const auto id = measuredIdOf (b, i);
        const juce::Identifier key (id + switchAimSuffix);
        const float v = apvts.getRawParameterValue (id)->load();

        if (const auto n = blockAt (b).switchValueAt (i, v); n.isNotEmpty())
            apvts.state.setProperty (key, n, nullptr);
        else
            apvts.state.removeProperty (key, nullptr);

        lastSwitchValue[b][(size_t) i] = v;
    }

    void pumpSwitchNames()
    {
        // While an aim is in flight the STORED names are the truth and the parameters are being
        // moved to match them: read the values as the new baseline and write nothing at all.
        const bool quiet = switchAimFrames > 0 || ! namesPrimed;

        for (size_t b = 0; b < numCaptured; ++b)
        {
            const float dv = apvts.getRawParameterValue (deviceIdOf (b))->load();

            if (! quiet && ! juce::approximatelyEqual (dv, lastDeviceValue[b]))
            {
                // The baseline is only taken when the write actually happened; otherwise the next
                // tick asks again, by which time the pack has been selected.
                noteBlockNames (b, true);
                continue;
            }

            lastDeviceValue[b] = dv;

            for (int i = 0; i < core::CapturedBlock::numMeasured; ++i)
            {
                const float v = apvts.getRawParameterValue (measuredIdOf (b, i))->load();

                if (! quiet && ! juce::approximatelyEqual (v, lastSwitchValue[b][(size_t) i]))
                    noteSwitchName (b, i);
                else
                    lastSwitchValue[b][(size_t) i] = v;
            }
        }

        namesPrimed = true;
    }

    /** WRITES EVERY NAME, ONCE, BEFORE ANYBODY IS WATCHING — called from the constructor, after
        the first scan and before the history takes its baseline.

        Without it a fresh instance has no names in it at all, because the pump only writes when a
        parameter MOVES and nothing has moved yet. A player who opens the plugin, likes what the
        default device does, dials a sound around it and saves that as a preset would get a preset
        that identifies its device by number — which is the whole disease, in the commonest case
        there is. The seed costs one pass and, because the baseline is taken after it, it is not an
        edit and leaves no undo step. */
    void seedSwitchNames()
    {
        for (size_t b = 0; b < numCaptured; ++b)
            noteBlockNames (b, false);   // the ctor has just selected: number and pack agree

        // The seed took every baseline, so the pump is primed: a device or switch moved between
        // construction and the first tick is a real move and has to be written, not swallowed as
        // "the first tick only looks".
        namesPrimed = true;
    }

    /** The state changed under us — a session opened, a register recalled: aim the switches again. */
    void markSwitchAimsPending()
    {
        switchAimFrames = aimWindowFrames;
        cabIrParamBaselineStale = true;   // a restored `cab_ir` is not a host moving it — see the pump

        if (productEngine != nullptr)
            productEngine->stateLanded();       // ...and the product's own half has parameters to read the same way

        // BOTH baselines are taken from what the tree has just become, and that is the whole of
        // how a hand wins. `aimWrote*` starting at "nothing written yet" meant theft could only be
        // seen on a parameter the aim had already moved — so a player who reached for a control
        // whose saved number happened to be right was quietly overruled a tick later. And
        // `lastValue` left over from before the restore made the pump read the restored numbers as
        // a hand's move the moment the window closed, and write the fallback's name over the one
        // the aim had been trying to reach. Same fix, twice: start from where we actually are.
        // THE TWO BASELINES ARE NOT IN THE SAME UNIT, and writing them from one number was a bug
        // that inverted this whole feature for every device but the first. `aimWrote*` is compared
        // against `RangedAudioParameter::getValue()`, which is NORMALISED — the device's index 5
        // reads 5/127 there. `lastValue*` is compared against `getRawParameterValue()`, which is
        // the plain index. Seeded from the plain index, the theft test answered "a hand moved
        // this" on the first tick of every restore of any device but index 0, and the aim wrote
        // down whatever the stale number had loaded — destroying the name it was about to honour.
        for (size_t b = 0; b < numCaptured; ++b)
        {
            const auto* devId = deviceIdOf (b);
            lastDeviceValue[b] = apvts.getRawParameterValue (devId)->load();

            if (const auto* p = apvts.getParameter (devId); p != nullptr)
                aimWroteDevice[b] = p->getValue();

            for (int i = 0; i < core::CapturedBlock::numMeasured; ++i)
            {
                // A switch's parameter is a Float over 0..1, so its two units are the same number.
                const float v = apvts.getRawParameterValue (measuredIdOf (b, i))->load();
                aimWroteSwitch[b][(size_t) i] = lastSwitchValue[b][(size_t) i] = v;
            }
        }
    }

    /** Listens only while someone is watching: with no editor there is no needle, and an MPM pass
        thirty times a second for nobody is the definition of waste. */
    void pumpTuner()
    {
        // Nobody watching, or the tuner is not in the rig: an MPM pass thirty times a second
        // for a needle that is not there is the definition of waste.
        const auto display = prefs::tunerDisplay (ownProduct);
        tunerPoly.tune (prefs::tunerTuning (ownProduct), (float) prefs::tunerReference (ownProduct));   // its lanes, even off

        if (getActiveEditor() == nullptr || ! linkWorks (params::rowTuner))
        {
            tunerStrobe.stop (display == prefs::TunerDisplay::strobe);   // off, a strobe still shows as one
            tunerPoly.stop (display == prefs::TunerDisplay::poly);       // ...and the lanes as lanes (AUTO: the needle)
            return;
        }

        if (const double sr = getSampleRate();
            sr > 0.0 && ! juce::approximatelyEqual (sr, tunerEar.preparedRate()))
        {
            tunerEar.prepare (sr);
            tunerStrobe.prepare (sr);
        }

        tunerEar.setLowestHz (ownProduct.tunerLowestHz);
        tunerEar.setLevelFloorDb ((double) prefs::tunerFloor (ownProduct));
        tunerEar.setReferenceHz ((float) prefs::tunerReference (ownProduct));
        tunerEar.update (tunerTap, juce::Time::getMillisecondCounter());

        // The strobe, when the player reads by it: after the ear, whose note it holds to. Off, it is not
        // run at all, and the ear and its needle are exactly what they were without it.
        if (display == prefs::TunerDisplay::strobe)
        {
            tunerStrobe.setLevelFloorDb ((double) prefs::tunerFloor (ownProduct));   // what the ear takes for noise
            tunerStrobe.update (tunerTap, tunerEar);
        }
        else
            tunerStrobe.stop();

        // POLY: every string of a strum at once, off the same tap — run only while the player reads by it, or by
        // AUTO, which shows its lanes for a strum and the needle for one string.
        if (display == prefs::TunerDisplay::poly)
            tunerPoly.update (tunerTap, getSampleRate(), (double) prefs::tunerFloor (ownProduct));
        else if (display == prefs::TunerDisplay::automatic)
            tunerPoly.updateAuto (tunerTap, getSampleRate(), (double) prefs::tunerFloor (ownProduct));
        else
            tunerPoly.stop();
    }

    /** IS THIS LINK WORKING — the one question the chain asks, and the only shape of it. Every
        link stands in the chain for good; a switch says only whether it is ON or in STANDBY, and a
        link with no switch — the two ends — always works. */
    bool linkWorks (params::ChainRow row) const noexcept
    {
        const auto* on = rowOn[(size_t) row];
        return chainHolds (row) && linkInRig (row) && (on == nullptr || on->load() > 0.5f);
    }

public:
    /** WHETHER THE LINK STANDS IN THE CHAIN NOW (any thread): its chain holds it and, where the player may take it out of
        the strip (ChainLink::presentParam — OrbitBass's effects), it is in. Out, it never works, whatever its switch. */
    bool linkInRig (params::ChainRow row) const noexcept
    {
        const auto* in = rowPresent[(size_t) row];
        return chainHolds (row) && (in == nullptr || in->load() > 0.5f);
    }

    /** The same, as the FACE reads it (message thread): from the parameter itself, which a callback of its own already
        holds — the raw value behind the audio thread's read may still be a step behind. */
    bool linkShown (params::ChainRow row) const noexcept
    {
        if (! chainHolds (row))
            return false;
        const char* id = params::chainLinks[(size_t) row].presentParam;
        auto* p = id != nullptr ? apvts.getParameter (id) : nullptr;
        return p == nullptr || p->getValue() > 0.5f;
    }

    /** Whether the link may be taken out of the strip here (its checklist offers it). */
    bool linkRemovable (params::ChainRow row) const noexcept
    {
        const char* id = params::chainLinks[(size_t) row].presentParam;
        return chainHolds (row) && id != nullptr && apvts.getParameter (id) != nullptr;
    }

    /** THE BASS'S DELAY'S AND REVERB'S PICTURES (ui/SpaceBlocks.h): the loudest of what each ADDS, a column every 10 ms
        — written by the audio thread only while `delayWatched` / `reverbWatched`, which the pictures set while they are
        on screen; read on the pictures' own timers. */
    core::SpaceTap delayTap, reverbTap;
    std::atomic<bool> delayWatched { false }, reverbWatched { false };

    /** The reverb's tail as its stage computes it now, seconds (any thread) — the REVERB picture's law. */
    float reverbTailSeconds() const noexcept { return reverb.tailSeconds(); }

    /** Whether the bass's DELAY / REVERB ran its stage in the last block (the audio thread's; a gate reads it between
        blocks): out of the strip, and once its fade is done, it is not run at all. */
    bool spaceRan (bool reverbStage) const noexcept { return spaceRunning[reverbStage ? 1 : 0]; }

private:

public:
    /** The bass's FILTER: where its sweep stands now, Hz — the module's own read-out (any thread). */
    float filterShownCutoffHz() const noexcept { return envFilter.shownCutoffHz(); }

    /** COMP'S PICTURE (ui/CompBlock.h): the columns of its in, its out and its squeeze — written by the audio thread
        only while `compWatched`, which the picture sets while it is on screen; read on the picture's own timer. */
    core::CompTap compTap;
    std::atomic<bool> compWatched { false };

    /** OCTAVE'S PICTURE (ui/OctaveScope.h): the stage's input and its sub, sample by sample — written by the audio thread
        only while `octaveWatched`, which the picture sets while it is on screen; read on the picture's own timer. On the
        heap, made with the processor: a third of a second of two signals is no object for a stack. */
    const std::unique_ptr<core::OctaveTap> octaveTap { std::make_unique<core::OctaveTap>() };
    std::atomic<bool> octaveWatched { false };

    /** THE ENVELOPE'S PICTURE (ui/FilterBlock.h): where FILTER's sweep stood, a column every 5 ms — written by the audio
        thread only while `filterWatched`, which the picture sets while it is on screen; read on the picture's own timer. */
    core::FilterTap filterTap;
    std::atomic<bool> filterWatched { false };

    /** FILTER's knobs as its stage is told them now: the menu's fields from their parameters, through the module's law
        at its slider — at 0 while its link stands by. The stage is told exactly this (tellFilter) and its picture draws
        it (any thread: relaxed reads of the parameters). `base` gives the fields no parameter holds — none today. */
    felitronics::envfilter::EnvelopeFilterParams filterKnobsNow (felitronics::envfilter::EnvelopeFilterParams base = {}) const noexcept;

    /** CHORUS'S PICTURE (ui/ChorusBlock.h): its LFO's phase, derived on every call of the stage and written into the
        ring only while `chorusWatched`, which the picture sets while it is on screen; read on the picture's own timer. */
    core::ChorusTap chorusTap;
    std::atomic<bool> chorusWatched { false };
    /** FOR A GATE — what the inner cuts do: `innerCutsOut` plays the bass's HPF at the END alone; with `probing` on, the
        audio thread keeps the loudest sample going INTO the AMP and the sum of what comes OUT of it (after its cut), so a
        gate reads a thump and an offset at those points. Off, nothing is written. */
    std::atomic<bool> innerCutsOut { false }, probing { false };
    float probeAmpInPeak = 0.0f;
    double probeAmpOutSum = 0.0;
    long long probeAmpOutCount = 0;

    /** The ratio COMP works at now — the law's, or the hand's (the audio thread's; a gate reads it between blocks). */
    double compRatioNow() const noexcept { return bassComp.ratioNow(); }

    /** The three effects for a gate — 0 OCTAVE, 1 FILTER, 2 CHORUS: how many times each has been told its knobs, and
        whether it stands idle (the audio thread's; a gate reads them between blocks). */
    int  effectTells (int which) const noexcept { return effectTold[(size_t) juce::jlimit (0, 2, which)]; }
    bool effectIdle (int which) const noexcept
    {
        return which == 0 ? octaver.isIdle() : which == 1 ? envFilter.isIdle() : chorus.isIdle();
    }

    /** What an effect's lamp shows, 0..1 (any thread): OCTAVE lights while its sub plays (locked), FILTER and CHORUS
        while they are not idle — a lamp that could never light was noise. */
    float effectLampDepth (int which) const noexcept
    {
        const bool lit = which == 0 ? octaver.isLocked() : effectWorking[(size_t) juce::jlimit (1, 2, which)].load (std::memory_order_relaxed);
        return lit ? 1.0f : 0.0f;
    }

    /** Whether OCTAVE is at work — in the chain, switched on, its slider up — as the audio thread last told it (any
        thread): its picture's frame stands whole while it is not. */
    bool octaveAtWork() const noexcept { return octaveWorks.load (std::memory_order_relaxed); }

    /** How many planes the bass's back half runs — from CHORUS on: the prepared bus's, 1 or 2 — or 0 on a guitar amp's
        chain. Any thread. */
    int bassBackPlanes() const noexcept { return bassWide.load (std::memory_order_relaxed); }

    /** WHETHER THIS PRODUCT'S CHAIN HAS THE LINK AT ALL (Product::chain) — the bass's holds every one (its DELAY and
        REVERB out of its strip until the player adds them: linkInRig); a guitar amp's none of the bass's own, OCTAVE,
        ENVELOPE and CHORUS, and COMP only where the amp has one (Product::guitarComp: OrbitAmp's). A link it does not
        hold never works, whatever its switch says (a state of another amp's, a host's automation), and has no arrow on
        the strip and no block on the panel. */
    bool chainHolds (params::ChainRow row) const noexcept
    {
        return ownProduct.chain == Chain::bass ? params::bassChainHolds (row) : params::guitarChainHolds (row, ownProduct.guitarComp);
    }

private:


    /** The chain's round-trip to the host: whatever the three players' models need for
        rate-matching — reported whenever any of them changes. */
    void reportLatency();

    /** Makes wire `index` long enough for a block that reports `lat` samples. Message thread. */
    void fitWire (int index, int lat);

    /** One thread for both blocks' model builds. One, because a load is twenty milliseconds and
        two blocks asking at once still finish inside a frame; a second thread would only let two
        WaveNets fight over the same cores the audio thread wants. */
    juce::ThreadPool modelPool { 1 };

    // requestCabSynth's: which request is the latest, whether the processor still stands, and the worker — declared
    // last of the three, so it is gone (its job finished) before what the job reads.
    std::atomic<int> cabSynthLatest { 0 };
    int cabSynthHeard = 0;   // the latest request whose render was loaded — the message thread's alone
    std::shared_ptr<int> cabSynthAlive = std::make_shared<int> (0);
    juce::ThreadPool cabSynthPool { 1 };

public:
    /** Everything a moved control needs done OFF the audio thread, for both captured blocks: landing
        the dial where the gain knob points, the tone knobs where their slots stand, handing the
        player's load jobs to the pool and its finished models back, retiring what nobody plays.

        The timer calls this thirty times a second. It is public because it is the plugin's only
        message-thread heartbeat, and something that is not a host — a test — has to be able to drive
        it; without a driver a knob moves a parameter and nothing ever reads it, which is exactly the
        bug this became. */
    void pumpDeviceWork();

    /** NAMES WHAT THE DEVICE PARAMETERS POINT AT, AT ONCE — for a caller on the message thread that has
        just moved one and needs the state coherent before it returns, not a tick later.

        A device is a number and a name, and the pump writes the name on the tick AFTER the number
        moves. For a hand on a selector that gap is harmless: the move and its name settle into one
        undo step. For a caller writing under a suppression scope it is not. The scope closes on a
        baseline that holds the new number beside the OLD name; the name then lands outside it as an
        edit of its own; and undoing that edit hands the aim the old name, which it honours by loading
        the device the caller had just left. `rescanDevices` keeps its numbers and their names in one
        scope for exactly this reason — this is the same, for anyone else who moves a device. */
    void settleDeviceNames()
    {
        pumpDeviceWork();   // selects the pack each number names, which is what the names are read from

        for (size_t b = 0; b < numCaptured; ++b)
        {
            const auto* id = deviceIdOf (b);

            if (juce::approximatelyEqual (apvts.getRawParameterValue (id)->load(), lastDeviceValue[b]))
                continue;

            // What the aim does when a hand takes a device from it: the new pack's names, and the
            // aim told that this is where the device now stands.
            if (noteBlockNames (b, true))
                if (const auto* param = apvts.getParameter (id))
                    aimWroteDevice[b] = param->getValue();
        }
    }

    /** A model is BUILT off the message thread — bytes from the pack, a network parsed and warmed,
        some twenty milliseconds — and brought back to the player through the message queue. A driver
        without a message loop (a test) sets this and the jobs run inside pumpDeviceWork instead. */
    bool inlineLoads = false;

private:
    /** Reads each EQ link's parameters into its stack. Called per block from the audio thread;
        a stack only redesigns when something actually moved. */
    void updateEqSettings() noexcept;

    /** WHETHER THE AMP'S CONSOLE IS THE BASS'S STANDARD EQ (Product::eqStandard, params::EqStandard) — the one reading
        every EQ decision here goes by: its tone always ours, its MIDDLE in its span, its second bell and scalpel out,
        the console's own cuts out of the sound because the bass's are its own stage. */
    bool bassEq() const noexcept { return ownProduct.eqStandard() == params::EqStandard::bass; }

    /** The bass's own cuts as its parameters stand (params::bassHpf, bassLpf) — what its stage glides and fades
        to (core::BassCuts). Audio thread, per block. */
    core::BassCuts::Settings bassCutSettings() const noexcept;

    /** Same, for the reverb: character and mix, applied only when they move. */
    void updateReverbSettings() noexcept;

    /** Same, for the delay — including the one computation that needs the PROCESSOR: the sync
        time in milliseconds, from the host's tempo when it conducts and the BPM field when it
        does not. */
    void updateDelaySettings() noexcept;

    float editorScale = preferredScale;

    std::array<core::EqLink, params::numEqLinks> eqLinks;
    felitronics::delay::TapeDelay  delay;
    felitronics::reverb::AmpReverb reverb;
    // A prepare either stage refuses (no channel to carry, more than two) leaves it out of the sound:
    // the dry passes as if the link were out of the rig, rather than a stage nobody built being run.
    bool delayReady = false, reverbReady = false;

public:
    /** WHETHER THE AMP'S BOOST IS ON, the one its level (params::boostLevel) is heard with: the playing
        pack's own Boost switch past its first position — OrbitAmp's BOOST key, the rovers' TURBO, which
        holds it there — or, on a pack with no such switch, params::preampBoost — OrbitAmp's BOOST key and
        the cockpit's TURBO there. Never while the AMP is out of the chain or switched off: the boost is
        the amp's. Any thread — the audio thread asks it every
        block; which slot the pack's Boost is in is found by the pump, where the pack is. */
    bool boostOn() const noexcept;

    /** The delay's picture taps, read-only: the face draws its comb from the same numbers the
        heads are actually standing on. */
    const felitronics::delay::TapeDelay& delayTaps() const noexcept { return delay; }

    /** The tempo the host reported at the last block, or 0 when it reports none — the standalone,
        or a host that does not say. For the delay's face, which shows the number that conducts. */
    float hostTempoBpm() const noexcept { return hostTempo.load (std::memory_order_relaxed); }

    /** Whether a host could conduct at all — false in the standalone, where the choice between the
        host's tempo and the block's own does not exist. */
    bool runsInHost() const noexcept { return wrapperType != wrapperType_Standalone; }

    /** A PROGRAM CHANGE THAT CAME IN, for a face that plays them. The engine has no programs of its
        own: it keeps the last one a MIDI input sent (0..127, on any channel) until a face that gives
        programs a meaning takes it — the cockpit, whose first four are its channels. -1 when none has
        come since the last call. */
    int takeProgramChange() noexcept { return midiProgram.exchange (-1, std::memory_order_relaxed); }

    /** ...AND A CONTROLLER'S VALUE, the same way: the last one each of the 128 controllers sent
        (0..127, on any channel), kept until a face that rides something on that controller takes it.
        -1 when that controller has sent nothing since the last call. */
    int takeControlChange (int controller) noexcept
    {
        return midiControl[(size_t) (controller & 0x7f)].exchange (-1, std::memory_order_relaxed);
    }

private:
    std::atomic<int> midiProgram { -1 };
    std::array<std::atomic<int>, 128> midiControl;      // each -1 until its controller speaks

    /** The noise gate, from felitronics-core — the same engine OrbitCab ships. It keys off the
        raw input at the front of the chain; where it MUTES is the player's parameter. */
    felitronics::dynamics::NoiseGate gate;

    /** The gate's voicing, ours to edit only where a parameter exists (Decay -> closeMs). Kept
        here because the engine does not read its config back. */
    felitronics::dynamics::NoiseGate::Config gateCfg;
    float lastGateDecay = -1.0f;

    /** The hum stage, from felitronics-core — first on the input, ahead of the trim (processChunk says
        why). It takes its mode through `humWanted`, the machine's cached setting, and is handed it on
        the audio thread when it differs from `humApplied`, the one it was last given; both are the
        audio thread's alone once prepareToPlay has run. A hundred and twenty-eight harmonics, so a hum
        can be followed up to 6.4 kHz at 50 Hz; how many of them run is the tuning's (tuneHum). */
    HumStage humStage;
    const std::atomic<int>* humWanted = nullptr;
    const std::atomic<int>* humLastOn = nullptr;    // what OFF bypasses (prefs::humLastOn)
    int humApplied = -1;        // the mode the stage runs
    int humShown   = -1;        // the machine's mode, OFF included, as last published
    // OFF's crossfade: the input as it came, copied before the stage (sized in prepareToPlay), and how
    // much of the stage's output is heard, ramped a sample at a time.
    juce::AudioBuffer<float> humDry;
    // How hard the stage works — TEMPORARY, for tuning by ear (ui/HumTuning.h): the machine's three
    // cached numbers, and the ones the stage was last given.
    const std::atomic<float>* humPlayWanted  = nullptr;
    const std::atomic<float>* humQuietWanted = nullptr;
    const std::atomic<float>* humUpToWanted  = nullptr;
    float humTunedPlay = -1.0f, humTunedQuiet = -1.0f, humTunedUpTo = -1.0f;
    bool tuneHum (felitronics::dehum::HumRemoverParams&) noexcept;
    float humMix = 1.0f, humMixStep = 1.0f;
    long long humLearnSamples = -1;    // how long the learn now running has run; -1 while none does
    float     humLearnArmedMs = -1.0f; // when in it the stage started on a model; -1 until it has
    double    humRate = 48000.0;       // the rate it is counted at — the one prepareToPlay was given
    void removeHum (juce::AudioBuffer<float>& buffer) noexcept;

public:
    /** The captured preamp: a device list, the stage playing what is chosen from it, that device's
        measured controls as filters, and the taps its pictures read. The library and the loading
        live on the message thread; the audio thread only ever meets a model that is already in
        memory. */
    core::CapturedBlock preamp   { device::DeviceLibrary::ampSlot };

    /** Re-scans the devices folder and loads whatever the device parameters point at. Message
        thread. */
    void rescanDevices();

    /** The cabinet shelf — the family's (device/FactoryShelf.h), in params::cabIrNames order, and the
        folder of it each IR lies in. The face draws the same bytes the engine convolves. A number the
        build does not carry (a face, and the shelf's `Other`) is RED GIANT's, the default's. A number of
        the other chain's (Product::playsCabIr) is no bytes and no folder, whatever the factory folder holds. */
    struct IrBytes { const char* data; int size; const char* folder; };
    static const IrBytes& cabIrBytes (const Product&, int index);

    /** WHAT THE CABINET PLAYS, as the state says it: an IR of the player's own when the state names
        one the store holds, and the factory shelf's entry under `cabIr` otherwise — which is also
        the answer for a preset whose embedded IR did not survive. Message thread only; reads the
        parameter OBJECT, so an attachment's callback asking this is not one change behind. */
    struct CabChoice
    {
        int factory = params::cabIrDefault;
        const juce::MemoryBlock* bytes = nullptr;   // the player's own IR, or null for the factory's
        juce::String key, name, from;

        bool isUser() const noexcept { return bytes != nullptr; }

        /** One string per distinct sound, for a watcher to compare. */
        juce::String identity() const { return isUser() ? "user:" + key : "factory:" + juce::String (factory); }
    };

    CabChoice cabChoice() const;

    /** A factory IR from the shelf: the player's own is let go, and the parameter takes the pick
        as one gesture. One the product does not play (Product::playsCabIr) changes nothing. Message
        thread only. */
    void chooseCabFactory (int index);

    /** An IR out of the library: its bytes are read once, here, and from then on the state names
        them by key — the file can go anywhere afterwards. False, changing nothing, for a file that
        is too big to be an IR or that does not decode as audio. Message thread only. */
    bool chooseCabFile (const juce::File& file);

    /** WHERE THE MIC STANDS at a cabinet rendered live (core/CabSynth.h): across its first speaker in radii from the cap,
        x right and y up; how far in front of it; the horn on or off. */
    struct CabSpot
    {
        float x = 0.0f, y = 0.0f, distance = 0.05f;
        bool horn = true;
        juce::String mic;   // the table's mic by name (md421, sm57, re20, d112, u87, ribbon), or empty for the cabinet's own
        float angle = 0.0f; // degrees it leans back toward the cap
        float hornDb = 0.0f; // the horn's attenuator while it is on: 0, -6 or -12 dB against the table's level
    };

    /** A CABINET OF THE BASS'S OWN, rendered with the mic at `spot` — on a worker, a tenth of a second or so — then
        played as an IR of the player's own is: its bytes kept by key, so a session or a preset carries it, named by the
        cabinet, its `from` saying where the mic stood (cabSynthOf reads it back). A later request supersedes one still
        rendering; the cabinet's own 50 ms crossfade carries the swap. Message thread. */
    /** ...and two mics as an engineer puts them up (core/CabSynth.h, renderPair), each on or off: both on, B mixed into A
        by `blend` on one clock unless `align` (on from the start, as the panel's: CabPicker::Setup), B turned over by
        `invert`; one on, that one alone — `blend` kept where it stands, and not heard. The horn is A's.

        `live`: a hand still moving a mic. The cabinet is only HEARD — loaded straight into the convolution, nothing kept,
        no key, no host told: a drag renders as often as the worker keeps up, and every position kept would stay in the
        store for the plugin's life. The hand letting go asks once more without it, and that one is kept. */
    struct CabSetup
    {
        CabSpot a, b;
        float blend = 0.5f;
        bool align = true, invert = false;
        bool aOn = true, bOn = false;
        int back = -1;   // the box's back: -1 as the table has it; 0 closed, 1 ported, 2 open (core::cabsynth::Back)
    };

    void requestCabSynth (const juce::String& cab, CabSetup setup, bool live = false);

    /** THE IR LAST LOADED INTO THE CABINET, for a picture of what plays: a rendered one live under a moving hand, or what
        the state keeps (`kept`) — a rendered cabinet, the player's own, the factory's. `serial` grows with each; the
        same one loaded again (a prepare) is no new one. Message thread. */
    struct CabParts { std::vector<float> a, b; };   // a rendered pair's two mics, each at the mix's level, 48 kHz
    struct CabHeard
    {
        std::shared_ptr<const juce::MemoryBlock> bytes;
        int serial = 0;
        bool kept = false;
        std::shared_ptr<const CabParts> parts;   // ...with both mics heard; none otherwise
    };
    CabHeard cabHeard() const { return cabHeardNow; }

    /** The rendered cabinet a choice plays, and where its mics stood — or `valid` false for any other IR. */
    struct CabSynthChoice { juce::String cab; CabSetup setup; bool valid = false; };
    static CabSynthChoice cabSynthOf (const CabChoice&);
    static CabSynthChoice cabSynthOf (const juce::String& from);   // ...as its `from` says it, wherever that is kept
    static juce::String cabSynthFrom (const juce::String& box, const CabSetup&);   // ...and the `from` a kept one is given

    /** TEMPORARY — the audition loop player. Goes with the demo strip it belongs to. */
    core::DemoPlayer demo;
    void selectDemoLoop (int index);
    int  demoLoop() const noexcept { return demoLoopIndex; }     // the one last selected; -1 before any was

    /** The raw input, kept for the tuner. The audio thread only ever writes it; listening happens
        on the processor's own message-thread pump, and every needle — the strip's miniature, the
        zoomed tuner — reads the one ear, so they can never disagree. */
    core::TunerTap tunerTap;
    core::TunerEar tunerEar;
    core::TunerStrobe tunerStrobe;   // the same pump, after the ear — only while the player reads by it
    core::TunerPoly   tunerPoly;     // ...and the polyphonic tuner, off the same tap, only while the player reads by it

    /** The gate's effective gain at the last block end, in dB — what a pressure meter shows.
        Written by the audio thread, read by the strip's thumb and the zoomed gate on their
        repaint clocks. */
    std::atomic<float> gateMeterDb { 0.0f };

    /** COMP squeezing now, in dB (0 or below) — the indicator beside its knob. 0 on every amp that has no COMP. */
    std::atomic<float> compGrDb { 0.0f };

    /** THE HUM AT THE INPUT, as the stage last heard it — written by the audio thread at the end of
        every block, read by the windows on their own clocks. `humInForce` is the mode the stage is
        IN FORCE, which is the machine's (prefs::hum) unless the stage could not be prepared for this
        input, and then it is OFF and says so. OFF is a bypass: the stage goes on running in the last
        mode that was not OFF, and only its output is set aside. The rest is the stage's own telemetry: the family AUTO's
        ear decided (50 or 60, 0 while it abstains — whatever the mode), the mains frequency it tracks
        (0 while it takes nothing off), whether that tracker has settled, and the level of the hum it
        is taking off, in dB re a full-scale sine. */
    std::atomic<int>   humInForce { (int) prefs::Hum::off };
    std::atomic<int>   humFamily  { 0 };
    std::atomic<float> humHz      { 0.0f };
    std::atomic<bool>  humLocked  { false };
    std::atomic<float> humDb      { (float) HumStage::kSilentDb };

    /** What the stage was last tuned to, as it was handed over — TEMPORARY, with the tuning — and how
        many harmonics it runs now, published every block. */
    struct HumTuned
    {
        std::atomic<float> playHz { 0.0f }, quietHz { 0.0f }, maxHz { 0.0f };
        std::atomic<int>   harmonics { 0 }, active { 0 };
    } humTuned;

    /** The hum stage's LEARN, asked for by its take (core/HumLearnTake.h) and served at the start of the
        next block — see removeHum. */
    core::HumLearnLink humLearn;

    /** AUTO'S STATE, while the stage runs AUTO — under OFF too, which bypasses it and lets it go on
        listening: 50 or 60 once it has decided, 0 while it is still listening, -1 when the stage runs
        another mode or none. What a menu's AUTO line and the faces' tag say (prefs::humLabel). */
    int humHeard() const noexcept
    {
        return humRuns.load (std::memory_order_relaxed) == (int) prefs::Hum::automatic
                   ? humFamily.load (std::memory_order_relaxed) : -1;
    }
    std::atomic<int> humRuns { -1 };    // the mode the stage runs, published per block; -1 while it runs none

    /** The output level after everything, and the two latched clip caps (the UI clears them). */
    std::atomic<float> outDb   { -90.0f };

    /** The level at every joint of the chain — the gain-staging story, readable per stage. Each
        captured block's output, indexed like blockSpectrumTap — 0 is the preamp. */
    std::array<std::atomic<float>, numCaptured> blockOutDb { -90.0f };

    std::atomic<float> cabOutDb    { -90.0f };
    std::atomic<bool>  inClip  { false };
    std::atomic<bool>  outClip { false };

    /** How hard the limiter squeezed the last block, in dB (0 = untouched) — the LIMIT badge. */
    std::atomic<float> limiterGrDb { 0.0f };

    /** The per-stage DSP load, orbitcab's grammar: each stage's wall-clock as a smoothed % of
        the block's real-time budget. Indexed by Stage; the footer badge reads these. */
    // The list itself lives with the parameters now — see params::Stage. Pulled in whole so
    // every `nsStage[stPreamp]` in this class still reads the way it always did.
    using Stage = params::Stage;
    using enum params::Stage;
    std::atomic<float> stageLoad[numStages] {};

    /** The dropout evidence: every block that BLEW its budget counts, and each stage keeps the
        worst share it has hit since somebody last looked (the breakdown panel clears them).
        A mean of 7% hides a worst of 200% — and one blown block is one audible drop. */
    std::atomic<float>    stageWorst[numStages] {};
    std::atomic<uint32_t> overruns { 0 };

    /** The load's last ~12 seconds, one column per ~33 ms holding the WORST total share inside
        it — a strip chart the panel draws. Peaks survive here; an EMA would eat them. */
    static constexpr int loadHistSize = 360;
    std::atomic<float> loadHist[loadHistSize] {};
    std::atomic<int>   loadHistPos { 0 };

    /** Latched: the limiter has worked since somebody last looked (the badge clears it). */
    std::atomic<bool> limiterWorked { false };

    /** Latched: the gate pressed a signal that was ABOVE its own threshold — it ate a live
        note, not a pause. Closing on silence is the job; this is the accident worth a light. */
    std::atomic<bool> gateWorked { false };

    /** One tap per captured block, at its output — which, now that each block owns its EQ, is the
        EQ's output too. The curve and the spectrum drawn under it therefore describe the same
        point, and there is one FFT per block rather than two at the same place. Lock-free SPSC with
        a starve-tolerant reader, so several views may sip from it. */
    std::array<felitronics::analysis::RollingSpectrumTap, numCaptured> blockSpectrumTap;

    /** The pair's other half: what the EQ eats — the capture's voice before the console colours
        it. With the output tap above, an EQ pane shows before and after, the way the cabinet's
        picture does. */
    std::array<felitronics::analysis::RollingSpectrumTap, numCaptured> blockInSpectrumTap;

    /** The cabinet's own pair — what goes into the IR and what leaves it, for the picture's faint
        spectra. Pushed around the convolution, channel 0, only while the cabinet is on. */
    std::array<felitronics::analysis::RollingSpectrumTap, 2> cabSpectrumTap;

    /** The reverb's pair: the door, and what the room ADDS — the wet alone, post its own HPF, at
        the mix. Channel 0, only while the reverb is on; the block draws them as its picture. */
    std::array<felitronics::analysis::RollingSpectrumTap, 2> reverbSpectrumTap;

    /** One analysis resolution for every consumer of the taps: mixed orders would make the tap
        force-republish on every alternating pull. */
    static constexpr int eqSpectrumOrder = 11;   // 2048

    /** ...and the frame a block's tap publishes while its TONE picture is THROWN OPEN.

        The tile's 2048 points are 23 Hz a bin: between 20 and 100 Hz that is three and a half bins,
        which is why the bottom of the spectrum reads as mush no matter how many pixels it is given.
        The big view asks for the longest frame the tap can give — 16384 — and reads it through the
        constant-Q pane, which feeds several window lengths from that one frame and reports in bands
        of a twenty-fourth of an octave: fifty-six of them across the same two decades.

        Not everywhere, because it is not free. A constant-Q reading costs about fifteen times the
        classic one, and the face runs six to eight panes at once between the two consoles, the
        cabinet and the reverb — so it is spent where there is ONE pane and somebody looking at it.

        No second tap: `RollingSpectrumTap` decouples cadence from window on purpose, so one ring
        serves any order and the switch is click-free — it force-publishes at the new size and the
        reader discards frames of the wrong one. What it does NOT do is serve two orders at once,
        and it need not: the console stops pulling when a picture is thrown open over it. */
    static constexpr int eqSpectrumOrderBig = 14;   // 16384 — the constant-Q pane's longest tier

    /** What resolution each block's pair is publishing at, written by that block's face. */
    std::array<std::atomic<int>, numCaptured> blockSpectrumOrder;

    /** The raw input's peak this block, in dB, over the channels the chain works — the level the
        gate KEYS off, for the meter the thresholds are drawn on. Same writer, same readers. */
    std::atomic<float> gateKeyDb { -90.0f };

    /** WHAT A WORLD HEARS OF THE GUITAR (worlds/Ear.h, worlds/Play.h): the same peak, linear, and the pick — how far
        the sound's top jumped — each the largest since the face last took them, so a frame sees the stroke that
        came between two of them and not only the last block's. A frame that came before any block since the last
        one gets that last block's peak and no jump. One taker: the face's view. */
    worlds::Ear::Heard takePlay() noexcept
    {
        const float p = playPeak.exchange (-1.0f, std::memory_order_relaxed);
        const float k = playPick.exchange (-1.0f, std::memory_order_relaxed);
        return { p >= 0.0f ? p : juce::Decibels::decibelsToGain (gateKeyDb.load (std::memory_order_relaxed), -90.0f),
                 k >= 0.0f ? k : 1.0f };
    }

    /** Whether the view is lit by the guitar at all (prefs::litCache): off, the ear does not listen and the
        face's view hears nothing. */
    bool viewLit() const noexcept { return litWanted != nullptr && litWanted->load (std::memory_order_relaxed); }

private:
    const std::atomic<bool>* litWanted = nullptr;                   // prefs::litCache, taken where the hum's are
    int demoLoopIndex = -1;     // which of params::demoLoops the audition player holds
    std::atomic<float> playPeak { -1.0f }, playPick { -1.0f };     // takePlay's: -1 is nothing since it was last taken
    worlds::Ear ear;                                                // the audio thread's: the pick, on channel 0

    // Cached atomic parameter pointers — getRawParameterValue does a map lookup, which the audio
    // thread should not be doing per block.
    struct EqLinkParams
    {
        std::atomic<float>* hpfOn    = nullptr;
        std::atomic<float>* hpfHz    = nullptr;
        std::atomic<float>* hpfSlope = nullptr;
        std::atomic<float>* loDb     = nullptr;
        std::atomic<float>* loHz     = nullptr;
        std::atomic<float>* hiDb     = nullptr;
        std::atomic<float>* hiHz     = nullptr;
        std::atomic<float>* lpfOn    = nullptr;
        std::atomic<float>* lpfHz    = nullptr;
        std::atomic<float>* lpfSlope = nullptr;
        std::atomic<float>* eqMode   = nullptr;   // the block's DEVICE TONE / UNIVERSAL EQ, looked up once, not per block
        std::atomic<float>* b3On     = nullptr;
        std::atomic<float>* bellDb[3] {};
        std::atomic<float>* bellHz[3] {};
        std::atomic<float>* bellQ[3]  {};
    };

    std::array<EqLinkParams, params::numEqLinks> eqParams;

    std::atomic<float>* inTrimParam        = nullptr;
    std::atomic<float>* outTrimParam       = nullptr;
    std::atomic<float>* tunerMuteParam     = nullptr;
    float lastMuteGain = 1.0f;   // the tuner mute's ramp, per block
    std::atomic<float>* stereoModeParam    = nullptr;
    float histWorst   = 0.0f;
    int   histSamples = 0;
    std::atomic<float>* limiterCeilParam   = nullptr;
    float lastOutGain = 1.0f;

    // THE BOOST'S LEVEL (params::boostLevel), ahead of the master's hand: unity while the boost is off,
    // its level while it is on, and between the two a straight ramp of boostRampSeconds — not the
    // block-long ramp of the trims, whose length is whatever the host's block is: a boost thrown on a
    // block of 32 samples would step in under a millisecond. Seeded at prepare, so a session saved with
    // its boost on opens at its level instead of rising into it.
    std::atomic<float>* boostLevelParam = nullptr;
    std::atomic<float>* preampBoostParam = nullptr;
    std::array<std::atomic<float>*, (size_t) params::numSelectors> preampSelectorParams {};
    std::atomic<int> boostSlot { -1 };          // the playing pack's Boost among the preamp's selector slots; -1 for none
    juce::LinearSmoothedValue<float> boostGain { 1.0f };
    static constexpr double boostRampSeconds = 0.015;
    float boostTarget() const noexcept;         // what boostGain heads for: the level, or unity
    // THE SAFETY AT THE DOOR (core/DoorLimiter.h): a true-peak limiter that sees each peak a
    // millisecond before it arrives. It always runs, so its state is warm when it is switched in; while
    // it is out, `limiterLate` hands on the input exactly as late as the limiter would have, so the
    // chain is equally late either way and a switch is a crossfade between two signals that line up.
    felitronics::limiter::TruePeakLimiter limiter;
    core::LateWire limiterLate;
    bool limiterReady = false;
    std::atomic<int> limiterLatency { 0 };
    float limiterCeilingSet = 1.0e9f;      // the ceiling last handed to it, dB

    core::CabinetIr cab;

    /** THE BASS'S OWN LINKS (Chain::bass) — prepared and run on its chain alone: its COMP after the gate, the
        DI's copy beside the cabinet, its own cuts after DI/CAB. OrbitAmp's COMP is the same (Product::guitarComp), and
        in STEREO, where its chain works two signals, a second one squeezes the right (`compRight`, unwatched). */
    core::BassComp bassComp, compRight;
    bool compRightRan = false;   // the audio thread's: the twin worked the last block
    core::BassCuts bassCuts;

    /** THE GUITAR'S INPUT CUT (params::guitarInputCutHz) — every guitar amp's, always on. */
    core::BassCuts guitarInputCut;

    /** THE BASS'S HPF ACROSS THE CHAIN (the owner, 31.10): besides the main cut at the END (bassCuts), gentle ones —
        2nd order, an octave under the chosen frequency — at the input (before the gate and the keys), after COMP, after
        OCTAVE and after the AMP (after BLEND's sum: both legs the same phase). They clear DC, sub thumps and the subs a
        nonlinear stage makes before the next one hears them, and cost the lowest string a fraction of a decibel. The
        one switch and the one choice drive them all, each gliding and fading as the main cut does. Mono, as the front
        half is. */
    std::array<core::BassCuts, 4> innerCuts;
    core::BassCuts::Settings innerCutSettings() const noexcept;
    juce::AudioBuffer<float> diDry;
    float diNow  = 0.3f;     // DI/CAB's share as the audio thread has glided it — see processChunk
    float diStep = 0.001f;   // ...and its glide per sample, 20 ms for the whole travel at the prepared rate

    /** OrbitBass's own parameters (params::addBassParameters), read where it has them; null on every guitar
        amp, which has none — but OrbitAmp's COMP's (`comp*`, params::addGuitarCompParameters). */
    struct BassParams
    {
        std::atomic<float>* compAmount = nullptr;
        std::atomic<float>* compAttack = nullptr;
        std::atomic<float>* compRelease = nullptr;
        std::atomic<float>* compCharacter = nullptr;
        std::atomic<float>* blend = nullptr;
        std::atomic<float>* blendHz = nullptr;
        std::atomic<float>* blendInvert = nullptr;
        std::atomic<float>* cabMix = nullptr;
        std::atomic<float>* hpf = nullptr;
        std::atomic<float>* hpfHz4 = nullptr;
        std::atomic<float>* hpfHz5 = nullptr;
        std::atomic<float>* lpf = nullptr;
        std::atomic<float>* lpfHzFingers = nullptr;
        std::atomic<float>* lpfHzSlap = nullptr;
        std::atomic<float>* octave = nullptr;
        std::atomic<float>* octaveToneHz = nullptr;
        std::atomic<float>* filter = nullptr;
        std::atomic<float>* filterMode = nullptr;
        std::atomic<float>* filterDirection = nullptr;
        std::atomic<float>* filterResonance = nullptr;
        std::atomic<float>* filterRange = nullptr;
        std::atomic<float>* filterKeepLows = nullptr;
        std::atomic<float>* filterKeepHz = nullptr;
        std::atomic<float>* filterReleaseMs = nullptr;
        std::atomic<float>* chorus = nullptr;
        std::atomic<float>* chorusRateHz = nullptr;
        std::atomic<float>* chorusCrossoverHz = nullptr;
        std::atomic<float>* compRatio = nullptr;
        std::atomic<float>* compThreshold = nullptr;
        std::atomic<float>* hpfOn = nullptr;
        std::atomic<float>* lpfOn = nullptr;
    } bass;

    /** THE BASS'S THREE EFFECTS (felitronics-core's octave, envfilter, chorus), on its chain alone: each fed its slider
        through the module's own law (paramsForAmount) over the menu's fields, and told only when the result moved.
        A link standing by is a slider at 0 — the module's own counted ramp takes it to idle, and a wake is its own
        10 ms door. All three are zero-latency. Audio thread. */
    felitronics::octave::Octaver          octaver;
    felitronics::envfilter::EnvelopeFilter envFilter;
    felitronics::chorus::BassChorus       chorus;
    bool octaverReady = false, envFilterReady = false, chorusReady = false;
    /** OCTAVE's input kept while its picture watches: the stage adds its sub in place (runOctave). */
    std::array<float, 256> octaveDry {};
    felitronics::octave::OctaverParams           octaveTold {};
    felitronics::envfilter::EnvelopeFilterParams filterTold {};
    felitronics::chorus::BassChorusParams        chorusTold {};
    bool octaveToldValid = false, filterToldValid = false, chorusToldValid = false;
    bool filterTapLive = false;   // the audio thread's: whether it wrote FILTER's tap last block (a fresh look wakes it)
    std::atomic<bool> octaveWorks { false };   // octaveLive, for the picture (octaveAtWork)
    bool octaveLive = false, filterLive = false, chorusLive = false;   // the audio thread's: each at work — in the chain,
                                                                       // switched on, its slider up — its picture written only then
    std::array<int, 3> effectTold {};     // setParams calls, per effect — for a gate (effectTells)
    std::atomic<bool> effectWorking[3] {};  // FILTER's and CHORUS's "not idle", for their lamps (OCTAVE's is the module's)

    /** The bass's back half's width — the prepared bus's, 1 or 2 — and the scratch bus a narrower call is played on
        (processBlock); 0 on a guitar amp's chain. */
    std::atomic<int> bassWide { 0 };
    juce::AudioBuffer<float> narrowBus;
    juce::AudioBuffer<float> filterKey;   // FILTER's key: the gated input before COMP, the string itself

    /** BLEND's three to the player (CapturedBlock::setCrossover), under `blendControl`: the two control-side callers —
        the message thread's pump and a prepare on a host's thread — never write it at once. Never the audio thread. */
    void tellBlend();
    std::mutex blendControl;

    /** Each effect told its knobs — the module's law over the menu, the menu being what the stage was last told with the
        menu's fields from their parameters — when they moved, and only then. */
    void tellOctave() noexcept;
    void tellFilter() noexcept;
    void tellChorus() noexcept;
    void runOctave (float* const* channels, int numChannels, int numSamples) noexcept;
    void runFilter (float* const* channels, int numChannels, int numSamples) noexcept;
    void runChorus (float* const* channels, int numChannels, int numSamples) noexcept;

    /** THE BASS'S DELAY OR REVERB (Chain::bass), after its CHORUS, on the back half's planes: the stage run on a copy
        of what it is handed, what it ADDED cut below params::bassWetCutHz and put back over the dry — the repeats and
        the tail lose their lows, the bass keeps them. Out of the strip, a stage's whole contribution fades out over the
        bypass fade, and then — in the call the fade ends in — the stage is cleared and not run at all. Its picture is
        written while `watched` and at work (switched on, `adds`: its MIX up). No channel (null, 0) is a gap: the stage
        takes it as its own (law 11c). */
    template <typename Stage>
    void runSpaceStage (Stage&, bool ready, params::ChainRow, int which, core::SpaceTap&, const std::atomic<bool>& watched,
                        bool adds, float* const* channels, int numChannels, int numSamples) noexcept;
    bool delayAdds() const noexcept  { return delayMixParam != nullptr && delayMixParam->load() > 0.0f; }
    bool reverbAdds() const noexcept { return reverbMixParam != nullptr && reverbMixParam->load() > 0.0f; }
    juce::AudioBuffer<float> spaceDry;                        // a stage's input, for what it added
    std::array<std::array<core::CutBiquad, 2>, 2> wetCuts {};  // [delay, reverb][plane]: the low cut on the wet
    std::array<bool, 2> spaceRunning {};                      // [delay, reverb]: ran last block
    std::array<bool, 2> spaceFresh { true, true };            // [delay, reverb]: cleared, its next call its first

    /** A call of positive length with no channel (law 11c), to the three effects — their shared clocks run as through
        silence — and to DELAY and REVERB, present and running. */
    void passGap (int numSamples) noexcept;
    std::atomic<float>* cabHpfOnParam = nullptr;
    std::atomic<float>* cabHpfHzParam = nullptr;
    std::atomic<float>* cabHpfSlopeParam = nullptr;
    std::atomic<float>* cabLpfOnParam = nullptr;
    std::atomic<float>* cabLpfHzParam = nullptr;
    std::atomic<float>* cabLpfSlopeParam = nullptr;
    std::atomic<float>* cabTrimOnParam = nullptr;
    std::atomic<float>* cabTrimParam = nullptr;
    std::atomic<float>* cabPhaseParam = nullptr;

    /** The sound the engine was last handed (`CabChoice::identity`), and whether a fresh prepare
        wants it handed again. The flag is set from `prepareToPlay`, which is not the pump's thread. */
    juce::String lastCabIr;

    /** Where the pump last saw `cab_ir`, and whether that sighting is out of date because the whole
        state was just replaced — see the automation rule in pumpDeviceWork. */
    int  lastCabIrParam = -1;
    bool cabIrParamBaselineStale = true;
    std::atomic<bool> cabIrStale { true };

    /** The player's own IR the message thread last found the state naming, or null — for a prepare
        that runs off that thread and cannot read the tree. */
    std::atomic<const juce::MemoryBlock*> cabUserIr { nullptr };

    /** The player's own IRs a state refers to — see `device::EmbeddedIrs`. */
    device::EmbeddedIrs cabIrEmbeds;
    CabHeard cabHeardNow;          // cabHeard's
    juce::String cabHeardIdentity; // ...and which IR it is, so one loaded again is not heard anew
    void heardCab (std::shared_ptr<const juce::MemoryBlock>, const juce::String& identity, bool kept,
                   std::shared_ptr<const CabParts> parts = {});
    juce::String cabKeptPartsKey;                      // the kept render's key, and its parts — for the pump to hand on
    std::shared_ptr<const CabParts> cabKeptParts;      // with the IR when it loads it

    /** Adds to a tree bound for disk every player's-own IR any state inside it names — the live
        one, and each register's. */
    void embedCabIrs (juce::ValueTree& tree) const;

    std::atomic<float>* preampSmoothParam = nullptr;

    float lastTrimGain = 1.0f;

    /** Every link's switch, straight off `params::chainLinks`; null for a link that has none.
        Filled once in prepare; read on the audio thread like every other parameter. */
    std::atomic<float>* rowOn[params::numChainRows] { };
    std::atomic<float>* rowPresent[params::numChainRows] { };   // ChainLink::presentParam, where the product has it

    std::atomic<float>* gateThresholdParam = nullptr;
    std::atomic<float>* gatePosParam       = nullptr;
    std::atomic<float>* gateDecayParam     = nullptr;

    std::atomic<float>* delaySyncParam    = nullptr;
    std::atomic<float>* delayTimeMsParam  = nullptr;
    std::atomic<float>* delayDivParam     = nullptr;
    std::atomic<float>* delayBpmParam     = nullptr;
    std::atomic<float>* delayHostTempoParam = nullptr;
    std::atomic<float>  hostTempo { 0.0f };   // see hostTempoBpm()
    std::atomic<float>* delayRepeatsParam = nullptr;
    std::atomic<float>* delayDarkParam    = nullptr;
    std::atomic<float>* delayOffsetParam  = nullptr;
    std::atomic<float>* delayMixParam     = nullptr;

    std::atomic<float>* reverbTypeParam = nullptr;
    std::atomic<float>* reverbMixParam  = nullptr;
    std::atomic<float>* reverbDecayParam    = nullptr;
    std::atomic<float>* reverbPredelayParam = nullptr;
    std::atomic<float>* reverbHpfHzParam    = nullptr;

    std::atomic<float>* packCompParam    = nullptr;
    std::atomic<float>* preampGainParam = nullptr;
    juce::AudioBuffer<float> scopeDry;   // a block's input, kept for its pictures

    /** The crossfade of every link that REPLACES the signal, and the one buffer they share to do
        it: what the block was handed, kept only while a fade is actually running. Indexed by row,
        so a link asks for its own by name. */
    core::BypassFade         blockFade[params::numChainRows];
    juce::AudioBuffer<float> fadeDry;

    /** One per captured block: the delay a BYPASSED block still has to carry, so the chain does
        not arrive early the moment somebody stands a rate-matching model down. Only runs when the
        block is not working — while it is working the model carries its own latency and there is
        nothing to imitate. */
    core::BypassWire wire[numCaptured];

    /** Set once `reportLatency` has seen a wire refuse and has said so. Message thread only. */
    bool wireRefusalSeen = false;

public:
    /** Has a bypass wire ever been asked for more delay than it carries — which is a capture
        recorded below `BypassWire::lowestPackRate` at this session rate, and a bypass path that
        will comb. Latched; a `prepareToPlay` clears the wires and this follows on the next pump. */
    bool bypassWireRefused() const noexcept
    {
        for (const auto& w : wire)
            if (w.everShortened())
                return true;

        return false;
    }

    /** How long wire `index` currently is, in samples. Sized from the delay its block reports and
        from nothing else — no ceiling is computed anywhere — so this reads back as that delay. */
    int bypassWireCapacity (int index) const noexcept
    {
        return wire[(size_t) juce::jlimit (0, (int) numCaptured - 1, index)].capacity();
    }

private:

    /** Can a crossfade actually run this block? The buffer was sized in prepare, and a host may
        hand over a bigger block than it promised — copying into it on that block would walk off
        the end of the heap. Without room we simply do not blend: the switch lands hard, which is
        a click, and a click is a great deal better than a corrupted heap. */
    bool canFade (int numSamples, int numChannels) const noexcept
    {
        return numSamples <= fadeDry.getNumSamples() && numChannels <= fadeDry.getNumChannels();
    }

public:
    /** What the footer reports: the run's own facts, not the sound's. */
    double currentSampleRate() const noexcept { return getSampleRate(); }
    float  dspLoadPercent() const noexcept    { return dspLoad.load(); }

private:
    std::atomic<float> dspLoad { 0.0f };

    /** The channel mode's default follows the ENVIRONMENT until somebody chooses: the standalone
        opens on STEREO SPACE (one guitar in, a wide room out), a plugin on a mono bus on MONO, on
        a stereo bus on STEREO. The layout declares it so, for the buses the processor is made with,
        and a host that settles other buses moves it there (processorLayoutsChanged) — never a
        prepare, which a host may hold the parameters still across. `modeAutoValue` is what the
        environment last set — while the parameter still reads that, nobody has chosen and the
        environment may set it again; one hand-move or one restored session ends it. */
    params::StereoMode modeOfEnvironment() const;
    int  enginePlace      = 0;      // where params::enginePack stood in the product's list as it was made
    bool stateWasRestored = false;
    int  modeAutoValue    = (int) params::StereoMode::mono;   // the layout's own default, set as it is made

    //==========================================================================
    // The saved session — see refreshSavedState.

    /** Builds the session's bytes from the live state. Message thread only. */
    juce::MemoryBlock buildSavedState();

    juce::CriticalSection savedLock;
    juce::MemoryBlock     savedState;              // guarded by savedLock

    // What says the saved copy is behind. The tree says so itself (a listener); the registers are
    // not in the tree, so the copy remembers which register trees it was written from — a copy
    // into a register replaces its tree, and a switch changes the active index.
    std::atomic<bool> stateDirty { true };
    int  savedActive = -1;

    /** Restores handed over but not yet applied on the message thread — see setStateInformation. */
    std::atomic<int> pendingRestores { 0 };
    std::vector<juce::ValueTree> savedRegisters;
    int  ticksSinceSaved = 0;
    static constexpr int savedThrottleTicks = 10;   // ~a third of a second at the 30 Hz pump

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { stateDirty = true; }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override            { stateDirty = true; }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override     { stateDirty = true; }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override              { stateDirty = true; }
    void valueTreeRedirected (juce::ValueTree&) override                              { stateDirty = true; }

public:
    /** The product's own half of the engine, where it has one (ProductEngine) — null for OrbitAmp.
        Declared after everything it reaches into, so that it goes before any of it does. */
    std::unique_ptr<ProductEngine> productEngine;

private:
    JUCE_DECLARE_WEAK_REFERENCEABLE (AmpProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpProcessor)
};

/** THE PLAYER'S IRS OF BEFORE, gathered into the family's folder: what the product kept in its own `IRs`
    is copied into the family's folder of its chain, `Darwin's Cat/IRs` for a guitar amp (Product::irsFolder,
    device::IrLibrary::gather) — once in a process for each product,
    and once on this machine (prefs::irsGathered), tried again on the next start only where a file could
    not be copied. Every product's first start calls it (Product::firstStart), so a gate that plays a
    product with none never reaches the machine's real folders. */
void gatherFormerIrs (const Product&);

} // namespace orbitamp
