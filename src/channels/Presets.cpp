// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "Presets.h"

#include "../Parameters.h"

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

PresetStore Presets::store() const
{
    return { amp.product().name, "." + juce::String (amp.product().name).toLowerCase() };

}

juce::StringArray Presets::names() const
{
    return store().names();
}

bool Presets::exists (const juce::String& name) const
{
    return store().exists (name);
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
    if (const auto* former = amp.product().formerInputZeroId)   // ...nor the zero 0.8.0 noted beside INPUT
        tree.removeProperty (former, nullptr);
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
    auto tree = store().read (name);
    if (! tree.isValid())
        return false;

    amp.takeEmbeddedIrs (tree);     // a cabinet IR it carries goes to the store, not into the history
    amp.takeFormerTrims (tree);     // a preset of 0.8.0's: its gate set where 0.8.0's key heard the guitar
    amp.takeFormerCabinet (tree);   // ...and one saved while every amp opened on DARK MATTER, on RED GIANT

    // INPUT stays where the player has it.
    if (auto* in = amp.apvts.getParameter (params::inTrim))
        setInTree (tree, params::inTrim, in->convertFrom0to1 (in->getValue()));

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
    if (! store().write (name, amp.presetForSaving()))
        return false;

    amp.history.markSaved();
    return true;
}

bool Presets::rename (const juce::String& to)
{
    const auto from = current();
    if (to.isEmpty() || to == from || ! exists (from) || exists (to))
        return false;
    if (! store().move (from, to))
        return false;

    // The file moved; the sound did not. Only the name on the node changes.
    const felitronics::appkit::CompareHistory::ScopedSuppress quiet (amp.history);
    channels.setTag (nameTag, to);
    return true;
}

bool Presets::removeCurrent()
{
    const auto name = current();
    if (! exists (name) || ! store().remove (name))
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
                setInTree (tree, rp->paramID, rp->convertFrom0to1 (amp.engineDefault (*rp)));

    // ...and every channel's memory, and whose preset this was. A state without the node is one the
    // product has not seen, and its engine adopts it the way it adopts the first: chain, channel, pack.
    if (auto node = tree.getChildWithName (channels.nodeType()); node.isValid())
        tree.removeChild (node, nullptr);

    amp.history.applyEdit (amp.history.active(), tree, "Init");
    amp.history.markSaved();
}

} // namespace orbitamp::channels
