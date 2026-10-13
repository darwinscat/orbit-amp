// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "CatpitChannels.h"

#include <array>
#include <functional>

namespace orbitamp::catpit
{

/** TURBO's push is the machine's to say (CatpitEditor.h) — and the engine's to know before any window
    has opened, since a channel comes back at the push in force. */
namespace settings
{
inline const juce::Identifier turboKey { "turbo_db" };
inline constexpr int turboDefault = 9;
inline constexpr std::array<int, 4> turboPushes { 3, 6, 9, 12 };

/** The push on offer nearest to `db`. */
int snapTurbo (int db);

/** The push the settings hold now. */
int turbo();
} // namespace settings

/** THE COCKPIT'S HALF OF THE ENGINE: what makes the processor a four-channel amp whether or not its
    window is open. A plug-in's window is open for minutes and shut for hours, and what a host can
    reach is the processor's parameters alone — so the channels live here, under one parameter of the
    cockpit's own, CHANNEL, and the window is one more hand on it.

    WHO MOVES A CHANNEL. A hand — the lever, the keys 1 2 3 4, a Program Change — calls go(), and the
    switch is made there and then: one tick, one undo step, as it always was. A HOST moves the
    parameter, from whatever thread it likes; pump() sees that on the processor's own timer and makes
    the same switch on the message thread. Either way the parameter and the state end up saying the
    same channel.

    The parameter is the REQUEST, the state is the FACT (Channels::active): where they differ, which
    of the two moved says what to do. The parameter moved — somebody asked for a channel. The state
    moved under a parameter that did not — an undo, a register or a preset brought its channel with
    it — and the parameter is put where the sound is, which is not an edit.

    A STATE THAT LANDS MOVES THE PARAMETER TOO, and that is not somebody asking: one saved before the
    parameter existed has no word for it, and the parameter falls to its default as the state arrives.
    So the processor says when a state has landed (stateLanded), and where the parameter stands at
    that moment is taken as where it stood — only a move after it is a request.

    Message thread only. */
class Engine final : public ProductEngine
{
public:
    explicit Engine (AmpProcessor&);

    /** The cockpit's half of this processor. Every processor a cockpit build makes has one. */
    static Engine& of (AmpProcessor&);

    /** CHANNEL, the host's handle on the lever. The cockpit's own: OrbitAmp has no such parameter. */
    static constexpr const char* channelId = "catpit_channel";
    static void addParameters (juce::AudioProcessorValueTreeState::ParameterLayout&);

    Channels channels;

    /** To that channel, now. False, and the sound left where it was, when its pack is not installed —
        and the parameter is put back on the channel that is playing, which is what sends the lever
        back and tells a host its request was not met. */
    bool go (int channel);

    /** THE FIXED CHAIN. The cockpit is a cut-down OrbitAmp: the links it has are always in the rig and
        always on, the ones it has not are out, and its tone knobs are the pack's own. `firstTime`
        also gives a state the cockpit has never seen the cockpit's starting choices. */
    void setUpChain (bool firstTime);

    /** Makes a state the cockpit has not seen its own — the chain, the channel whose pack is playing,
        a cabinet CAB can reach. True when it was not. Writes; the caller says whether that is an edit. */
    bool makeOwn();

    void pump() override;
    void stateLanded() override;

    /** The face, when it is open: the engine has moved a channel or taken a state as its own, and
        what the window shows may be behind. */
    std::function<void()> onChange;

private:
    AmpProcessor& amp;
    juce::RangedAudioParameter* const channel;
    int lastAsked = 0;      // where the parameter stood when it was last looked at

    int  asked() const;
    void say (int playing);
    void setParam (const juce::String& paramId, float value);
};

} // namespace orbitamp::catpit
