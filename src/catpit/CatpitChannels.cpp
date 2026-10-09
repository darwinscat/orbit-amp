// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CatpitChannels.h"

#include "../Parameters.h"

#include <array>

namespace orbitamp::catpit
{

namespace
{
    /** STAND-INS until the cockpit's own captures exist: four preamp packs from the library, in order
        of gain, matched by their file name without the extension (exactly — "V4 Copper" is not
        "V4 Copper TB"). */
    constexpr std::array<const char*, Channels::count> packs { "IR-X Ch1", "V4 Copper", "V4 Sheriff Ch1", "V4 Kraken Ch2" };

    constexpr std::array<const char*, Channels::numFlags> flagIds { "turbo", "fat", "bright", "depth", "presence" };

    const juce::Identifier nodeId   { "catpit" };
    const juce::Identifier recordId { "channel" };
    const juce::Identifier activeId { "active" };
    const juce::Identifier indexId  { "i" };

    constexpr int preampConsole = 1;     // the EQ link BASS / MIDDLE / TREBLE ride

    /** A channel's record in the node; made if `create` and it is not there. */
    juce::ValueTree recordOf (juce::ValueTree node, int channel, bool create)
    {
        auto record = node.getChildWithProperty (indexId, channel);
        if (! record.isValid() && create && node.isValid())
        {
            record = juce::ValueTree (recordId);
            record.setProperty (indexId, channel, nullptr);
            node.appendChild (record, nullptr);
        }
        return record;
    }
}

Channels::Channels (AmpProcessor& p) : amp (p)
{
    kept.add (params::preampGain);
    kept.add (params::eqLoDb (preampConsole));
    kept.add (params::eqBellDb (preampConsole, 0));
    kept.add (params::eqHiDb (preampConsole));
    kept.add (params::cabHpfOn);
    kept.add (params::cabLpfOn);
    kept.add (params::reverbMix);
    kept.add (params::reverbType);
    kept.add (params::delayMix);
    kept.add (params::delayDiv);        // the note value; the tempo it is counted in is the song's, not a channel's
    kept.add (params::cabIr);
}

juce::ValueTree Channels::node() const
{
    // Asked for afresh every time: an undo, a register or a session REPLACES the state tree, and a
    // handle kept across that would be a handle to the tree that was thrown away.
    return amp.apvts.state.getChildWithName (nodeId);
}

juce::ValueTree Channels::ensureNode()
{
    auto n = node();
    if (! n.isValid())
    {
        n = juce::ValueTree (nodeId);
        n.setProperty (activeId, 0, nullptr);
        amp.apvts.state.appendChild (n, nullptr);
    }
    return n;
}

int Channels::packFor (int channel) const
{
    const auto& library = amp.preamp.packs;
    for (int i = 0; i < library.size(); ++i)
        if (library.getReference (i).location.getFileName().upToFirstOccurrenceOf (".", false, false)
                .equalsIgnoreCase (packs[(size_t) juce::jlimit (0, count - 1, channel)]))
            return i;
    return -1;
}

bool Channels::adopt()
{
    if (node().isValid())
        return false;

    const int playing = juce::roundToInt (plain (params::preampDevice));
    int channel = -1;
    for (int c = 0; c < count; ++c)
        if (packFor (c) >= 0 && packFor (c) == playing)
            channel = c;

    ensureNode().setProperty (activeId, juce::jmax (0, channel), nullptr);
    if (channel < 0 && packFor (0) >= 0)
    {
        set (params::preampDevice, (float) packFor (0));
        amp.settleDeviceNames();    // the caller is settling under a suppression: the name goes in with the number
    }
    return true;
}

int Channels::active() const
{
    return juce::jlimit (0, count - 1, (int) node().getProperty (activeId, 0));
}

bool Channels::select (int to)
{
    to = juce::jlimit (0, count - 1, to);
    const int from = active();
    if (to == from)
        return true;

    const int pack = packFor (to);
    if (pack < 0)
        return false;

    auto n = ensureNode();
    store (recordOf (n, from, true));
    n.setProperty (activeId, to, nullptr);

    // The pack, then what is dialled on it, then the pack's name beside its number — all before this
    // returns, so the state is never caught half-way, and all in one tick, so the history commits it
    // as ONE step: undo goes back to the channel that was playing, exactly as it stood.
    set (params::preampDevice, (float) pack);
    recall (recordOf (n, to, false));
    amp.settleDeviceNames();
    return true;
}

bool Channels::flag (Flag f) const
{
    return (bool) node().getProperty (flagIds[(size_t) f], false);
}

void Channels::setFlag (Flag f, bool on)
{
    if (flag (f) != on)
        ensureNode().setProperty (flagIds[(size_t) f], on, nullptr);
}

void Channels::store (juce::ValueTree record) const
{
    for (const auto& id : kept)
        record.setProperty (id, plain (id), nullptr);
    for (int f = 0; f < numFlags; ++f)
        record.setProperty (flagIds[(size_t) f], flag ((Flag) f), nullptr);
}

void Channels::recall (const juce::ValueTree& record)
{
    // No record is a channel never visited: every control at its default, every layer off.
    for (const auto& id : kept)
        set (id, record.isValid() && record.hasProperty (id) ? (float) record[id] : defaultOf (id));
    for (int f = 0; f < numFlags; ++f)
        setFlag ((Flag) f, record.isValid() && (bool) record.getProperty (flagIds[(size_t) f], false));
}

float Channels::plain (const juce::String& paramId) const
{
    auto* p = amp.apvts.getParameter (paramId);
    jassert (p != nullptr);
    return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
}

float Channels::defaultOf (const juce::String& paramId) const
{
    if (paramId == params::reverbType)      // the cockpit's room is a hall, whatever the engine opens on
        return (float) juce::jmax (0, params::reverbCharacters.indexOf (defaultReverb));

    auto* p = amp.apvts.getParameter (paramId);
    return p != nullptr ? p->convertFrom0to1 (p->getDefaultValue()) : 0.0f;
}

void Channels::set (const juce::String& paramId, float value)
{
    // The cabinet has its own door: picking a factory IR also lets go of a player's own file.
    if (paramId == params::cabIr)
    {
        amp.chooseCabFactory (juce::roundToInt (value));
        return;
    }

    if (auto* p = amp.apvts.getParameter (paramId))
        if (const float to = p->convertTo0to1 (value); ! juce::approximatelyEqual (to, p->getValue()))
            p->setValueNotifyingHost (to);
}

} // namespace orbitamp::catpit
