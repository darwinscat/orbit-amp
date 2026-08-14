#pragma once

#include "../device/DeviceLibrary.h"

#include <felitronics/nam/NamStage.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>

namespace orbitamp::core
{

/** One captured block — a pedal or a preamp — playing the `.namz` that matches where its knobs are.

    A captured control SELECTS a file: turning Drive does not change a parameter inside the model, it
    picks a different model. So the work here is choosing the right file when a knob moves, loading
    it off the audio thread, and swapping it in without a click. felitronics::nam::NamStage owns the
    swap and the deferred free; this owns the choosing.

    Loading happens on the message thread. The audio thread only ever sees a model that is already
    in memory, or none.

    THE CLICK. A bare swap is three discontinuities landing together: the nonlinear transfer changes
    between one sample and the next, the newcomer's network buffers are empty so its first couple of
    thousand samples are a model talking about silence, and the loudness makeup jumps when the two
    captures were measured at different levels. Nothing downstream can smooth that — by the time it
    is audio it is already a step.

    So there are TWO stages here, and a knob move is a handover rather than a swap. The new model
    loads into whichever one is not playing, gets fed the SAME input silently until its buffers are
    full, and only then is faded in over a few tens of milliseconds; the roles change afterwards, on
    the message thread. Both models run for the length of the handover — that is the whole cost, and
    it is paid only while a knob is moving. */
class CapturedStage
{
public:
    void prepare (double sampleRate, int maxBlock, int numChannels = 2)
    {
        for (auto& s : stages)
        {
            s.prepare (sampleRate, maxBlock);
            s.reset();
        }

        warmSamples = juce::jmax (1, juce::roundToInt (sampleRate * warmSeconds));
        fadeSamples = juce::jmax (1, juce::roundToInt (sampleRate * fadeSeconds));

        // The newcomer's own copy of the input. Allocated here because the audio thread must never
        // ask for memory, and sized for the block the host promised.
        handover.setSize (juce::jmax (2, numChannels), juce::jmax (1, maxBlock), false, false, true);
        handover.clear();

        // Audio is stopped, so a handover caught in flight cannot finish on its own — nothing will
        // call process() again until the host restarts. Finish it by decree: at every phase the
        // model being faded IN is the one the knob asked for, so that is what plays when audio
        // comes back. (The audio thread never gets this shortcut — see settleIfDone.)
        if (const int st = state.load(); phaseOf (st) != Phase::idle)
            state.store (encode (Phase::idle, 1 - liveOf (st)));
    }

    void reset()
    {
        for (auto& s : stages)
            s.reset();
    }

    /** Point the stage at a pack. Clears whatever was loaded — a different device is a different
        set of files, and keeping the old model alive would be playing the previous pedal. */
    void setPack (const device::DeviceLibrary::Pack* newPack)
    {
        pack = newPack;
        targetFile.clear();
        pendingFile.clear();
        ready.store (false);

        for (auto& s : stages)
            s.clearModel();

        for (auto& g : slotGain)
            g.store (1.0f);

        state.store (encode (Phase::idle, 0));

        // The device's own default position for every selecting control. resolve() needs somewhere to
        // start: the gain knob names one setting, and whatever else the device has keeps the value it
        // was captured at until something moves it.
        settings = firstNam() != nullptr ? namz::rig::defaultSettings (firstNam()->device)
                                         : namz::rig::Settings {};

        loadOnlyFile();
    }

    /** The captured positions of the control marked `role: gain`, in the order the pack lists them —
        the real angles the device was captured at, which is what the knob's detents are. */
    juce::StringArray gainPositions() const
    {
        juce::StringArray out;

        if (const auto* stageDef = firstNam())
            for (const auto& c : stageDef->device.controls)
                if (c.role == namz::rig::Role::Gain)
                    for (const auto& v : c.values)
                        out.add (juce::String (v));

        return out;
    }

    /** The device's OTHER selecting controls — everything that picks a file and is not the gain axis.

        Fur Coat is the case that exposed this: a Fuzz dial of twenty-one positions AND an Octave
        switch, forty-two files, two for every dial position. Reading only the gain control meant half
        the pack was unreachable — the player silently got whichever side `resolve` fell back to.

        Returned as name plus its values, in the order the pack lists them, because that order is the
        one the device's own panel had. */
    struct Selector { juce::String name; juce::StringArray values; };

    juce::Array<Selector> selectors() const
    {
        juce::Array<Selector> out;

        if (const auto* stageDef = firstNam())
            for (const auto& c : stageDef->device.controls)
                if (c.role != namz::rig::Role::Gain && c.values.size() > 1)
                {
                    Selector s { juce::String (c.name), {} };
                    for (const auto& v : c.values)
                        s.values.add (juce::String (v));

                    out.add (std::move (s));
                }

        return out;
    }

    /** Turn one of them, and load whatever that combination resolves to. Message thread. */
    void selectValue (const juce::String& controlName, const juce::String& value)
    {
        const auto* stageDef = firstNam();
        if (pack == nullptr || stageDef == nullptr || controlName.isEmpty())
            return;

        const auto* entry = namz::rig::resolve (stageDef->device, settings,
                                                controlName.toStdString(), value.toStdString());
        if (entry == nullptr)
            return;

        load (juce::String (entry->id),
              juce::Decibels::decibelsToGain ((float) -entry->inputDb));
    }

    /** A device with no gain axis has exactly one model, and nothing will ever ask for it by
        position — so it loads when the device does. Without this a lone .nam sits silent: the gain
        knob is what triggers a load, and this kind of device does not have one. */
    void loadOnlyFile()
    {
        const auto* stageDef = firstNam();
        if (pack == nullptr || stageDef == nullptr)
            return;

        if (! gainPositions().isEmpty() || stageDef->device.files.size() != 1)
            return;

        load (juce::String (stageDef->device.files.front().id), 1.0f);
    }

    /** Whether the loaded device's gain is a knob at all. */
    bool hasGainAxis() const { return ! gainPositions().isEmpty(); }

    /** Load the model for a gain index. Message thread: it reads a file and decodes.

        namz picks the file, not us. `resolve` knows what our own search did not: that a combination
        with no capture of its own can be an ALIAS for a neighbour played with less signal going in,
        and that when several controls select, the one just turned is law while the rest fall back to
        the nearest match. A hand-rolled lookup on one control gets the right answer only while the
        device has exactly one. */
    void selectGainIndex (int index)
    {
        const auto* stageDef = firstNam();
        if (pack == nullptr || stageDef == nullptr)
            return;

        const auto positions = gainPositions();
        if (! juce::isPositiveAndBelow (index, positions.size()))
            return;

        juce::String gainName;
        for (const auto& c : stageDef->device.controls)
            if (c.role == namz::rig::Role::Gain)
                gainName = juce::String (c.name);

        if (gainName.isEmpty())
            return;

        const auto* entry = namz::rig::resolve (stageDef->device, settings,
                                                gainName.toStdString(),
                                                positions[index].toStdString());
        if (entry == nullptr)
            return;

        // The pre-volume INTO the model, applied AS WRITTEN: the capture side stores the offset
        // in plain dB (negative = softer), and negating it here played every alias LOUDER and
        // dirtier than its source — the bottom of the dial rose instead of fading. Found by ear
        // on IR-X Ch1's Center bottom, convicted by the level probe, sentenced by the capture
        // app's own table (pre dB -9.0 at the floor).
        load (juce::String (entry->id),
              juce::Decibels::decibelsToGain ((float) entry->inputDb));
    }

    /** Reads one named model in and hands the sound over to it. Message thread.

        `preGain` travels with the model rather than sitting in one shared knob, because for the
        length of a handover there are two models playing and each was captured expecting its own
        amount of signal. One shared pre-volume would have jumped to the newcomer's the instant the
        file landed — a step in the OUTGOING model, which is exactly the click being cured. */
    void load (const juce::String& wanted, float preGain = 1.0f)
    {
        if (pack == nullptr || wanted.isEmpty())
            return;

        settleIfDone();   // a handover that finished while nobody was looking hands over its roles here

        if (wanted == targetFile)   // already playing, or already on its way in
            return;

        // Nothing is playing yet — there is nothing to fade FROM, and a first model must not cost
        // the player fifty milliseconds of silence. It goes straight into the live slot.
        if (! ready.load())
        {
            if (! loadInto (liveOf (state.load()), wanted, preGain))
                return;

            targetFile = wanted;
            ready.store (true);
            return;
        }

        int st = state.load();

        // Mid-fade the spare is already audible, so its model cannot be swapped under the fade.
        // Park the request instead of dropping it: collectGarbage() — the same thirty-a-second pump
        // that asked — retries it as soon as the handover lands. A dial swept fast therefore plays
        // two or three clean steps rather than queueing every notch it passed through.
        const Phase ph = phaseOf (st);
        if ((ph != Phase::idle && ph != Phase::warming)
            || ! state.compare_exchange_strong (st, encode (Phase::loading, liveOf (st))))
        {
            pendingFile = wanted;
            pendingGain = preGain;
            return;
        }

        // The spare is ours alone now: the audio thread keeps feeding it, and will not promote it
        // while the phase says `loading`. A knob moved again mid-warm simply restarts the warm.
        if (! loadInto (1 - liveOf (st), wanted, preGain))
        {
            state.store (encode (ph, liveOf (st)));
            return;
        }

        targetFile = wanted;
        pendingFile.clear();
        warmRemaining.store (warmSamples);
        state.store (encode (Phase::warming, liveOf (st)));
    }

    /** The pack's measured controls — the ones a player computes rather than selects. */
    const std::vector<namz::rig::Measured>* measured() const
    {
        const auto* s = firstNam();
        return s != nullptr ? &s->measured : nullptr;
    }

    /** The device spec string for the glyph row, e.g. "ic:2,diode:4". Empty when the pack is silent
        about the circuit. */
    juce::String circuit() const
    {
        const auto* s = firstNam();
        return s != nullptr ? juce::String (s->circuit) : juce::String();
    }

    juce::String deviceName() const
    {
        const auto* s = firstNam();
        if (s == nullptr)
            return {};

        const auto make = juce::String (s->make), model = juce::String (s->model);
        return make.isNotEmpty() ? make + " " + model : model;
    }

    /** Frees models the swap retired, hands over the roles of a handover the audio thread finished,
        and retries a request that arrived while one was in flight. Message thread, on the pump. */
    void collectGarbage()
    {
        for (auto& s : stages)
            s.collectGarbage();

        settleIfDone();

        if (pendingFile.isNotEmpty())
        {
            const auto wanted = pendingFile;
            const auto gain   = pendingGain;
            pendingFile.clear();
            load (wanted, gain);
        }
    }

    bool isReady() const noexcept { return ready.load(); }

    /** How hard to drive the model, for a device with no captured gain axis.

        A capture taken at one setting has no dial of its own — but it still has a nonlinearity, and
        what decides how hard that is hit is how much signal arrives. So the gain knob keeps working:
        where a pack offers positions it SELECTS one, and where it does not it DRIVES. Same knob, same
        gesture, and the block is never left with a control that does nothing. */
    void setDrive (float linearGain) noexcept { drive.store (linearGain); }

    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        if (! ready.load() || numChannels <= 0 || numSamples <= 0)
            return;

        // Phase and live slot read as ONE value. Held apart they can be caught mid-handover — the
        // roles already swapped, the phase not yet — and one block of the wrong model is the click.
        const int   st       = state.load();
        const Phase ph       = phaseOf (st);
        const int   liveSlot = liveOf (st);
        const float d        = drive.load();

        // One model playing. `settled` is a finished handover waiting for the message thread to
        // rename the slots — the sound has already moved, so play what the fade ended on.
        if (ph == Phase::idle || ph == Phase::settled)
        {
            playOne (ph == Phase::settled ? 1 - liveSlot : liveSlot, channels, numChannels, numSamples, d);
            return;
        }

        // A block bigger than we were prepared for has no scratch to warm the newcomer in. Play the
        // one that is already audible and let the handover wait for a block that fits — a stalled
        // fade is a knob that acts late, a fade with nowhere to write is a crash.
        if (numSamples > handover.getNumSamples() || numChannels > handover.getNumChannels())
        {
            playOne (liveSlot, channels, numChannels, numSamples, d);
            return;
        }

        // The newcomer gets its OWN copy of the input at its OWN pre-volume: it is a second device
        // fed the same signal, not a second pass over the first one's output.
        const int spareSlot = 1 - liveSlot;

        for (int ch = 0; ch < numChannels; ++ch)
            juce::FloatVectorOperations::copy (handover.getWritePointer (ch), channels[ch], numSamples);

        playOne (liveSlot,  channels,                          numChannels, numSamples, d);
        playOne (spareSlot, handover.getArrayOfWritePointers(), numChannels, numSamples, d);

        if (ph != Phase::fading)
        {
            // Warming. The newcomer's output is thrown away on purpose — what matters is that its
            // network buffers stop being empty, so that when it does arrive it is already mid-
            // conversation instead of describing the silence it was born into.
            // Subtracted in ONE step, not read-then-written: a knob moved again mid-warm restarts
            // the count from the message thread, and a plain read-modify-write can land on top of
            // that restart with a stale number — which promotes a model that has not warmed at all.
            const int left = warmRemaining.fetch_sub (numSamples) - numSamples;

            if (left <= 0)
            {
                // Only from `warming`: a load that started between blocks holds the phase at
                // `loading`, and promoting under it would fade in a model still being replaced.
                int expected = encode (Phase::warming, liveSlot);
                if (state.compare_exchange_strong (expected, encode (Phase::fading, liveSlot)))
                    fadePos = 0;
            }

            return;
        }

        // The handover itself. LINEAR, not equal-power: the two models are the same device a notch
        // apart, fed the same signal, so their outputs are strongly correlated — and on correlated
        // signals equal-power sums to +3 dB at the midpoint, which is an audible swell on a held
        // note. Sines that add rather than square is the right law here.
        const float inc = 1.0f / (float) fadeSamples;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* out      = channels[ch];
            const float* in = handover.getReadPointer (ch);
            float t         = (float) fadePos * inc;

            for (int i = 0; i < numSamples; ++i, t += inc)
            {
                const float b = juce::jmin (1.0f, t);
                out[i] = out[i] * (1.0f - b) + in[i] * b;
            }
        }

        fadePos += numSamples;

        if (fadePos >= fadeSamples)
            state.store (encode (Phase::settled, liveSlot));
    }

private:
    const namz::rig::Stage* firstNam() const
    {
        if (pack == nullptr)
            return nullptr;

        for (const auto& s : pack->rig.chain)
            if (s.kind == namz::rig::StageKind::Nam)
                return &s;

        return nullptr;
    }

    // Fifty milliseconds of warm covers the couple of thousand samples a WaveNet needs before its
    // output means anything; forty of fade is long enough to hide the seam and short enough that a
    // knob still feels like a knob. Both are paid only while a model is changing.
    static constexpr double warmSeconds = 0.050;
    static constexpr double fadeSeconds = 0.040;

    /** Where a handover is. `loading` is `warming` with the message thread's hand on the spare —
        the audio thread keeps feeding it but must not promote it. `settled` is a finished fade
        whose slots have not been renamed yet; only the message thread does that. */
    enum class Phase { idle, loading, warming, fading, settled };

    // Phase and live slot in one word, so the audio thread reads a pair that agrees with itself.
    static constexpr int   encode  (Phase p, int liveSlot) noexcept { return ((int) p << 1) | liveSlot; }
    static constexpr Phase phaseOf (int s) noexcept                 { return (Phase) (s >> 1); }
    static constexpr int   liveOf  (int s) noexcept                 { return s & 1; }

    /** Hand the roles over once the audio thread has FINISHED a fade — `settled` and nothing else.
        The pump touches this thirty times a second, so a version that settled any phase in flight
        would rename the slots a millisecond into the warm and hard-swap a model that has not played
        a sample yet: the exact click, with a cold model on top of it.

        Message thread only, which is why the audio thread never touches the live slot number and a
        fade can never be pulled out from under itself. Safe as a plain store: nothing writes the
        state while it says `settled`. */
    void settleIfDone() noexcept
    {
        const int st = state.load();
        if (phaseOf (st) == Phase::settled)
            state.store (encode (Phase::idle, 1 - liveOf (st)));
    }

    /** Read a model off disk into one slot. Message thread — it reads a file and decodes. */
    bool loadInto (int slot, const juce::String& wanted, float preGain)
    {
        const auto bytes = device::DeviceLibrary::readBinaryEntry (*pack, wanted);
        if (bytes.getSize() == 0)
            return false;

        if (! stages[(size_t) slot].loadModelFromMemory (bytes.getData(), bytes.getSize()))
            return false;

        slotGain[(size_t) slot].store (preGain);
        return true;
    }

    /** One slot, in place, at its own pre-volume. */
    void playOne (int slot, float* const* channels, int numChannels, int numSamples, float d) noexcept
    {
        if (const float g = slotGain[(size_t) slot].load() * d; ! juce::approximatelyEqual (g, 1.0f))
            for (int ch = 0; ch < numChannels; ++ch)
                juce::FloatVectorOperations::multiply (channels[ch], g, numSamples);

        stages[(size_t) slot].process (channels, numChannels, numSamples, true);
    }

    /** Two of everything, because a model change is a handover and not a swap. */
    std::array<felitronics::nam::NamStage, 2> stages;
    std::array<std::atomic<float>, 2>         slotGain { 1.0f, 1.0f };
    juce::AudioBuffer<float>                  handover;

    std::atomic<int> state         { encode (Phase::idle, 0) };
    std::atomic<int> warmRemaining { 0 };
    int fadePos     = 0;              // audio thread only
    int warmSamples = 2400;
    int fadeSamples = 1920;

    /** The file that is playing, or the one on its way in. */
    juce::String targetFile;

    /** A request that arrived mid-fade, kept for the next pump tick. Last one wins: a dial swept
        across a device is asking for where it STOPPED, not for everything it passed. */
    juce::String pendingFile;
    float        pendingGain = 1.0f;

    const device::DeviceLibrary::Pack* pack = nullptr;
    std::atomic<float> drive { 1.0f };
    namz::rig::Settings settings;
    std::atomic<bool> ready { false };
};

} // namespace orbitamp::core
