// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "Channels.h"

#include "Set.h"
#include "../Parameters.h"
#include "../device/IrLibrary.h"

#include <algorithm>
#include <array>

namespace orbitamp::channels
{

namespace
{
    constexpr std::array<const char*, Channels::numFlags> flagIds { "fat", "bright", "depth", "presence" };

    const juce::Identifier recordId { "channel" };
    const juce::Identifier activeId { "active" };
    const juce::Identifier indexId  { "i" };
    const juce::Identifier cabinetId { "cabinet" };

    // A pair's record (Lineup::pairs) says which of the two it was left playing, and the tones by their
    // knobs: the two packs may keep them in different places.
    const juce::Identifier playedId { "played" };
    const std::array<juce::Identifier, Channels::maxTones> knobIds { "knob_1", "knob_2", "knob_3", "knob_4", "knob_5" };

    /** The family's factory folder, which the installer lays out: where the product's set lies (Set.h),
        and where the device library looks for the packs the product plays out of the box. */
    juce::File factory()
    {
        return device::DeviceLibrary::factoryDirectory();
    }

    /** Where a build's own cabinets are: in the factory folder with its packs, named in the product's own list. */
    juce::Array<juce::File> bundledCabinets (const juce::String& product)
    {
        return set::ownCabinets (factory(), product);
    }

    /** The cabinets among `files`, in the order of their names — and, given the `shelf` they lie on,
        folder by folder in the order of the folders' names first. One too big to be kept inside a
        session (EmbeddedIrs) cannot be played, and a cabinet that cannot be played is not one. */
    juce::Array<juce::File> cabinetsAmong (const juce::Array<juce::File>& files, const juce::File& shelf = {})
    {
        juce::Array<juce::File> found;
        for (const auto& f : files)
            if (device::IrLibrary::isAudioFile (f) && f.getSize() <= device::EmbeddedIrs::maxBytes)
                found.add (f);

        std::sort (found.begin(), found.end(), [&shelf] (const juce::File& x, const juce::File& y)
        {
            if (shelf != juce::File())
                if (const int folders = x.getParentDirectory().getRelativePathFrom (shelf)
                                            .compareNatural (y.getParentDirectory().getRelativePathFrom (shelf));
                    folders != 0)
                    return folders < 0;

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

Channels::Channels (AmpProcessor& p, const Lineup& l) : amp (p), line (l)
{
    kept.add (params::preampGain);
    kept.add (params::blockIn (params::preampId));      // TURBO: how hard the capture is fed
    for (int t = 0; t < maxTones; ++t)                  // the tones each channel's knobs turn: its pack's places, as many as it has
        for (int c = 0; c < count(); ++c)
            if (const auto id = toneOf (c, t); id.isNotEmpty())
                tones.addIfNotAlreadyThere (id);
    kept.addArray (tones);
    for (int s = 0; s < params::numSelectors; ++s)      // where its pack's switches stand
        kept.add (params::selectorId (params::preampId, s));
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
    // what a player adds never lands among the factory ones. The build's own are the ones the product's
    // list names, not whatever another product laid out beside them.
    irs = cabinetsAmong (bundledCabinets (amp.product().name), set::cabinetsIn (device::DeviceLibrary::factoryDirectory()));

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

juce::String Channels::cabinetFolder (int index) const
{
    if (index < 0 || index >= juce::jmin (factory, irs.size()))
        return {};

    const auto shelf = set::cabinetsIn (device::DeviceLibrary::factoryDirectory());
    const auto dir   = irs.getReference (index).getParentDirectory();
    return dir.isAChildOf (shelf) ? dir.getRelativePathFrom (shelf).replaceCharacter ('\\', '/') : juce::String();
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
        if (cabinetStem (irs.getReference (i)).equalsIgnoreCase (line.openingCabinet))
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
    hold (active());

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
    return amp.apvts.state.getChildWithName (nodeType());
}

juce::ValueTree Channels::ensureNode()
{
    auto n = node();
    if (! n.isValid())
    {
        n = juce::ValueTree (nodeType());
        n.setProperty (activeId, 0, nullptr);
        amp.apvts.state.appendChild (n, nullptr);
    }
    return n;
}

int Channels::packFor (int channel) const
{
    const auto& library = amp.preamp.packs;
    for (int i = 0; i < library.size(); ++i)
        if (library.getReference (i).rigId == line.channels[(size_t) juce::jlimit (0, count() - 1, channel)].capture)
            return i;
    return -1;
}

juce::StringArray Channels::toneNamesOf (int channel) const
{
    juce::StringArray names;
    if (const int pack = packFor (channel); pack >= 0)
        for (const auto& stage : amp.preamp.packs.getReference (pack).rig.chain)
        {
            if (stage.kind != namz::rig::StageKind::Nam)
                continue;
            for (int t = 0; t < (int) stage.tone.size() && t < maxTones; ++t)
                names.add (juce::String (stage.tone[(size_t) t].name).trim());
            break;
        }
    return names;
}

std::vector<namz::rig::Tone> Channels::tonesOf (int channel) const
{
    std::vector<namz::rig::Tone> out;
    if (const int pack = packFor (channel); pack >= 0)
        for (const auto& stage : amp.preamp.packs.getReference (pack).rig.chain)
        {
            if (stage.kind != namz::rig::StageKind::Nam)
                continue;
            for (size_t t = 0; t < stage.tone.size() && (int) t < maxTones; ++t)
                out.push_back (stage.tone[t]);
            break;
        }
    return out;
}

juce::String Channels::toneOf (int channel, int knob) const
{
    // Knob i is the pack's tone i: there while its list goes that far.
    return knob >= 0 && knob < toneNamesOf (channel).size() ? params::preampMeasured (knob) : juce::String();
}

std::vector<Channels::PackSwitch> Channels::switchesOf (int channel) const
{
    std::vector<PackSwitch> out;
    const int pack = packFor (channel);
    if (pack < 0)
        return out;

    const auto& own = line.channels[(size_t) juce::jlimit (0, count() - 1, channel)];
    for (const auto& stage : amp.preamp.packs.getReference (pack).rig.chain)
    {
        if (stage.kind != namz::rig::StageKind::Nam)
            continue;

        // As the block reads the same pack (core::CapturedBlock::selectors): the captured stage's
        // controls that pick a file and are not the gain dial, one to a slot, as many as there are slots.
        for (const auto& c : stage.device.controls)
            if (c.role != namz::rig::Role::Gain && c.values.size() > 1 && (int) out.size() < params::numSelectors)
            {
                PackSwitch s;
                s.slot = (int) out.size();
                s.name = juce::String (c.name);
                for (const auto& v : c.values)
                    s.values.add (juce::String (v));
                s.held = own.held.control != nullptr && s.name == own.held.control;
                s.opening = juce::jmax (0, s.values.indexOf (s.held ? juce::String (own.held.value)
                                                                    : juce::String (namz::rig::defaultValue (c))));
                out.push_back (std::move (s));
            }
        break;
    }
    return out;
}

void Channels::hold (int channel)
{
    for (const auto& s : switchesOf (channel))
        if (s.held)
            set (params::selectorId (params::preampId, s.slot), (float) s.opening);
}

bool Channels::adopt()
{
    if (node().isValid())
        return false;

    // A device still where the engine starts it is nobody's choice — an INIT, a state from before the
    // product — and the product opens on its first channel then, whichever pack the engine starts on. A
    // fresh instance already stands on its first channel's pack, and finds that channel by it.
    auto* device = amp.apvts.getParameter (params::preampDevice);
    const int playing = juce::roundToInt (plain (params::preampDevice));
    const bool chosen = device != nullptr && playing != juce::roundToInt (device->convertFrom0to1 (amp.engineDefault (*device)));
    int channel = -1;
    for (int c = 0; c < count() && channel < 0; ++c)       // two channels on one pack: the first of them
        if (chosen && packFor (c) >= 0 && packFor (c) == playing)
            channel = c;

    ensureNode().setProperty (activeId, juce::jmax (0, channel), nullptr);
    align();    // the first channel's pack if none of ours was playing, and a cabinet of ours — names settled with them
    return true;
}

int Channels::active() const
{
    return juce::jlimit (0, count() - 1, (int) node().getProperty (activeId, 0));
}

bool Channels::select (int to)
{
    to = juce::jlimit (0, count() - 1, to);
    const int from = active();
    if (to == from)
        return true;

    const int pack = packFor (to);
    if (pack < 0)
        return false;

    auto n = ensureNode();
    const bool twin = memoryOf (to) == memoryOf (from);
    const auto turned = knobs();
    store (recordOf (n, memoryOf (from), true));
    n.setProperty (activeId, to, nullptr);

    // The pack, then what is dialled on it, then the pack's name beside its number — all before this
    // returns, so the state is never caught half-way, and all in one tick, so the history commits it
    // as ONE step: undo goes back to the channel that was playing, exactly as it stood. To the other
    // of the pair, the same memory: only the pack, the tones on their knobs in its places, and the
    // switch it holds where it holds it.
    set (params::preampDevice, (float) pack);
    if (twin)
    {
        turnKnobs (to, turned);
        hold (to);
    }
    else
    {
        recall (to, recordOf (n, memoryOf (to), false));
    }
    amp.settleDeviceNames();
    return true;
}

int Channels::memoryOf (int channel) const noexcept
{
    return line.pairs ? channel - channel % 2 : channel;
}

int Channels::leftOn (int channel) const
{
    const int memory = memoryOf (juce::jlimit (0, count() - 1, channel));
    if (memoryOf (active()) == memory)
        return active();

    const auto record = recordOf (node(), memory, false);
    const int played = record.isValid() ? (int) record.getProperty (playedId, memory) : memory;
    return played >= 0 && played < count() && memoryOf (played) == memory ? played : memory;
}

Channels::Knobs Channels::knobs() const
{
    Knobs k {};
    for (int t = 0; t < maxTones; ++t)
        if (const auto id = toneOf (active(), t); id.isNotEmpty())     // a knob past the pack's list stands nowhere
            k[(size_t) t] = plain (id);
    return k;
}

void Channels::turnKnobs (int channel, const Knobs& k)
{
    // A knob that stood nowhere — past the list of the pack it was on — has nothing to bring over: its
    // tone on this pack opens where the channel opens it, as a place on no knob here does.
    for (const auto& id : tones)
    {
        const int knob = knobAt (channel, id);
        set (id, knob >= 0 && k[(size_t) knob].has_value() ? *k[(size_t) knob] : defaultOf (channel, id));
    }
}

int Channels::knobAt (int channel, const juce::String& paramId) const
{
    if (paramId.isEmpty())
        return -1;
    for (int t = 0; t < maxTones; ++t)
        if (toneOf (channel, t) == paramId)
            return t;
    return -1;
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

    juce::ValueTree out (nodeType());
    out.setProperty (activeId, playing, nullptr);

    for (int c = 0; c < count(); ++c)
    {
        if (memoryOf (c) != c)      // a pair is one memory, kept under its first
            continue;

        const auto record = n.getChildWithProperty (indexId, c);
        const bool live = memoryOf (playing) == c;

        juce::ValueTree full (recordId);
        full.setProperty (indexId, c, nullptr);
        if (line.pairs)
            full.setProperty (playedId, live ? playing : leftOn (c), nullptr);
        for (const auto& id : kept)
        {
            const float value = live ? plain (id)
                                     : record.isValid() && record.hasProperty (id) ? (float) record[id] : defaultOf (c, id);

            // TURBO as on or off: how hard it pushes is the settings' to say, not the channel's.
            full.setProperty (id, id == params::blockIn (params::preampId) ? (value > turboOnAbove ? 1.0f : 0.0f) : value, nullptr);
        }
        for (int f = 0; f < numFlags; ++f)
            full.setProperty (flagIds[(size_t) f], live ? flag ((Flag) f)
                                                         : (bool) record.getProperty (flagIds[(size_t) f], false),
                              nullptr);

        // The cabinet, by name; a channel never visited would open on the one every channel opens on.
        const auto opening = defaultCabinet() < 0 ? juce::String() : irs.getReference (defaultCabinet()).getFileNameWithoutExtension();
        full.setProperty (cabinetId, live ? cabinetPlaying()
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
    if (line.pairs)
    {
        const auto k = knobs();
        for (int t = 0; t < maxTones; ++t)
        {
            if (k[(size_t) t].has_value())
                record.setProperty (knobIds[(size_t) t], *k[(size_t) t], nullptr);
            else
                record.removeProperty (knobIds[(size_t) t], nullptr);      // past the list: nowhere to come back to
        }
        record.setProperty (playedId, active(), nullptr);
    }
    for (int f = 0; f < numFlags; ++f)
        record.setProperty (flagIds[(size_t) f], flag ((Flag) f), nullptr);
    record.setProperty (cabinetId, cabinetPlaying(), nullptr);
}

void Channels::recall (int channel, const juce::ValueTree& record)
{
    // No record is a channel never visited: every control at what that channel opens on, every layer off.
    for (const auto& id : kept)
        set (id, settled (id, record.isValid() && record.hasProperty (id) ? (float) record[id] : defaultOf (channel, id)));

    // A pair's record left on the other of the two has its tones in that pack's places: they come back
    // on their knobs, in this pack's.
    if (record.isValid() && record.hasProperty (playedId) && (int) record[playedId] != channel)
    {
        Knobs k {};
        for (int t = 0; t < maxTones; ++t)
            if (record.hasProperty (knobIds[(size_t) t]))      // ...a knob the record has none for opening where the channel does
                k[(size_t) t] = (float) record[knobIds[(size_t) t]];
        turnKnobs (channel, k);
    }
    hold (channel);     // whatever the record says: a host may have moved it while the channel played
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
    const auto& own = line.channels[(size_t) memoryOf (juce::jlimit (0, count() - 1, channel))];     // a pair opens on its first's

    if (paramId == params::reverbType)      // the channel's own room, whatever the engine opens on
        return (float) juce::jmax (0, params::reverbCharacters.indexOf (own.reverb));
    if (paramId == params::reverbMix && own.reverbMix.has_value())
        return *own.reverbMix;
    if (paramId == params::delayMix)
        return own.delayMix;
    for (const auto& s : switchesOf (channel))     // a switch of its pack where the pack opens it, or where the channel holds it
        if (paramId == params::selectorId (params::preampId, s.slot))
            return (float) s.opening;

    // A tone of its pack where the pack starts it (core::CapturedBlock::defaultNorm, as OrbitAmp's panel
    // opens it): a sweep at its reference, the middle of it on every pack here — and a tone of positions on
    // its reference position, not on the parameter's middle, which on a three-way switch is the second.
    const auto packTones = tonesOf (memoryOf (juce::jlimit (0, count() - 1, channel)));
    for (size_t t = 0; t < packTones.size(); ++t)
        if (paramId == params::preampMeasured ((int) t))
            return (float) core::CapturedBlock::defaultNorm (packTones[t]);

    auto* p = amp.apvts.getParameter (paramId);
    return p != nullptr ? p->convertFrom0to1 (p->getDefaultValue()) : 0.0f;
}

void Channels::set (const juce::String& paramId, float value)
{
    if (auto* p = amp.apvts.getParameter (paramId))
        if (const float to = p->convertTo0to1 (value); ! juce::approximatelyEqual (to, p->getValue()))
            p->setValueNotifyingHost (to);
}

} // namespace orbitamp::channels
