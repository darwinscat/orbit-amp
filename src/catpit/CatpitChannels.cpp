// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CatpitChannels.h"

#include "CatpitSet.h"
#include "../Parameters.h"
#include "../device/FactorySet.h"
#include "../device/IrLibrary.h"

#include <OrbitAmpVersion.h>

#include <algorithm>
#include <array>

namespace orbitamp::catpit
{

namespace
{
    /** A channel's pack is the capture it plays, known by the `rig_id` its manifest carries — what the
        pack IS, whatever its file is called. The X88-IR's own four channels: Normal for Clean, Altered
        for Edge, Crunch for Crunch, Overdrive for Hi-Gain. */
    constexpr std::array<const char*, Channels::count> captures { "x88-ir-normal-bae8", "x88-ir-altered-c14c",
                                                                  "x88-ir-crunch-2abb", "x88-ir-overdrive-0344" };

    constexpr std::array<const char*, Channels::numFlags> flagIds { "fat", "bright", "depth", "presence" };

    const juce::Identifier nodeId   { Channels::nodeType };
    const juce::Identifier recordId { "channel" };
    const juce::Identifier activeId { "active" };
    const juce::Identifier indexId  { "i" };
    const juce::Identifier cabinetId { "cabinet" };

    /** The family's folder beside the products': where the set a build carries is laid out
        (CatpitSet.h), and where the device library looks for the packs the builds carry. */
    juce::File factory()
    {
        return device::DeviceLibrary::factoryDirectory();
    }

    /** Where a build's own cabinets are: laid out with its packs, and named in the cockpit's own list. */
    juce::Array<juce::File> bundledCabinets()
    {
        return set::ownCabinets (factory());
    }

    /** The cabinets among `files`, in the order of their names. One too big to be kept inside a session
        (EmbeddedIrs) cannot be played, and a cabinet that cannot be played is not one. */
    juce::Array<juce::File> cabinetsAmong (const juce::Array<juce::File>& files)
    {
        juce::Array<juce::File> found;
        for (const auto& f : files)
            if (device::IrLibrary::isAudioFile (f) && f.getSize() <= device::EmbeddedIrs::maxBytes)
                found.add (f);

        std::sort (found.begin(), found.end(), [] (const juce::File& x, const juce::File& y)
        {
            return x.getFileName().compareNatural (y.getFileName()) < 0;
        });
        return found;
    }

    /** A folder's cabinets, in the order of their names. */
    juce::Array<juce::File> cabinetsIn (const juce::File& folder)
    {
        return cabinetsAmong (folder.isDirectory() ? folder.findChildFiles (juce::File::findFiles, true)
                                                   : juce::Array<juce::File>());
    }

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
    kept.add (params::blockIn (params::preampId));      // TURBO: how hard the capture is fed
    for (int t = 0; t < numTones; ++t)
        kept.add (toneId (t));
    kept.add (params::cabHpfOn);
    kept.add (params::cabLpfOn);
    kept.add (params::reverbMix);
    kept.add (params::reverbType);
    kept.add (params::delayMix);
    kept.add (params::delayDiv);        // the note value; the tempo it is counted in is the song's, not a channel's

    readCabinets();
}

void Channels::readCabinets()
{
    // The cabinets: a channel keeps its own too, but by NAME — a cabinet is a file, not a parameter.
    // The build's own stand first and the player's after them, each group in the order of its names:
    // what a player adds never lands among the factory ones. The build's own are the ones the cockpit's
    // list names, not whatever another product laid out beside them.
    irs = cabinetsAmong (bundledCabinets());
    factory = irs.size();
    irs.addArray (cabinetsIn (device::IrLibrary::directory()));
    irKeys.clear();
}

Channels::Added Channels::importCabinets (const juce::StringArray& paths)
{
    Added out;
    const auto before = irs;
    const auto library = device::IrLibrary::directory();

    for (const auto& path : paths)
    {
        const juce::File from (path);
        if (from.isAChildOf (library))
            continue;       // it is one of the player's already

        if (device::IrLibrary::isAudioFile (from) && from.getSize() > device::EmbeddedIrs::maxBytes)
            out.refused.add (from.getFileName());
        else
            device::IrLibrary::importPath (from, library);
    }

    readCabinets();
    for (int i = factory; i < irs.size(); ++i)
        if (! before.contains (irs.getReference (i)))
        {
            ++out.cabinets;
            if (out.first < 0)
                out.first = i;
        }
    return out;
}

juce::String Channels::cabinetStem (const juce::File& file)
{
    // "03-big-bubba.wav" is big-bubba: the number is for the order.
    auto name = file.getFileNameWithoutExtension();
    int digits = 0;
    while (digits < name.length() && juce::CharacterFunctions::isDigit (name[digits]))
        ++digits;
    if (digits > 0 && digits < name.length())
        name = name.substring (digits).trimCharactersAtStart ("-_. ");
    return name;
}

juce::String Channels::cabinetName (const juce::File& file)
{
    // ...and reads BIG BUBBA: the dashes are for the file system.
    return cabinetStem (file).replaceCharacters ("-_", "  ").trim();
}

int Channels::defaultCabinet() const
{
    // Only among the build's own: a player's file of the same name is the player's, not the default.
    for (int i = 0; i < juce::jmin (factory, irs.size()); ++i)
        if (cabinetStem (irs.getReference (i)).equalsIgnoreCase (defaultCabinetStem))
            return i;
    return irs.isEmpty() ? -1 : 0;
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
    if (playing.isEmpty())
        return -1;

    for (int i = 0; i < irs.size(); ++i)
        if (irs.getReference (i).getFileNameWithoutExtension() == playing)
            return i;

    // No file of that name. A state carries its cabinet's bytes, though, and the key of them: a file
    // the player has since renamed is still that cabinet, and is found by what it is.
    const auto key = amp.apvts.state.getProperty (params::cabIrUserKey).toString();
    for (int i = 0; i < irs.size(); ++i)
        if (keyOfFile (i) == key)
            return i;

    return -1;
}

juce::String Channels::keyOfFile (int index) const
{
    // Once a file: this is asked on every step of the history, and the list itself is read once.
    irKeys.resize ((size_t) irs.size());
    auto& key = irKeys[(size_t) index];
    if (! key.has_value())
    {
        const auto& file = irs.getReference (index);
        juce::MemoryBlock bytes;
        key = file.getSize() <= device::EmbeddedIrs::maxBytes && file.loadFileAsData (bytes)
                  ? device::EmbeddedIrs::keyOf (bytes) : juce::String();
    }
    return *key;
}

bool Channels::cabinetIsStale() const
{
    const int playing = cabinet();
    if (playing < 0)
        return false;

    // Found by what it is, under a name the file no longer has: the state's word for it is the stale part.
    if (irs.getReference (playing).getFileNameWithoutExtension() != cabinetPlaying())
        return true;

    const auto ofFile = keyOfFile (playing);
    return ofFile.isNotEmpty() && ofFile != amp.apvts.state.getProperty (params::cabIrUserKey).toString();
}

void Channels::nameCabinet (const juce::File& file)
{
    amp.apvts.state.setProperty (params::cabIrUserName, file.getFileNameWithoutExtension(), nullptr);
    amp.apvts.state.setProperty (params::cabIrUserFrom,
                                 file.getRelativePathFrom (device::IrLibrary::directory()).replaceCharacter ('\\', '/'), nullptr);
}

bool Channels::renameCabinet (int index, const juce::String& name)
{
    if (index < factory || index >= irs.size())
        return false;       // the build's own are not the player's to rename

    const auto file = irs[index];
    const auto was  = file.getFileNameWithoutExtension();

    // The name it will have, cut the way the library cuts it: legal, and with the file's own extension.
    const auto legal = juce::File::createLegalFileName (name.trim()).upToLastOccurrenceOf (".", false, false);
    const auto renamed = file.getSiblingFile (legal + file.getFileExtension());
    if (legal.isEmpty() || ! device::IrLibrary::rename (device::IrLibrary::directory(), file, name))
        return false;

    // A CHANNEL REMEMBERS ITS CABINET BY THE FILE'S NAME, and so does the sound that is playing: both
    // go with the file, or every channel left on this cabinet would come back on the first one.
    const auto now = renamed.getFileNameWithoutExtension();
    for (auto record : node())
        if (record.hasType (recordId) && record.getProperty (cabinetId).toString() == was)
            record.setProperty (cabinetId, now, nullptr);
    if (cabinetPlaying() == was)
        nameCabinet (renamed);

    readCabinets();
    return true;
}

bool Channels::removeCabinet (int index)
{
    if (index < factory || index >= irs.size()
        || ! device::IrLibrary::remove (device::IrLibrary::directory(), irs[index]))
        return false;

    readCabinets();
    return true;
}

void Channels::chooseCabinet (int index)
{
    if (index >= 0 && index < irs.size() && index != cabinet())
        amp.chooseCabFile (irs.getReference (index));
}

bool Channels::cabinetOn() const
{
    return plain (params::cabOn) > 0.5f;
}

void Channels::setCabinetOn (bool on)
{
    if (cabinetOn() != on)
        set (params::cabOn, on ? 1.0f : 0.0f);
}

void Channels::chooseCabinetNamed (const juce::String& stem)
{
    for (int i = 0; i < irs.size(); ++i)
        if (irs.getReference (i).getFileNameWithoutExtension() == stem)
        {
            chooseCabinet (i);
            return;
        }
    chooseCabinet (defaultCabinet());   // not here any more, or never chosen: the one a channel opens on
}

void Channels::align()
{
    if (const int pack = packFor (active()); pack >= 0 && pack != juce::roundToInt (plain (params::preampDevice)))
    {
        set (params::preampDevice, (float) pack);
        amp.settleDeviceNames();
    }

    if (const int playing = cabinet(); playing < 0)
    {
        chooseCabinet (defaultCabinet());
    }
    else if (irs.getReference (playing).getFileNameWithoutExtension() != cabinetPlaying())
    {
        // The cabinet it is, under the name the file had when the state was kept: the name is put right.
        nameCabinet (irs.getReference (playing));
    }
    else if (cabinetIsStale())
    {
        // The file is the cabinet: read again, and its key is whatever was just read — or nothing, if
        // it would not read, so that it is not asked for again.
        const bool read = amp.chooseCabFile (irs.getReference (playing));
        irKeys[(size_t) playing] = read ? amp.apvts.state.getProperty (params::cabIrUserKey).toString() : juce::String();
    }
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
        if (library.getReference (i).rigId == captures[(size_t) juce::jlimit (0, count - 1, channel)])
            return i;
    return -1;
}

bool Channels::adopt()
{
    if (node().isValid())
        return false;

    // A device still where its parameter starts is nobody's choice — a fresh instance, an INIT — and the
    // cockpit opens on its first channel then, whichever pack the engine starts on.
    const int playing = juce::roundToInt (plain (params::preampDevice));
    const bool chosen = playing != juce::roundToInt (defaultOf (0, params::preampDevice));
    int channel = -1;
    for (int c = 0; c < count; ++c)
        if (chosen && packFor (c) >= 0 && packFor (c) == playing)
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
    recall (to, recordOf (n, to, false));
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
        {
            const float value = c == playing ? plain (id)
                                             : record.isValid() && record.hasProperty (id) ? (float) record[id] : defaultOf (c, id);

            // TURBO as on or off: how hard it pushes is the settings' to say, not the channel's.
            full.setProperty (id, id == params::blockIn (params::preampId) ? (value > turboOnAbove ? 1.0f : 0.0f) : value, nullptr);
        }
        for (int f = 0; f < numFlags; ++f)
            full.setProperty (flagIds[(size_t) f], c == playing ? flag ((Flag) f)
                                                                : (bool) record.getProperty (flagIds[(size_t) f], false),
                              nullptr);

        // The cabinet, by name; a channel never visited would open on the one every channel opens on.
        const auto opening = defaultCabinet() < 0 ? juce::String() : irs.getReference (defaultCabinet()).getFileNameWithoutExtension();
        full.setProperty (cabinetId, c == playing ? cabinetPlaying()
                                                  : record.hasProperty (cabinetId) ? record[cabinetId].toString() : opening,
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

void Channels::recall (int channel, const juce::ValueTree& record)
{
    // No record is a channel never visited: every control at what that channel opens on, every layer off.
    for (const auto& id : kept)
        set (id, settled (id, record.isValid() && record.hasProperty (id) ? (float) record[id] : defaultOf (channel, id)));
    for (int f = 0; f < numFlags; ++f)
        setFlag ((Flag) f, record.isValid() && (bool) record.getProperty (flagIds[(size_t) f], false));
    chooseCabinetNamed (record.getProperty (cabinetId).toString());
}

float Channels::settled (const juce::String& paramId, float value) const
{
    return paramId == params::blockIn (params::preampId) && value > turboOnAbove ? turboPush : value;
}

float Channels::plain (const juce::String& paramId) const
{
    auto* p = amp.apvts.getParameter (paramId);
    jassert (p != nullptr);
    return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
}

float Channels::defaultOf (int channel, const juce::String& paramId) const
{
    const auto c = (size_t) juce::jlimit (0, count - 1, channel);

    if (paramId == params::reverbType)      // the channel's own room, whatever the engine opens on
        return (float) juce::jmax (0, params::reverbCharacters.indexOf (defaultReverbs[c]));
    if (paramId == params::delayMix)
        return defaultDelayMix[c];

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

namespace orbitamp
{

/** The cockpit's set is in its binary (CatpitSet.h). Once in a process, however many of the amp it
    holds: the first one built lays it out, and the folder is the same for all of them — as the build
    this is, so that of two products carrying one file the newer build's stands. The cockpit's own
    `Factory` of before is emptied with it, of what a build of before laid there. */
void layOutFactorySet()
{
    static const int laid = device::FactorySet::retire (device::DeviceLibrary::formerFactoryDirectory())
                          + catpit::set::layOut (catpit::factory(), version::kBuildNumber);
    juce::ignoreUnused (laid);
}

} // namespace orbitamp
