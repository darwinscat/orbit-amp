// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../Parameters.h"
#include "../device/DeviceLibrary.h"
#include "ScopeTap.h"
#include "WaveRibbon.h"

#include <felitronics/lineareq/MagnitudeCurve.h>
#include <felitronics/rigplayer/RigPlayer.h>

#include <juce_events/juce_events.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace orbitamp::core
{

/** Everything one captured block needs to play and to be looked at.

    A block is a device list, the player sounding whatever is chosen from it, and the two taps its
    pictures read. The PLAYER is felitronics::rigplayer — the one the capture app auditions through —
    so which captures sound for a panel, how two of them are mixed along the gain dial, how a tone
    knob becomes a filter and how two models are lined up in time are decided once, there, and
    checked there without a sound card. What is here is the translation: a host's parameters into
    the player's knobs, a pack on disk into the bytes the player asks for, and the player's
    read-outs into what a face draws.

    Nothing here knows which block it is. What differs between them is the SLOT they scan for and the
    parameters they are wired to — both handed in from outside. */
class CapturedBlock
{
public:
    using RigPlayer = felitronics::rigplayer::RigPlayer;

    /** Tone slots a host can see. Parameters exist for this many, and the loaded pack decides what
        each one turns — five, because the biggest preamp pack ships five. */
    static constexpr int numMeasured = params::boostNumMeasured;
    static_assert (params::preampNumMeasured == params::boostNumMeasured,
                   "one block serves every captured slot, so the tone slot counts have to agree");

    /** The picture's frequency axis: the player draws its tone on a 20 Hz – 20 kHz grid. */
    static constexpr double toneLoHz = 20.0;
    static constexpr double toneHiHz = 20000.0;

    explicit CapturedBlock (device::DeviceLibrary::Slot deviceSlot) : slot (deviceSlot) {}

    /** A load still out on the pool comes back to nobody: the token it checks is gone. */
    ~CapturedBlock() { alive.reset(); }

    void prepare (double sampleRate, int maxBlock, int numChannels)
    {
        player.prepare (sampleRate, maxBlock, numChannels);
        ribbon.prepare (sampleRate);
        curDrive = drive.load();
        lastGain = -1.0f;
        lastTone.fill (-1.0f);
    }

    /** Re-reads the folder and loads whatever `index` now points at. Message thread. */
    void rescan (int index)
    {
        packs = device::DeviceLibrary::scan (slot);
        select (index);
    }

    /** Loads the device at `index`. A saved session names a device by position in a list that is
        whatever is on disk today; if it is gone, the first one stands in rather than nothing loading,
        since silence is a worse answer than the wrong device and the name says which it is. */
    void select (int index)
    {
        lastSelected = index;

        const auto* pack = juce::isPositiveAndBelow (index, packs.size())
                             ? &packs.getReference (index)
                             : (packs.isEmpty() ? nullptr : &packs.getReference (0));

        if (pack == nullptr)
        {
            player.unload();
            return;
        }

        // The bytes come by `files[].id`, from whatever thread the host runs the load job on — not
        // the one that opened the pack. So the source owns what it needs to find them, and the
        // library opens the archive afresh per call and shares nothing with anyone.
        auto where = std::make_shared<const device::DeviceLibrary::Pack> (*pack);

        player.load (pack->rig, [where] (const std::string& id) -> std::vector<std::byte>
        {
            const auto block = device::DeviceLibrary::readBinaryEntry (*where, juce::String (id));
            const auto* p = static_cast<const std::byte*> (block.getData());
            return std::vector<std::byte> (p, p + block.getSize());
        });

        player.setBlendShape (shapeFor (lastSmooth));

        if (raw)
            parkTone();

        // The pump lands every knob where its parameter stands: the values are the host's, and
        // a device change does not move them.
        lastGain = -1.0f;
        lastTone.fill (-1.0f);
        lastSelector.fill (-1);
    }

    /** Loads what the device parameter now points at, when it moved. This is how a restored session
        or a host automating the parameter actually lands: replaceState changes the number and calls
        nothing, so the pump watches it — the same arrangement every other loading knob has. */
    void selectIfMoved (int index)
    {
        if (index != lastSelected)
            select (index);
    }

    /** The device's OTHER selecting controls — everything that picks a file and is not the gain dial.
        Fur Coat's octave, IR-X's Bright: in the order the pack lists them, because that order is the
        one the device's own panel had. */
    struct Selector
    {
        juce::String      name;
        juce::StringArray values;
        int               defaultIndex = 0;   // where the pack says a player starts
    };

    juce::Array<Selector> selectors() const
    {
        juce::Array<Selector> out;

        if (const auto* s = nam())
            for (const auto& c : s->device.controls)
                if (c.role != namz::rig::Role::Gain && c.values.size() > 1)
                {
                    Selector sel { juce::String (c.name), {}, 0 };
                    for (const auto& v : c.values)
                        sel.values.add (juce::String (v));

                    sel.defaultIndex = juce::jmax (0, sel.values.indexOf (
                        juce::String (namz::rig::defaultValue (c))));

                    out.add (std::move (sel));
                }

        return out;
    }

    /** Turn a selecting control. The player resolves the closest captured combination — the dial
        keeps its angle and follows along the new knots. Message thread. */
    void applySelectors (const std::array<int, (size_t) params::numSelectors>& indices)
    {
        const auto list = selectors();

        for (int i = 0; i < params::numSelectors && i < list.size(); ++i)
        {
            const auto& sel = list.getReference (i);
            const int index = juce::jlimit (0, sel.values.size() - 1, indices[(size_t) i]);

            if (index == lastSelector[(size_t) i])
                continue;

            lastSelector[(size_t) i] = index;
            player.setSwitch (sel.name.toStdString(), sel.values[index].toStdString());
        }
    }

    /** The gain dial, 0..10 on the panel, onto the captured dial's degrees. SMOOTH lets it stand
        anywhere and the player mixes the two neighbouring captures by angle; STEP lands it on the
        nearest captured position, so one capture plays at a time and the knob reads like a
        selector. A device with no captured axis has nothing to select, so there the knob DRIVES:
        5 is unity, the middle of the dial is the capture as it was taken. Message thread. */
    void setGain (float gain, bool smooth)
    {
        if (smooth != lastSmooth)
        {
            lastSmooth = smooth;
            player.setBlendShape (shapeFor (smooth));
            lastGain = -1.0f;   // the same number lands differently now
        }

        if (! player.loaded())
            return;

        const auto& dial = player.dialName();

        if (dial.empty())
        {
            drive.store (juce::Decibels::decibelsToGain ((gain - 5.0f) * driveDbPerStep));
            return;
        }

        drive.store (1.0f);

        if (juce::approximatelyEqual (gain, lastGain))
            return;

        lastGain = gain;

        double deg = juce::jlimit (0.0, 1.0, (double) gain / 10.0) * (double) player.dialSweep();
        if (! smooth)
            deg = nearestKnot (deg);

        player.setDial (dial, deg);
    }

    /** The pack's per-file input trims (`input_db`), see RigPlayer::setInputTrims: off, every
        capture eats exactly what the chain feeds it — for a library shot at one honest level
        the stated attenuations are somebody else's story. Atomic underneath; any thread. */
    void setInputTrims (bool on) { player.setInputTrims (on); }

    /** Whether the pack's own tone controls are out of the signal — see `params::blockEqMode`. Raw
        is what OURS means: our parametric is standing where the device's tone stack used to, and a
        device wearing both at once is a decibel nobody can trace. Parked, not bypassed: every tone
        knob goes to its `reference`, the position every model was captured at, which is flat by
        construction — exactly what the weights already contain. */
    void setRaw (bool shouldBeRaw)
    {
        if (raw == shouldBeRaw)
            return;

        raw = shouldBeRaw;

        if (raw)
            parkTone();
        else
            lastTone.fill (-1.0f);   // the pump lands every knob again
    }

    bool isRaw() const noexcept { return raw; }

    /** THE POSITION A SWITCH SLOT STANDS ON, BY NAME — message thread.

        A session must save the name, never the fraction. The fraction is an index into the pack's
        position list, so a device repacked one position shorter reopens an old session on a
        different position, silently and with no way to notice. The name is the pack's own address
        for that position (namz addresses a switch by `value`, and since schema 4 there is no `norm`
        on a switch at all — the array order IS the panel order).

        Empty when the slot is not a switch, or when no pack is loaded yet. */
    juce::String switchValueAt (int slot, float parameterValue) const
    {
        const auto tones = player.tones();

        if (slot < 0 || slot >= (int) tones.size())
            return {};

        const auto& t = tones[(size_t) slot];

        if (t.sweep > 0 || t.positions.empty())
            return {};

        const int n     = (int) t.positions.size();
        const int index = juce::jlimit (0, n - 1, juce::roundToInt (parameterValue * (float) (n - 1)));
        return juce::String (t.positions[(size_t) index].value);
    }

    /** The parameter value that lands on a NAMED position, or -1 when this pack has no position by
        that name (including "the pack is not here yet" — the caller retries while `isReady()` is
        false and gives up once it is true). */
    float switchParameterFor (int slot, const juce::String& value) const
    {
        const auto tones = player.tones();

        if (slot < 0 || slot >= (int) tones.size())
            return -1.0f;

        const auto& t = tones[(size_t) slot];

        if (t.sweep > 0 || t.positions.empty())
            return -1.0f;

        const int n = (int) t.positions.size();

        for (int i = 0; i < n; ++i)
            if (value == juce::String (t.positions[(size_t) i].value))
                return n > 1 ? (float) i / (float) (n - 1) : 0.0f;

        return -1.0f;
    }

    /** The tone slots, 0..1 each, onto the pack's knobs in the order it lists them: a swept knob
        takes the value as a fraction of its rotation, a switch takes the position nearest to it. */
    void updateToneIfMoved (const std::array<float, (size_t) numMeasured>& values)
    {
        if (raw || ! player.loaded())
            return;

        const auto tones = player.tones();

        for (int i = 0; i < numMeasured && i < (int) tones.size(); ++i)
        {
            const float v = values[(size_t) i];

            if (juce::approximatelyEqual (v, lastTone[(size_t) i]))
                continue;

            lastTone[(size_t) i] = v;
            const auto& t = tones[(size_t) i];

            if (t.sweep > 0)
            {
                player.setDial (t.name, (double) juce::jlimit (0.0f, 1.0f, v) * (double) t.sweep);
            }
            else if (! t.positions.empty())
            {
                const int n = (int) t.positions.size();
                const int index = juce::jlimit (0, n - 1, juce::roundToInt (v * (float) (n - 1)));
                player.setSwitch (t.name, t.positions[(size_t) index].value);
            }
        }
    }

    /** What the device's tone controls are doing as a whole, in dB at a frequency — the block's
        answer for any picture that wants to draw its tone. Read off the player's own design: the
        curve its FIRs were built from and the bands its biquads are running, both sides of the
        models summed, because they sit in series. Message thread, like every read-out. */
    double toneDb (double freqHz) const
    {
        double sum = 0.0;
        const auto& grid = player.commonGrid();

        for (int side = 0; side < 2; ++side)
        {
            if (const auto& c = player.curveDb (side); ! c.empty() && c.size() == grid.size())
                sum += felitronics::lineareq::curveDbAt (c, grid, freqHz);

            for (const auto& b : player.bands (side))
                sum += felitronics::rigplayer::sectionMagnitudeDb (b, freqHz, player.sampleRate());
        }

        return sum;
    }

    /** The message-thread heartbeat: the player's housekeeping, and its LOAD JOBS. A model the law
        asks for is built on `pool` — bytes from the pack, a network parsed and warmed, some twenty
        milliseconds — and brought back to the player on the message thread, where it installs. With
        no pool the job runs right here, which is what a test wants and what a host without a message
        loop can have. Call it from a timer, a few times a second at least. */
    void pump (juce::ThreadPool* pool)
    {
        player.service();

        if (pool == nullptr)
        {
            while (auto job = player.takeLoadJob())
                player.deliver (RigPlayer::run (std::move (*job)));
            return;
        }

        if (auto job = player.takeLoadJob())
        {
            // The job carries everything the work needs and nothing of the player, so the pool
            // thread touches nothing here. The result rides back on the message queue; if the block
            // is gone by then, the token says so and the model is simply dropped.
            std::weak_ptr<int> token = alive;
            auto* self = this;
            auto boxed = std::make_shared<RigPlayer::LoadJob> (std::move (*job));

            pool->addJob ([self, token, boxed]
            {
                auto loaded = std::make_shared<RigPlayer::Loaded> (RigPlayer::run (std::move (*boxed)));

                juce::MessageManager::callAsync ([self, token, loaded]
                {
                    if (token.lock() != nullptr)
                        self->player.deliver (std::move (*loaded));
                });
            });
        }
    }

    /** Host-rate latency of the models — their rate-matching, when a capture's rate is not the
        host's. Zero for a pack captured at the session's rate. */
    int latencySamples() const { return player.latencySamples(); }

    /** Plays the block and feeds its pictures. `dry` is scratch the caller owns, big enough for the
        buffer — kept so a picture can show what went in, not only what came out. Audio thread. */
    void process (juce::AudioBuffer<float>& buffer, juce::AudioBuffer<float>& dry) noexcept
    {
        const int numSamples = buffer.getNumSamples();

        dry.setSize (1, numSamples, false, false, true);
        dry.copyFrom (0, 0, buffer, 0, 0, numSamples);

        // The drive of a device with no captured axis — into the model, like every trim here.
        if (const float target = drive.load (std::memory_order_relaxed);
            ! juce::approximatelyEqual (target, 1.0f) || ! juce::approximatelyEqual (curDrive, 1.0f))
        {
            buffer.applyGainRamp (0, numSamples, curDrive, target);
            curDrive = target;
        }

        player.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), numSamples);

        scope.write (dry.getReadPointer (0), buffer.getReadPointer (0), numSamples);
        ribbon.write (dry.getReadPointer (0), buffer.getReadPointer (0), numSamples);
    }

    // ---- what a block's face asks about the device it is playing ----

    /** The tone knobs as they play: the pack's description of each, in the pack's order. */
    std::vector<namz::rig::Tone> tones() const { return player.tones(); }

    /** The captured positions of the gain dial, in dial order — the knob's detents. */
    juce::StringArray gainPositions() const
    {
        juce::StringArray out;

        if (const auto* s = nam())
            for (const auto& c : s->device.controls)
                if (c.role == namz::rig::Role::Gain)
                {
                    for (const auto& v : c.values)
                        out.add (juce::String (v));
                    break;
                }

        return out;
    }

    juce::String dialName() const { return juce::String (player.dialName()); }

    juce::String circuit() const
    {
        const auto* s = nam();
        return s != nullptr ? juce::String (s->circuit) : juce::String();
    }

    /** The gear's make and model, as the pack states them. */
    juce::String deviceName() const
    {
        const auto* s = nam();
        if (s == nullptr)
            return {};

        const auto make = juce::String (s->make), model = juce::String (s->model);
        return make.isNotEmpty() ? make + " " + model : model;
    }

    bool isReady() const noexcept { return player.loaded(); }

    /** Whether a measured control's positions are NAMED rather than numbered — the difference
        between a switch and a knob that happened to be swept at two points. */
    static bool namedPositions (const namz::rig::Tone& t)
    {
        // ROTATION OR NOT — that is the whole question, and since schema 4 it is the only signal
        // there is: a switch has an order and no angle, so it states no `sweep` and its positions
        // carry no `norm`. Sniffing the labels for letters was a guess, and it rules the wrong way
        // on a selector whose legends read "1 / 2 / 3".
        return t.sweep <= 0 && ! t.positions.empty();
    }

    /** Whether this device brought a tone stack a console can WEAR: at least one swept dial that is
        not a switch and not a control the pack tested and found flat.

        ONE function, asked from both sides. The face uses it to decide whether the choice between
        the two tone stacks exists at all — a device that measured nothing collapses to ours rather
        than showing an empty row — and the DSP has to reach the same verdict, or a block whose
        parameter says NATIVE while its face wears OURS would have its own bands parked in favour
        of a tone stack that does not exist, and end up with no tone at all. */
    bool hasWearableTone() const
    {
        for (const auto& t : tones())
        {
            if (t.sweep > 0 && ! trustFailed (t))
                return true;

            // ...and since schema 4, a knob that CLICKS can be a tone control too: its positions
            // state the filter each one IS. A device whose only tone is a two-position Bright now
            // genuinely HAS a tone stack, and the face must be allowed to offer it — a switch that
            // shapes the sound and is judged "not tone" leaves the block wearing ours over a stack
            // that exists.
            if (t.sweep <= 0 && switchCarriesBands (t))
                return true;
        }

        return false;
    }

    /** A switch whose positions declare filters rather than merely selecting captures. */
    static bool switchCarriesBands (const namz::rig::Tone& t)
    {
        for (const auto& p : t.positions)
            if (! p.sections.empty())
                return true;

        return false;
    }

    /** A control the pack tested and found NOT to be a filter anywhere: a collapsed trusted band.
        The player plays it as silence; a face should not build a knob for it. Bands are played
        whole — for them `trusted` is provenance, never a verdict. */
    static bool trustFailed (const namz::rig::Tone& t)
    {
        if (! t.sections.empty())
            return false;

        const auto& tr = t.trusted;
        const bool tested = tr.levels >= 2;
        const bool stated = tr.loHz > 0.0 || tr.hiHz > 0.0 || tr.loIndex != 0 || tr.hiIndex != 0;
        return tested && stated && (tr.loIndex > tr.hiIndex || (tr.hiHz > 0.0 && tr.hiHz <= tr.loHz));
    }

    /** Where a fresh device parks a tone slot, 0..1: the pack's `default`, else its `reference`.
        A player must not invent one — two readers starting the same pack at different positions
        make it two products. */
    static double defaultNorm (const namz::rig::Tone& t)
    {
        const auto start = felitronics::rigplayer::toneStart (t);

        if (t.sweep > 0)
        {
            double norm = 0.5;
            return felitronics::rigplayer::toneNorm (t, start, norm) ? norm : 0.5;
        }

        const int n = (int) t.positions.size();
        for (int i = 0; i < n; ++i)
            if (t.positions[(size_t) i].value == start)
                return n > 1 ? (double) i / (double) (n - 1) : 0.0;

        return 0.5;
    }

    RigPlayer player;
    ScopeTap scope;
    WaveRibbon ribbon;
    juce::Array<device::DeviceLibrary::Pack> packs;

private:
    // Four decibels a step: the full dial spans forty, which is enough to take a capture from barely
    // breaking up to thoroughly into it without leaving the range a model behaves in.
    static constexpr float driveDbPerStep = 4.0f;

    const namz::rig::Stage* nam() const { return player.loaded() ? &player.stage() : nullptr; }

    /** SMOOTH is the player's own law — the midpoint handover, the full span. STEP is that law with
        no width at all: a hard switch halfway between two captures, which the dial never visits
        anyway because the block has already landed it on one of them. */
    static felitronics::rigplayer::BlendShape shapeFor (bool smooth)
    {
        return smooth ? felitronics::rigplayer::BlendShape {}
                      : felitronics::rigplayer::BlendShape { 0.5, 0.0 };
    }

    /** The captured position nearest to `deg`, among the files along the dial at the panel's other
        settings. With nothing along it, the angle itself. */
    double nearestKnot (double deg) const
    {
        const auto& knots = player.selection().knots;
        if (knots.empty())
            return deg;

        double best = knots.front().deg;
        for (const auto& k : knots)
            if (std::abs (k.deg - deg) < std::abs (best - deg))
                best = k.deg;

        return best;
    }

    /** Every tone knob to its `reference`: the position the models were captured at, flat by
        construction. */
    void parkTone()
    {
        for (const auto& t : player.tones())
            player.setSwitch (t.name, t.reference);
    }

    device::DeviceLibrary::Slot slot;
    std::array<float, (size_t) numMeasured> lastTone { };
    std::array<int, (size_t) params::numSelectors> lastSelector { -1, -1 };
    float lastGain    = -1.0f;
    bool  lastSmooth  = true;
    int   lastSelected = -1;
    bool  raw = false;

    std::atomic<float> drive { 1.0f };
    float curDrive = 1.0f;

    // LAST, so it dies first: a load coming back finds the token gone before anything else here is.
    std::shared_ptr<int> alive = std::make_shared<int> (0);
};

} // namespace orbitamp::core
