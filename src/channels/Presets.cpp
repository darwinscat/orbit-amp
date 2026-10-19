// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "Presets.h"

#include "../Parameters.h"
#include "../Product.h"

namespace orbitamp::channels
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

juce::String Presets::extension()
{
    return "." + juce::String (product::name).toLowerCase();
}

juce::File Presets::directory()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);

   #if JUCE_MAC
    dir = dir.getChildFile ("Application Support");     // JUCE answers ~/Library here, one level too high
   #endif

    dir = dir.getChildFile ("Darwin's Cat").getChildFile (product::name).getChildFile ("Presets");
    if (! dir.isDirectory())
        dir.createDirectory();
    return dir;
}

juce::StringArray Presets::names()
{
    juce::StringArray out;
    for (const auto& f : directory().findChildFiles (juce::File::findFiles, false, "*" + extension()))
        out.add (f.getFileNameWithoutExtension());
    out.sortNatural();
    return out;
}

juce::File Presets::fileFor (const juce::String& name)
{
    return directory().getChildFile (juce::File::createLegalFileName (name) + extension());
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
      - how DRIVE goes between the captures, which the settings say;
      - how hard TURBO pushes, which they say too: that it is on is in, with its channel;
      - the device NUMBERS, which are places in a list that belongs to this machine today — the names
        beside them are the identity, and they are in;
      - the product's node as it lies in the tree, replaced by every channel written out in full:
        the node's record of the playing channel is stale by design, and a channel never visited has
        none, so the raw node differs between two states that are the same amp. */
juce::String Presets::sound() const
{
    auto tree = amp.apvts.copyState();

    setInTree (tree, params::inTrim, 0.0f);
    if (const auto* park = channels.lineup().parkId)
        setInTree (tree, park, 0.0f);                           // ...nor PARK
    tree.removeProperty (channels.leverZeroId(), nullptr);      // ...nor the zero INPUT was dialled against
    setInTree (tree, params::blockSmooth (params::preampId), 1.0f);    // ...nor DRIVE's way: as every state had it before there was a choice
    setInTree (tree, params::blockIn (params::preampId), 0.0f);        // ...nor TURBO's push: Channels::whole says on or off
    setInTree (tree, params::preampDevice, 0.0f);

    // CHANNEL the parameter says nothing the node does not, and is left out whole: a preset marked
    // before the parameter existed carries a fingerprint with no such line in it.
    if (auto asked = tree.getChildWithProperty ("id", juce::String (channels.lineup().parameterId)); asked.isValid())
        tree.removeChild (asked, nullptr);

    if (auto node = tree.getChildWithName (channels.nodeType()); node.isValid())
        tree.removeChild (node, nullptr);
    tree.appendChild (channels.whole(), nullptr);

    return juce::String::toHexString (tree.toXmlString (juce::XmlElement::TextFormat().singleLine()).hashCode64());
}

/** Writes down whose sound this is. Not an edit — the sound did not move — so it leaves no undo step. */
void Presets::mark (const juce::String& name)
{
    if (! channels.adopted())   // a state the product has not made its own yet has nowhere to keep a name
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

    // INPUT stays where the player has it — the trim, and the lever's zero it was dialled against.
    if (auto* in = amp.apvts.getParameter (params::inTrim))
        setInTree (tree, params::inTrim, in->convertFrom0to1 (in->getValue()));
    if (const auto zero = amp.apvts.state.getProperty (channels.leverZeroId()); ! zero.isVoid())
        tree.setProperty (channels.leverZeroId(), zero, nullptr);
    else
        tree.removeProperty (channels.leverZeroId(), nullptr);

    // ...and the amp stays parked or playing as it is.
    if (const auto* park = channels.lineup().parkId)
        if (auto* p = amp.apvts.getParameter (park))
            setInTree (tree, park, p->convertFrom0to1 (p->getValue()));

    // ...and DRIVE goes on turning the way the settings have it.
    const auto smooth = params::blockSmooth (params::preampId);
    if (auto* way = amp.apvts.getParameter (smooth))
        setInTree (tree, smooth, way->getValue());

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
    // product has not seen, and its engine adopts it the way it adopts the first: chain, channel, pack.
    if (auto node = tree.getChildWithName (channels.nodeType()); node.isValid())
        tree.removeChild (node, nullptr);

    amp.history.applyEdit (amp.history.active(), tree, "Init");
    amp.history.markSaved();
}

} // namespace orbitamp::channels
