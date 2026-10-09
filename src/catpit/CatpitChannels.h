// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../PluginProcessor.h"

namespace orbitamp::catpit
{

/** THE FOUR CHANNELS, AND WHAT EACH REMEMBERS. To the hand the throttle's four detents are four amps:
    leaving one leaves its DRIVE, its tone, its keys, TURBO, the reverb, the delay and the cabinet as
    they stood, and coming back finds them there.

    The engine has ONE set of those parameters, and whichever channel is playing owns it. The other
    three wait as records in the processor's state tree, under one node of the cockpit's own. Being in
    that tree is the whole design: the state is what a session saves, what undo steps through and what
    each of the A/B/C/D registers holds — so the memory travels with all three, and none of them had to
    be taught about it. The record of the channel that is PLAYING is stale by definition; the live
    parameters are its truth until it is left.

    The same node carries the controls that have no parameter yet: TURBO and the pack's four layers
    move and are remembered here, and will reach the sound when the packs carry them.

    No widgets in here, and nothing the audio thread reads. Message thread only. */
class Channels
{
public:
    static constexpr int count = 4;

    /** The reverb a channel opens on until the player picks another. */
    static constexpr const char* defaultReverb = "Hall";

    /** The controls kept in the node rather than in a parameter. */
    enum Flag { turbo, fat, bright, depth, presence, numFlags };

    explicit Channels (AmpProcessor&);

    /** Whether the state carries the cockpit's node at all. One that does not is a state the cockpit has
        never seen — a session saved before it existed, or a fresh instance. */
    bool adopted() const { return node().isValid(); }

    /** Makes the state the cockpit's, if it is not yet: the channel is the one whose pack is already
        playing, the first otherwise. True when there was nothing of ours in it before. */
    bool adopt();

    /** The channel the state says is playing. */
    int  active() const;

    /** Leaves the playing channel as it stands, switches to `channel`'s pack and puts back what that
        channel was left with — its defaults, the first time it is visited. False, and nothing touched,
        when that channel's pack is not installed: a channel that cannot be played is not claimed. */
    bool select (int channel);

    bool flag (Flag) const;
    void setFlag (Flag, bool);

    /** The pack a channel plays, as an index into the preamp's library; -1 when it is not installed. */
    int  packFor (int channel) const;

    /** The type of the cockpit's node in the state tree. */
    static constexpr const char* nodeType = "catpit";

    /** A note kept on the node beside the channels — it travels wherever they do. */
    juce::var tag (const juce::Identifier&) const;
    void setTag (const juce::Identifier&, const juce::var&);

    /** All four channels written out in full, as a tree of their own: the playing one from the live
        controls, the others from their records, a channel never visited from its defaults. Two states
        that would sound the same on every detent give the same tree — which the node itself does not,
        since the playing channel's record there is stale and an unvisited one is missing. */
    juce::ValueTree whole() const;

private:
    AmpProcessor& amp;
    juce::StringArray kept;      // the parameters a channel remembers

    juce::ValueTree node() const;
    juce::ValueTree ensureNode();
    void  store (juce::ValueTree record) const;
    void  recall (const juce::ValueTree& record);
    float plain (const juce::String& paramId) const;
    float defaultOf (const juce::String& paramId) const;
    void  set (const juce::String& paramId, float plainValue);
};

} // namespace orbitamp::catpit
