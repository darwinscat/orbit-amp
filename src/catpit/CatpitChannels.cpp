// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CatpitChannels.h"

#include "../Parameters.h"
#include "../device/IrLibrary.h"

#include <algorithm>
#include <array>

namespace orbitamp::catpit
{

namespace
{
    /** A channel's pack is the one whose file is named after it. */
    constexpr std::array<const char*, Channels::count> packs { "Clean", "Edge", "Crunch", "Hi-Gain" };

    constexpr std::array<const char*, Channels::numFlags> flagIds { "turbo", "fat", "bright", "depth", "presence" };

    const juce::Identifier nodeId   { Channels::nodeType };
    const juce::Identifier recordId { "channel" };
    const juce::Identifier activeId { "active" };
    const juce::Identifier indexId  { "i" };
    const juce::Identifier cabinetId { "cabinet" };

    /** Where a build ships the cockpit's cabinets: inside the bundle, beside its devices. */
    juce::File bundledCabinets()
    {
        return juce::File::getSpecialLocation (juce::File::currentApplicationFile)
                   .getChildFile ("Contents").getChildFile ("Resources").getChildFile ("IRs");
    }

    void addCabinets (const juce::File& folder, juce::Array<juce::File>& into)
    {
        if (folder.isDirectory())
            for (const auto& f : folder.findChildFiles (juce::File::findFiles, true))
                if (device::IrLibrary::isAudioFile (f))
                    into.add (f);
    }

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

    // The cabinets: a channel keeps its own too, but by NAME — a cabinet is a file, not a parameter.
    addCabinets (bundledCabinets(), irs);
    addCabinets (device::IrLibrary::directory(), irs);
    std::sort (irs.begin(), irs.end(), [] (const juce::File& x, const juce::File& y)
    {
        return x.getFileName().compareNatural (y.getFileName()) < 0;
    });
}

juce::String Channels::cabinetName (const juce::File& file)
{
    // "03-big-bubba.wav" reads BIG BUBBA: the number is for the order, the dashes for the file system.
    auto name = file.getFileNameWithoutExtension();
    int digits = 0;
    while (digits < name.length() && juce::CharacterFunctions::isDigit (name[digits]))
        ++digits;
    if (digits > 0 && digits < name.length())
        name = name.substring (digits).trimCharactersAtStart ("-_. ");
    return name.replaceCharacters ("-_", "  ").trim();
}

juce::String Channels::cabinetPlaying() const
{
    // A cabinet of ours plays as "the player's own IR": the state carries its key and its name.
    if (amp.apvts.state.getProperty (params::cabIrUserKey).toString().isEmpty())
        return {};
    return amp.apvts.state.getProperty (params::cabIrUserName).toString();
}

int Channels::cabinet() const
{
    const auto playing = cabinetPlaying();
    if (playing.isNotEmpty())
        for (int i = 0; i < irs.size(); ++i)
            if (irs.getReference (i).getFileNameWithoutExtension() == playing)
                return i;
    return -1;
}

void Channels::chooseCabinet (int index)
{
    if (index >= 0 && index < irs.size() && index != cabinet())
        amp.chooseCabFile (irs.getReference (index));
}

void Channels::chooseCabinetNamed (const juce::String& stem)
{
    for (int i = 0; i < irs.size(); ++i)
        if (irs.getReference (i).getFileNameWithoutExtension() == stem)
        {
            chooseCabinet (i);
            return;
        }
    chooseCabinet (0);      // not here any more, or never chosen: the first one (nothing, if there are none)
}

void Channels::align()
{
    if (const int pack = packFor (active()); pack >= 0 && pack != juce::roundToInt (plain (params::preampDevice)))
    {
        set (params::preampDevice, (float) pack);
        amp.settleDeviceNames();
    }

    if (cabinet() < 0)
        chooseCabinet (0);
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
    align();    // the first channel's pack if none of ours was playing, and a cabinet of ours — names settled with them
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

juce::var Channels::tag (const juce::Identifier& id) const
{
    return node().getProperty (id);
}

void Channels::setTag (const juce::Identifier& id, const juce::var& value)
{
    if (tag (id) != value)
        ensureNode().setProperty (id, value, nullptr);
}

juce::ValueTree Channels::whole() const
{
    const auto n = node();
    const int playing = active();

    juce::ValueTree out (nodeId);
    out.setProperty (activeId, playing, nullptr);

    for (int c = 0; c < count; ++c)
    {
        const auto record = n.getChildWithProperty (indexId, c);

        juce::ValueTree full (recordId);
        full.setProperty (indexId, c, nullptr);
        for (const auto& id : kept)
            full.setProperty (id, c == playing ? plain (id)
                                               : record.isValid() && record.hasProperty (id) ? (float) record[id] : defaultOf (id),
                              nullptr);
        for (int f = 0; f < numFlags; ++f)
            full.setProperty (flagIds[(size_t) f], c == playing ? flag ((Flag) f)
                                                                : (bool) record.getProperty (flagIds[(size_t) f], false),
                              nullptr);

        // The cabinet, by name; a channel never visited would open on the first.
        const auto first = irs.isEmpty() ? juce::String() : irs.getReference (0).getFileNameWithoutExtension();
        full.setProperty (cabinetId, c == playing ? cabinetPlaying()
                                                  : record.hasProperty (cabinetId) ? record[cabinetId].toString() : first,
                          nullptr);
        out.appendChild (full, nullptr);
    }
    return out;
}

void Channels::store (juce::ValueTree record) const
{
    for (const auto& id : kept)
        record.setProperty (id, plain (id), nullptr);
    for (int f = 0; f < numFlags; ++f)
        record.setProperty (flagIds[(size_t) f], flag ((Flag) f), nullptr);
    record.setProperty (cabinetId, cabinetPlaying(), nullptr);
}

void Channels::recall (const juce::ValueTree& record)
{
    // No record is a channel never visited: every control at its default, every layer off.
    for (const auto& id : kept)
        set (id, record.isValid() && record.hasProperty (id) ? (float) record[id] : defaultOf (id));
    for (int f = 0; f < numFlags; ++f)
        setFlag ((Flag) f, record.isValid() && (bool) record.getProperty (flagIds[(size_t) f], false));
    chooseCabinetNamed (record.getProperty (cabinetId).toString());
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
    if (auto* p = amp.apvts.getParameter (paramId))
        if (const float to = p->convertTo0to1 (value); ! juce::approximatelyEqual (to, p->getValue()))
            p->setValueNotifyingHost (to);
}

} // namespace orbitamp::catpit
