// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CatpitPresets.h"

#include "../Parameters.h"

namespace orbitamp::catpit
{

namespace
{
    const juce::Identifier nameTag  { "preset" };
    const juce::Identifier soundTag { "presetSound" };

    /** Sets one parameter's value in a state TREE — not in the live state. */
    void setInTree (juce::ValueTree& tree, const juce::String& paramId, float plainValue)
    {
        if (auto child = tree.getChildWithProperty ("id", paramId); child.isValid())
            child.setProperty ("value", plainValue, nullptr);
    }
}

Presets::Presets (AmpProcessor& p, Channels& c) : amp (p), channels (c) {}

juce::File Presets::directory()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);

   #if JUCE_MAC
    dir = dir.getChildFile ("Application Support");     // JUCE answers ~/Library here, one level too high
   #endif

    dir = dir.getChildFile ("Darwin's Cat").getChildFile ("CatpitAmp").getChildFile ("Presets");
    if (! dir.isDirectory())
        dir.createDirectory();
    return dir;
}

juce::StringArray Presets::names()
{
    juce::StringArray out;
    for (const auto& f : directory().findChildFiles (juce::File::findFiles, false, juce::String ("*") + extension))
        out.add (f.getFileNameWithoutExtension());
    out.sortNatural();
    return out;
}

juce::File Presets::fileFor (const juce::String& name)
{
    return directory().getChildFile (juce::File::createLegalFileName (name) + extension);
}

bool Presets::exists (const juce::String& name)
{
    return name.isNotEmpty() && fileFor (name).existsAsFile();
}

juce::String Presets::current() const
{
    return channels.tag (nameTag).toString();
}

bool Presets::changed() const
{
    return current().isNotEmpty() && channels.tag (soundTag).toString() != sound();
}

/** EVERYTHING A PRESET HOLDS, AS ONE SHORT STRING. Two states that would sound the same on every detent
    give the same one, so what is left out matters as much as what is in:
      - INPUT, which no preset holds;
      - the device NUMBERS, which are places in a list that belongs to this machine today — the names
        beside them are the identity, and they are in;
      - the cockpit's node as it lies in the tree, replaced by the four channels written out in full:
        the node's record of the playing channel is stale by design, and a channel never visited has
        none, so the raw node differs between two states that are the same amp. */
juce::String Presets::sound() const
{
    auto tree = amp.apvts.copyState();

    setInTree (tree, params::inTrim, 0.0f);
    setInTree (tree, params::preampDevice, 0.0f);
    setInTree (tree, params::boostDevice, 0.0f);

    if (auto node = tree.getChildWithName (Channels::nodeType); node.isValid())
        tree.removeChild (node, nullptr);
    tree.appendChild (channels.whole(), nullptr);

    return juce::String::toHexString (tree.toXmlString (juce::XmlElement::TextFormat().singleLine()).hashCode64());
}

/** Writes down whose sound this is. Not an edit — the sound did not move — so it leaves no undo step. */
void Presets::mark (const juce::String& name)
{
    if (! channels.adopted())   // a state the cockpit has not made its own yet has nowhere to keep a name
        return;

    const felitronics::appkit::CompareHistory::ScopedSuppress quiet (amp.history);
    channels.setTag (nameTag, name);
    channels.setTag (soundTag, name.isEmpty() ? juce::String() : sound());
}

bool Presets::load (const juce::String& name)
{
    juce::ValueTree tree;
    if (auto xml = juce::XmlDocument::parse (fileFor (name)))
        tree = juce::ValueTree::fromXml (*xml);
    if (! tree.isValid())
        return false;

    amp.takeEmbeddedIrs (tree);     // a cabinet IR it carries goes to the store, not into the history

    // INPUT stays where the player has it.
    if (auto* in = amp.apvts.getParameter (params::inTrim))
        setInTree (tree, params::inTrim, in->convertFrom0to1 (in->getValue()));

    amp.history.applyEdit (amp.history.active(), tree, "Load " + name);
    mark (name);
    amp.history.markSaved();
    return true;
}

bool Presets::save (const juce::String& name)
{
    if (name.isEmpty())
        return false;

    mark (name);    // first, so the file itself says whose sound it is
    auto xml = amp.presetForSaving().createXml();
    if (xml == nullptr || ! fileFor (name).replaceWithText (xml->toString()))
        return false;

    amp.history.markSaved();
    return true;
}

bool Presets::rename (const juce::String& to)
{
    const auto from = current();
    if (to.isEmpty() || to == from || ! exists (from) || exists (to))
        return false;
    if (! fileFor (from).moveFileTo (fileFor (to)))
        return false;

    // The file moved; the sound did not. Only the name on the node changes.
    const felitronics::appkit::CompareHistory::ScopedSuppress quiet (amp.history);
    channels.setTag (nameTag, to);
    return true;
}

bool Presets::removeCurrent()
{
    const auto name = current();
    if (! exists (name) || ! fileFor (name).deleteFile())
        return false;
    mark ({});
    return true;
}

void Presets::init()
{
    // The identity goes with the values: a device or switch NAME left behind would be aimed straight
    // back over the default the parameter has just been given.
    auto tree = amp.stateForSaving (true);

    for (auto* p : amp.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->paramID != params::inTrim)
                setInTree (tree, rp->paramID, rp->convertFrom0to1 (rp->getDefaultValue()));

    // ...and every channel's memory, and whose preset this was. A state without the node is one the
    // cockpit has not seen, and its face adopts it the way it adopts the first: chain, channel, pack.
    if (auto node = tree.getChildWithName (Channels::nodeType); node.isValid())
        tree.removeChild (node, nullptr);

    amp.history.applyEdit (amp.history.active(), tree, "Init");
    amp.history.markSaved();
}

} // namespace orbitamp::catpit
