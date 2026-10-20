// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "Engine.h"

#include "../Parameters.h"
#include "../ui/Prefs.h"

namespace orbitamp::channels
{

namespace
{
    constexpr int preampConsole = 0;        // the preamp's universal console: kept flat, the tones are the pack's

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

int settings::turbo (const Product& product)
{
    return snapTurbo ((int) prefs::store (product).get (turboKey, turboDefault));
}

//==============================================================================
void Engine::addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout, const Lineup& lineup)
{
    // The names the channels wear, as a host would write them.
    juce::StringArray names;
    for (const auto& c : lineup.channels)
        names.add (c.name);
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { lineup.parameterId, 1 }, "Channel", names, 0));

    // ...and PARK, where the product has it: off, the amp plays.
    if (lineup.parkId != nullptr)
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { lineup.parkId, 1 }, "Park", false));
}

void Engine::opening (const Lineup& lineup, params::Opening& o)
{
    // A fresh instance opens on the first channel (Channels::adopt) and setUpChain gives it that
    // channel's room and delay (Channels::defaultOf) — said here before there is an instance to say it to.
    jassert (! lineup.channels.empty());
    if (lineup.channels.empty())
        return;

    const auto& first = lineup.channels.front();
    o.capture  = first.capture;
    o.reverb   = first.reverb;
    o.delayMix = first.delayMix;
    if (first.reverbMix.has_value())
        o.reverbMix = *first.reverbMix;
}

Engine& Engine::of (AmpProcessor& p)
{
    jassert (dynamic_cast<Engine*> (p.productEngine.get()) != nullptr);
    return static_cast<Engine&> (*p.productEngine);

}

/** Built while the processor is still building itself, before its history has taken a baseline: what
    is settled here is how the amp starts, not an edit of it. */
Engine::Engine (AmpProcessor& p, const Lineup& lineup)
    : channels (p, lineup), amp (p), channel (p.apvts.getParameter (lineup.parameterId)),
      park (lineup.parkId != nullptr ? p.apvts.getRawParameterValue (lineup.parkId) : nullptr)
{
    jassert (channel != nullptr);

    channels.setTurboDb ((float) settings::turbo (p.product()));
    makeOwn();
    channels.align();
    say (channels.active());
    lastAsked = asked();
}

int Engine::asked() const
{
    return channel != nullptr ? juce::jlimit (0, channels.count() - 1, juce::roundToInt (channel->convertFrom0to1 (channel->getValue())))
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
        // A state the product has not seen — a session from before it existed, a preset's INIT — with
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
    setParam (params::preampPresent, 1.0f);
    setParam (params::preampOn,      1.0f);
    setParam (params::delayPresent,  1.0f);
    setParam (params::delayOn,       1.0f);
    setParam (params::delaySync,      1.0f);  // the delay keeps time, always: the host's when there is one...
    setParam (params::delayHostTempo, 1.0f);  // ...and its own BPM when there is not
    setParam (params::reverbPresent, 1.0f);
    setParam (params::reverbOn,      1.0f);
    if (firstTime)                            // after that the reverb is the player's to choose
    {
        setParam (params::reverbType, channels.defaultOf (channels.active(), params::reverbType));
        if (channels.lineup().channels[(size_t) channels.active()].reverbMix.has_value())
            setParam (params::reverbMix, channels.defaultOf (channels.active(), params::reverbMix));
    }
    setParam (params::cabPresent,    1.0f);
    if (firstTime)                            // after that, whether the cabinet plays is the player's to say
        setParam (params::cabOn,     1.0f);

    // TURBO is the preamp's input trim, at its push or at nothing: a state the product has never seen
    // starts with it off, wherever that trim was left.
    if (firstTime)
        setParam (params::blockIn (params::preampId), 0.0f);

    // ...and with as much delay as its channel opens on, not as much as the engine does — and its pack's
    // switches where the pack opens them, the one the channel holds where it holds it.
    if (firstTime)
    {
        setParam (params::delayMix, channels.defaultOf (channels.active(), params::delayMix));
        for (int s = 0; s < params::numSelectors; ++s)
            setParam (params::selectorId (params::preampId, s),
                      channels.defaultOf (channels.active(), params::selectorId (params::preampId, s)));
    }

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

} // namespace orbitamp::channels
