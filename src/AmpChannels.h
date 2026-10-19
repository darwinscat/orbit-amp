// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "PluginProcessor.h"
#include "device/PackOpening.h"

namespace orbitamp
{

/** THE AMP'S CHANNELS, AND WHAT EACH REMEMBERS — OrbitAmp's light version of the faces' memory
    (channels/Channels.h). A device's channels are its packs (DeviceLibrary::Device). Leaving one by
    hand — a key of the panel's channel row, or another device out of the menu — leaves its DRIVE, its
    tone knobs, the block's EQ, its switches and SMOOTH as they stood, and coming back finds them there.

    A channel never visited opens on its pack's own tone knobs and switches, and on the DRIVE, EQ and
    SMOOTH the hand already had: those mean the same thing on every device, which is what makes two
    devices comparable at one setting.

    The records live in the processor's state tree, one per pack under its `rig_id`. The tree is what a
    session saves, what undo steps through and what each compare register holds, so the memory
    travels with all three and none of them had to be taught about it. The record of the channel that
    is PLAYING is stale by definition: the live parameters are its truth until it is left.

    Only a hand's switch comes through here. A host moving the device parameter, a session, an undo or
    a preset landing change the parameters themselves, and what they set is what plays.

    Message thread only. Nothing the audio thread reads, and no widgets. */
class AmpChannels
{
public:
    using Library = device::DeviceLibrary;

    explicit AmpChannels (AmpProcessor& p) : amp (p) {}

    /** The state's node and its records: `<AmpChannels turn="n"><Channel id="<rig_id>" turn="k"
        preamp_gain="…" …/></AmpChannels>`, each kept parameter by its id, as a plain value. */
    static juce::Identifier nodeType()    { return "AmpChannels"; }
    static juce::Identifier recordType()  { return "Channel"; }
    static juce::Identifier keyId()       { return "id"; }
    static juce::Identifier turnId()      { return "turn"; }

    /** A pack's key among the records: its `rig_id`, or its name for a pack that states none. */
    static juce::String keyOf (const Library::Pack& p)
    {
        return p.rigId.isNotEmpty() ? p.rigId : "name:" + p.displayName();
    }

    /** The pack that is playing, as an index into the block's list: where the device parameter
        stands, the first pack when it points nowhere — as the block plays it. -1 with no packs. */
    int playing() const
    {
        const auto& packs = amp.preamp.packs;
        if (packs.isEmpty())
            return -1;

        const int i = juce::roundToInt (plain (params::preampDevice));
        return juce::isPositiveAndBelow (i, packs.size()) ? i : 0;
    }

    /** The channel a device opens on when the hand picks it: the one it was last left on, its first
        otherwise. */
    int openingOf (const Library::Device& d) const
    {
        const auto n = node();
        int best = d.channels.isEmpty() ? -1 : d.channels.getFirst();
        int bestTurn = 0;

        for (const int c : d.channels)
        {
            if (! juce::isPositiveAndBelow (c, amp.preamp.packs.size()))
                continue;

            const auto record = n.getChildWithProperty (keyId(), keyOf (amp.preamp.packs.getReference (c)));
            const int turn = record.isValid() ? (int) record.getProperty (turnId(), 0) : 0;

            if (turn > bestTurn)
            {
                best = c;
                bestTurn = turn;
            }
        }

        return best;
    }

    /** Plays the pack at `index`: leaves the playing channel's settings in its record, switches, and
        puts back what the arriving channel was left with — its pack's opening, the first time.

        Everything before this returns, and in one tick, so the history commits it as ONE step: undo
        goes back to the channel that was playing, exactly as it stood. False, and nothing touched, for
        an index with no pack behind it or the pack that is already playing. */
    bool play (int index)
    {
        const auto& packs = amp.preamp.packs;
        if (! juce::isPositiveAndBelow (index, packs.size()))
            return false;

        const int from = playing();
        if (from == index)
            return false;

        // Copied: a face that hears the device parameter move rebuilds itself from the list.
        const auto arriving = packs.getReference (index);
        const auto ids = kept();
        auto n = ensureNode();

        if (from >= 0)
        {
            auto record = recordOf (n, keyOf (packs.getReference (from)), true);

            for (const auto& id : ids)
                record.setProperty (id, plain (id), nullptr);

            // The device's last channel is the one most lately left.
            const int turn = (int) n.getProperty (turnId(), 0) + 1;
            n.setProperty (turnId(), turn, nullptr);
            record.setProperty (turnId(), turn, nullptr);
        }

        set (params::preampDevice, (float) index);
        amp.preamp.selectIfMoved (index);

        if (const auto record = recordOf (n, keyOf (arriving), false); record.isValid())
        {
            for (const auto& id : ids)
                if (record.hasProperty (id))
                    set (id, (float) record[id]);
        }
        else
        {
            const auto opening = device::PackOpening::of (arriving);

            for (int i = 0; i < params::preampNumMeasured; ++i)
                set (params::preampMeasured (i), opening.meas[(size_t) i]);

            for (int i = 0; i < params::numSelectors; ++i)
                set (params::selectorId (params::preampId, i), (float) opening.sel[(size_t) i]);
        }

        amp.settleDeviceNames();
        return true;
    }

    /** What a channel keeps: DRIVE and SMOOTH, the tone knobs, the pack's switches, and the block's
        whole EQ — its mode, its bands, its level — every parameter the console's id prefix names. */
    juce::StringArray kept() const
    {
        juce::StringArray ids { params::preampGain, params::blockSmooth (params::preampId) };

        for (int i = 0; i < params::preampNumMeasured; ++i)
            ids.add (params::preampMeasured (i));

        for (int i = 0; i < params::numSelectors; ++i)
            ids.add (params::selectorId (params::preampId, i));

        const auto console = params::eqId (0, "");

        for (auto* p : amp.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (ranged->getParameterID().startsWith (console))
                    ids.addIfNotAlreadyThere (ranged->getParameterID());

        return ids;
    }

private:
    AmpProcessor& amp;

    /** Asked for afresh every time: an undo, a register or a session REPLACES the state tree, and a
        handle kept across that would be a handle to the tree that was thrown away. */
    juce::ValueTree node() const { return amp.apvts.state.getChildWithName (nodeType()); }

    juce::ValueTree ensureNode()
    {
        auto n = node();
        if (! n.isValid())
        {
            n = juce::ValueTree (nodeType());
            amp.apvts.state.appendChild (n, nullptr);
        }
        return n;
    }

    static juce::ValueTree recordOf (juce::ValueTree n, const juce::String& key, bool create)
    {
        auto record = n.getChildWithProperty (keyId(), key);
        if (! record.isValid() && create)
        {
            record = juce::ValueTree (recordType());
            record.setProperty (keyId(), key, nullptr);
            n.appendChild (record, nullptr);
        }
        return record;
    }

    float plain (const juce::String& id) const
    {
        auto* p = amp.apvts.getParameter (id);
        return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
    }

    /** A hand's write: the host hears a gesture round it, as it does from a knob. */
    void set (const juce::String& id, float value)
    {
        if (auto* p = amp.apvts.getParameter (id))
            if (const float to = p->convertTo0to1 (value); ! juce::approximatelyEqual (to, p->getValue()))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (to);
                p->endChangeGesture();
            }
    }
};

} // namespace orbitamp
