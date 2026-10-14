// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Lineup.h"
#include "../PluginProcessor.h"

#include <array>
#include <optional>
#include <vector>

namespace orbitamp::channels
{

/** THE CHANNELS, AND WHAT EACH REMEMBERS. To the hand a product's channels are so many amps — the
    cockpit's throttle has four detents, the rover six packs: leaving one leaves its DRIVE, its tone,
    its keys, TURBO, the reverb, the delay and the cabinet as they stood, and coming back finds them
    there. How many there are and what each plays is the product's to say (Lineup.h).

    The engine has ONE set of those parameters, and whichever channel is playing owns it. The others
    wait as records in the processor's state tree, under one node of the product's own. Being in
    that tree is the whole design: the state is what a session saves, what undo steps through and what
    each of the A/B/C/D registers holds — so the memory travels with all three, and none of them had to
    be taught about it. The record of the channel that is PLAYING is stale by definition; the live
    parameters are its truth until it is left.

    The same node carries the controls that have no parameter yet: the pack's four layers are
    remembered here, and will reach the sound when the packs carry them (layerPlayable).

    No widgets in here, and nothing the audio thread reads. Message thread only. It belongs to the
    product's half of the engine (Engine.h), so it is there with the window shut. */
class Channels
{
public:
    Channels (AmpProcessor&, const Lineup&);

    const Lineup& lineup() const noexcept { return line; }
    int count() const noexcept { return line.count(); }

    /** WHAT A CHANNEL OPENS ON, until the player sets it otherwise — a channel never pulled, and every
        one of them after INIT: its own room and its own share of delay, as the lineup gives them.
        Everything else a channel keeps opens on its parameter's own default. */
    float defaultOf (int channel, const juce::String& paramId) const;

    /** BASS, MIDDLE, TREBLE are the playing pack's own tone controls, by their place in it: the
        products' packs are made with those three first, in that order. */
    static constexpr int numTones = 3;
    static juce::String toneId (int knob) { return params::preampMeasured (knob); }

    /** The controls kept in the node rather than in a parameter. */
    enum Flag { fat, bright, depth, presence, numFlags };

    /** WHICH LAYERS THE PACKS CARRY — none of them yet, so a hand on a layer's key would change nothing
        it could hear. Until a version of the packs brings a layer, the face keeps its key where it
        stands and does not let it be thrown; what a state already says about it is left alone, only not
        shown. One word each, in Flag's order, turns a key back on. */
    static constexpr std::array<bool, numFlags> layerPlayable {{
        false,      // FAT
        false,      // BRIGHT
        false,      // DEPTH
        false,      // PRESENCE
    }};

    /** TURBO IS A PUSH INTO THE CAPTURE: the preamp's own input trim (params::blockIn), at its push or
        at none — a clean boost in front of the amp, the way a pedal is one. It is a parameter, so a
        channel keeps it with everything else it remembers. How HARD the push is, though, is the
        machine's to say (the settings): a channel remembers that TURBO is on, and comes back at the
        push in force. */
    void  setTurboDb (float db) noexcept { turboPush = db; }
    float turboDb() const noexcept { return turboPush; }

    /** The trim is a parameter and a host may leave it anywhere: past this it reads as TURBO on. It is
        under every push on offer. */
    static constexpr float turboOnAbove = 1.5f;

    /** A note on the state itself, beside INPUT's trim: the lever's zero that trim was dialled against
        (the face's INPUT lever). It is INPUT's, and goes where INPUT goes: no preset holds either of
        them. */
    juce::Identifier leverZeroId() const { return line.leverZeroId; }

    /** Whether the state carries the product's node at all. One that does not is a state the product has
        never seen — a session saved before it existed, or a fresh instance. */
    bool adopted() const { return node().isValid(); }

    /** Makes the state the product's, if it is not yet: the channel is the one whose pack a hand has set
        playing, the first otherwise — and the first for a fresh instance or an INIT, whose device is
        the engine's starting one. True when there was nothing of ours in it before. */
    bool adopt();

    /** The channel the state says is playing. */
    int  active() const;

    /** Leaves the playing channel as it stands, switches to `channel`'s pack and puts back what that
        channel was left with — its defaults, the first time it is visited. False, and nothing touched,
        when that channel's pack is not installed: a channel that cannot be played is not claimed. */
    bool select (int channel);

    bool flag (Flag) const;
    void setFlag (Flag, bool);

    /** The pack a channel plays, as an index into the preamp's library; -1 when it is not installed.
        A channel's pack is the CAPTURE it plays, known by the `rig_id` in its manifest and not by what
        its file is called, wherever the device library finds it — among the packs the build carries
        first, as the library itself orders them. A file renamed is the same channel's pack still. */
    int  packFor (int channel) const;

    /** THE PRODUCT'S OWN CABINETS: every IR file in its two places — the ones the build carries, laid
        out in the family's factory folder under the product's own list (Set.h; another product's
        cabinets beside them are not among them), and the player's IRs folder beside its Devices —
        in the order of their names, so a leading number is how the order is chosen. Read when the
        engine is built and again each time the face opens: the folder is the player's to add to
        between two looks. CAB has as many positions as there are of them. */
    const juce::Array<juce::File>& cabinets() const noexcept { return irs; }
    void readCabinets();

    /** How many of cabinets() are the build's own. They stand first and the player's after them: what
        a player adds never lands among the factory ones, whatever it is called. */
    int factoryCabinets() const noexcept { return factory; }

    /** THE CABINET, OUT OF THE SOUND ALTOGETHER — for an amp played into a real one, or into an IR
        loaded somewhere else. One switch for every channel, and the sound's own: a session and a
        preset keep it, undo steps over it, INIT throws it back on. Off, each channel still remembers
        its cabinet. */
    bool cabinetOn() const;
    void setCabinetOn (bool);

    /** THE CABINET A CHANNEL OPENS ON, until the player picks another — a channel never pulled, every
        one of them after INIT, and one whose own cabinet is not here any more: the build's own of the
        name the lineup gives, found wherever it stands in the list, as an index into cabinets(). The
        first in the list where the build carries none of that name; -1 with none at all. */
    int defaultCabinet() const;

    /** TAKING FILES IN — IRs themselves, folders of them, zips — into the player's own IRs folder, the
        way OrbitAmp's library takes them, and reading the cabinets again. How many cabinets that made,
        the first of them as an index into cabinets(), and the files refused, by name: an IR too big to
        be kept inside a session cannot be a cabinet here. */
    struct Added
    {
        int cabinets = 0;
        int first = -1;
        juce::StringArray refused;
    };
    Added importCabinets (const juce::StringArray& paths);

    /** A cabinet of the player's own under another name — the file's, where it lies — or gone, to the
        Trash. Every channel that was left on a renamed cabinet goes with it. False, and nothing
        touched, for one of the build's own, a name already taken, or a file that would not move.
        Neither is an edit of the sound: the caller says so to the history. */
    bool renameCabinet (int index, const juce::String& name);
    bool removeCabinet (int index);

    /** The one that is playing, as an index into cabinets(); -1 when the sound's cabinet is none of them. */
    int  cabinet() const;
    void chooseCabinet (int index);

    /** A cabinet's name as the panel says it: the file's, without its leading number. */
    static juce::String cabinetName (const juce::File&);

    /** ...and as a file would carry it: the same, with the file's own dashes and no extension. */
    static juce::String cabinetStem (const juce::File&);

    /** True when the cabinet that is playing wears the name of one of the product's and is not that
        file: a state carries its cabinet's bytes, and one saved before the file was replaced under its
        name would go on sounding the old one, under a plate that names the new. And true the other
        way round — the file it is, under a name the file has since lost. align() cures both. */
    bool cabinetIsStale() const;

    /** Makes the sound agree with what the state says about itself: the playing channel's pack, and a
        cabinet that is one of the product's — the file of that name as it is now. The folders are the
        player's to change between sessions, and a state remembers a pack by a name and a cabinet by
        its bytes — either may be one the product no longer has. Writes; the caller decides whether
        that is an edit. */
    void align();

    /** The type of the product's node in the state tree. */
    juce::Identifier nodeType() const { return line.nodeType; }

    /** A note kept on the node beside the channels — it travels wherever they do. */
    juce::var tag (const juce::Identifier&) const;
    void setTag (const juce::Identifier&, const juce::var&);

    /** Every channel written out in full, as a tree of their own: the playing one from the live
        controls, the others from their records, a channel never visited from its defaults. Two states
        that would sound the same on every detent give the same tree — which the node itself does not,
        since the playing channel's record there is stale and an unvisited one is missing. TURBO is in
        as on or off: a channel kept under one push is the same channel under another. */
    juce::ValueTree whole() const;

private:
    AmpProcessor& amp;
    const Lineup& line;
    juce::StringArray kept;      // the parameters a channel remembers
    juce::Array<juce::File> irs; // the product's cabinets, in order
    int factory = 0;             // how many of them, from the front, are the build's own
    float turboPush = 9.0f;      // TURBO's push, dB
    float settled (const juce::String& paramId, float value) const;    // a remembered TURBO, at the push in force
    mutable std::vector<std::optional<juce::String>> irKeys;    // each file's content key, read when first asked for; empty if it would not read
    juce::String keyOfFile (int index) const;

    juce::String cabinetPlaying() const;                  // its file's name without the extension; empty when none of ours
    void nameCabinet (const juce::File&);                 // writes the playing cabinet's name down as that file's
    void chooseCabinetNamed (const juce::String& stem);   // ...or the one a channel opens on, when that one is not here

    juce::ValueTree node() const;
    juce::ValueTree ensureNode();
    void  store (juce::ValueTree record) const;
    void  recall (int channel, const juce::ValueTree& record);
    float plain (const juce::String& paramId) const;
    void  set (const juce::String& paramId, float plainValue);
};

} // namespace orbitamp::channels
