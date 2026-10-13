// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CatpitEngine.h"

#include "../Parameters.h"
#include "../ui/Prefs.h"

namespace orbitamp::catpit
{

namespace
{
    constexpr int preampConsole = 1;        // the preamp's universal console: kept flat, the tones are the pack's

    using Suppress = felitronics::appkit::CompareHistory::ScopedSuppress;
}

int settings::snapTurbo (int db)
{
    int best = turboPushes.front();
    for (const int push : turboPushes)
        if (std::abs (push - db) < std::abs (best - db))
            best = push;
    return best;
}

int settings::turbo()
{
    return snapTurbo ((int) prefs::store().get (turboKey, turboDefault));
}

//==============================================================================
void Engine::addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    // The names the lever wears, as a host would write them.
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { channelId, 1 }, "Channel",
                                                              juce::StringArray { "Clean", "Edge", "Crunch", "Hi-Gain" }, 0));
}

Engine& Engine::of (AmpProcessor& p)
{
    jassert (dynamic_cast<Engine*> (p.product.get()) != nullptr);
    return static_cast<Engine&> (*p.product);
}

/** Built while the processor is still building itself, before its history has taken a baseline: what
    is settled here is how the amp starts, not an edit of it. */
Engine::Engine (AmpProcessor& p)
    : channels (p), amp (p), channel (p.apvts.getParameter (channelId))
{
    jassert (channel != nullptr);

    channels.setTurboDb ((float) settings::turbo());
    makeOwn();
    channels.align();
    say (channels.active());
    lastAsked = asked();
}

int Engine::asked() const
{
    return channel != nullptr ? juce::jlimit (0, Channels::count - 1, juce::roundToInt (channel->convertFrom0to1 (channel->getValue())))
                              : channels.active();
}

void Engine::say (int playing)
{
    if (channel != nullptr && asked() != playing)
        channel->setValueNotifyingHost (channel->convertTo0to1 ((float) playing));
}

bool Engine::go (int to)
{
    const bool went = channels.select (to);
    say (channels.active());
    lastAsked = asked();
    return went;
}

bool Engine::makeOwn()
{
    const bool unseen = ! channels.adopted();
    if (unseen)
        setUpChain (channels.adopt());
    return unseen;
}

void Engine::stateLanded()
{
    lastAsked = asked();
}

void Engine::pump()
{
    bool moved = false;

    if (! channels.adopted())
    {
        // A state the cockpit has not seen — a session from before it existed, a preset's INIT — with
        // no window open to take it in.
        const Suppress settling (amp.history);
        makeOwn();
        channels.align();
        say (channels.active());
        moved = true;
    }
    else if (const int want = asked(); want != lastAsked)
    {
        // The parameter moved: a host's automation, a controller the host has bound to it.
        if (want != channels.active())
        {
            go (want);
            moved = true;
        }
    }
    else if (want != channels.active())
    {
        // The state moved under it: an undo, a register, a preset whose channel is not the one the
        // parameter arrived on — a state from before there was a parameter says nothing for it at all.
        // The parameter follows the sound.
        const Suppress quiet (amp.history);
        say (channels.active());
        moved = true;
    }

    lastAsked = asked();

    if (moved && onChange)
        onChange();
}

void Engine::setParam (const juce::String& paramId, float value)
{
    if (auto* p = amp.apvts.getParameter (paramId))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void Engine::setUpChain (bool firstTime)
{
    setParam (params::tunerPresent,  1.0f);   // the tuner bay is always there...
    if (firstTime)                            // ...and listening, until the player switches it off
        setParam (params::tunerOn,   1.0f);
    setParam (params::gatePresent,   1.0f);   // the gate stands in the chain; whether it works is the settings' ON
    setParam (params::inPresent,     1.0f);   // the INPUT and OUTPUT faders are these links' trims
    setParam (params::inOn,          1.0f);
    setParam (params::outPresent,    1.0f);
    setParam (params::outOn,         1.0f);
    setParam (params::boostPresent,  0.0f);
    setParam (params::preampPresent, 1.0f);
    setParam (params::preampOn,      1.0f);
    setParam (params::delayPresent,  1.0f);
    setParam (params::delayOn,       1.0f);
    setParam (params::delaySync,      1.0f);  // the delay keeps time, always: the host's when there is one...
    setParam (params::delayHostTempo, 1.0f);  // ...and its own BPM when there is not
    setParam (params::reverbPresent, 1.0f);
    setParam (params::reverbOn,      1.0f);
    if (firstTime)                            // after that the reverb is the player's to choose
        setParam (params::reverbType, channels.defaultOf (channels.active(), params::reverbType));
    setParam (params::cabPresent,    1.0f);
    setParam (params::cabOn,         1.0f);

    // TURBO is the preamp's input trim, at its push or at nothing: a state the cockpit has never seen
    // starts with it off, wherever that trim was left.
    if (firstTime)
        setParam (params::blockIn (params::preampId), 0.0f);

    // ...and with as much delay as its channel opens on, not as much as the engine does.
    if (firstTime)
        setParam (params::delayMix, channels.defaultOf (channels.active(), params::delayMix));

    // BASS / MIDDLE / TREBLE are the pack's own tone stack. The universal console's three bands they
    // used to ride stand flat behind it: a pack with no tone of its own falls to that console, and
    // must not find a setting there that no knob on the face shows.
    setParam (params::blockEqMode (params::preampId), (float) params::eqModes.indexOf ("Device Tone"));
    setParam (params::eqLoDb (preampConsole), 0.0f);
    setParam (params::eqBellDb (preampConsole, 0), 0.0f);
    setParam (params::eqHiDb (preampConsole), 0.0f);

    // HPF / LPF are on-off: the corners and slopes stay where OrbitCab puts them until set by ear.
    setParam (params::cabHpfHz, params::cabHpfDefaultHz);
    setParam (params::cabLpfHz, params::cabLpfDefaultHz);
}

} // namespace orbitamp::catpit

namespace orbitamp
{

// The cockpit's half of the two things a product gives the processor beside its face (PluginProcessor.h).
void addProductParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    catpit::Engine::addParameters (layout);
}

std::unique_ptr<ProductEngine> makeProductEngine (AmpProcessor& p)
{
    return std::make_unique<catpit::Engine> (p);
}

} // namespace orbitamp
