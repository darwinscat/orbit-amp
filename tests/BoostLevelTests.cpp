// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for THE BOOST'S LEVEL (params::boostLevel): while an amp's boost is on, the amp leaves that much
// louder — at the end of the chain, after the cabinet and ahead of the master's hand and the limiter:
//
//   - every amp has it, -3 to +9 dB, +3 out of the box;
//   - the boost is what each amp calls one: the playing pack's own Boost switch — OrbitAmp's BOOST key, the
//     rovers' TURBO channels, which hold it on — or the cockpit's TURBO, a push into the capture. OrbitAmp
//     on a pack with no Boost has none, its input trim pushed or not, and an amp out of the chain has none;
//   - with the boost off the level is not heard at all: what leaves is the same, bit for bit, at any level;
//   - with it on, what leaves is what leaves at 0 dB times the level, exactly, once the ramp is done;
//   - throwing the boost, or picking another level, ramps there in a straight line over 10 to 20 ms, never
//     a step;
//   - the limiter after it still holds its ceiling at +9;
//   - a session keeps the level, and so does a rover's preset;
//   - ON A PACK WITH NO BOOST SWITCH — the X88-IR's, in OrbitAmp and the cockpit — the boost is the capture
//     two captured positions up the dial from DRIVE, the knob where it stands, and a step the dial has not
//     got above its top capture fed into it 3 dB harder: every hour of the dial, exactly, in STEP, and the
//     same measure between them in SMOOTH; it plays what DRIVE two hours up plays (fed that much harder
//     through the input); thrown while playing, its push ramps in rather than stepping; off, it is not
//     there at all.
//
// Every comparison plays two processors side by side, fed the same and moved the same, one of them at 0 dB
// — whatever the boost does to the capture itself is in both, and only the level is between them. Out of
// the build's factory folder, so every amp plays its packs. main() returns 0 when every check held.

#include <juce_events/juce_events.h>

#include "AmpChannels.h"
#include "PluginProcessor.h"
#include "channels/Engine.h"
#include "channels/Presets.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace orbitamp;

namespace
{
    int failed = 0;

    void check (bool ok, const juce::String& what)
    {
        std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
        if (! ok)
            ++failed;
    }

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    /** A product as it plays, without its first start: nothing gathered from the player's folders. */
    Product quiet (const Product& p)
    {
        auto q = p;
        q.firstStart = nullptr;
        return q;
    }

    const Product& orbitAmp()    { static const Product p = quiet (products::orbitAmp());    return p; }
    const Product& catpitAmp()   { static const Product p = quiet (products::catpitAmp());   return p; }
    const Product& redPawAmp()   { static const Product p = quiet (products::redPawAmp());   return p; }
    const Product& frostPawAmp() { static const Product p = quiet (products::frostPawAmp()); return p; }
    const Product& amberPawAmp() { static const Product p = quiet (products::amberPawAmp()); return p; }

    std::unique_ptr<AmpProcessor> make (const Product& p)
    {
        auto amp = std::make_unique<AmpProcessor> (p);
        amp->inlineLoads = true;
        amp->prepareToPlay (sampleRate, blockSize);
        amp->pumpDeviceWork();
        return amp;
    }

    float param (AmpProcessor& amp, const juce::String& id)
    {
        auto* p = amp.apvts.getParameter (id);
        return p->convertFrom0to1 (p->getValue());
    }

    void setParam (AmpProcessor& amp, const juce::String& id, float value)
    {
        auto* p = amp.apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        amp.pumpDeviceWork();
    }

    juce::String say (float db) { return (db > 0.0f ? "+" : "") + juce::String (db, 1) + " dB"; }

    /** What leaves, channel 0, for `blocks` blocks of a chord a guitar might play, at `peakDb` — the
        sample count carried on, so a second call goes on where the first stopped. Pumped every block,
        as the timer would. */
    struct Feed
    {
        long long at = 0;
        float peakDb = -12.0f;

        std::vector<float> play (AmpProcessor& amp, int blocks)
        {
            std::vector<float> out;
            juce::AudioBuffer<float> buf (2, blockSize);
            juce::MidiBuffer midi;
            const double a = juce::Decibels::decibelsToGain ((double) peakDb) / 1.75;
            const double tau = 2.0 * juce::MathConstants<double>::pi;

            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < blockSize; ++i, ++at)
                {
                    const double t = (double) at / sampleRate;
                    const auto s = (float) (a * (std::sin (tau * 110.0 * t) + 0.5 * std::sin (tau * 164.8 * t)
                                                 + 0.25 * std::sin (tau * 246.9 * t)));
                    buf.setSample (0, i, s);
                    buf.setSample (1, i, s);
                }
                amp.processBlock (buf, midi);
                amp.pumpDeviceWork();
                out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + blockSize);
            }
            return out;
        }
    };

    /** Two processors of one product, fed the same and moved the same; `level` is the one at 0 dB. */
    struct Pair
    {
        std::unique_ptr<AmpProcessor> level0, level;
        Feed feed0, feed;

        explicit Pair (const Product& p) : level0 (make (p)), level (make (p))
        {
            setParam (*level0, params::boostLevel, 0.0f);
        }

        void both (const std::function<void (AmpProcessor&)>& move) { move (*level0); move (*level); }

        /** What each played, over `blocks` blocks. */
        std::pair<std::vector<float>, std::vector<float>> play (int blocks)
        {
            auto a = feed0.play (*level0, blocks);
            auto b = feed.play (*level, blocks);
            return { std::move (a), std::move (b) };
        }
    };

    bool same (const std::vector<float>& a, const std::vector<float>& b)
    {
        return a.size() == b.size() && std::equal (a.begin(), a.end(), b.begin(),
                                                   [] (float x, float y) { return juce::exactlyEqual (x, y); });
    }

    /** Whether `b` is `a` times `gain` exactly, from sample `from` on — and the signal there not silence. */
    bool scaled (const std::vector<float>& a, const std::vector<float>& b, float gain, size_t from = 0)
    {
        float loudest = 0.0f;
        for (size_t i = from; i < a.size(); ++i)
        {
            if (! juce::exactlyEqual (b[i], a[i] * gain))
                return false;
            loudest = juce::jmax (loudest, std::abs (a[i]));
        }
        return a.size() == b.size() && loudest > 1.0e-3f;
    }

    /** THE RAMP, read off the two signals: the gain between them sample by sample, where the 0 dB one is
        loud enough to divide by. It leaves `fromGain` where the first sample is not that exactly — later
        than the block it was moved in, by as late as the limiter after it makes everything, switched in
        or not — and is done where the rest is `to` exactly. How many samples that took, and whether it
        went in a straight line — every step no bigger than an even ramp's over that length, and never
        past either end. `end` is where it is done, an index into the signals. */
    struct Ramp
    {
        int samples = -1;
        size_t end = 0;
        bool straight = true;
    };

    Ramp rampOf (const std::vector<float>& a, const std::vector<float>& b, float fromGain, float to)
    {
        Ramp r;
        size_t from = 0;
        while (from < a.size() && juce::exactlyEqual (b[from], a[from] * fromGain))
            ++from;
        size_t end = from;
        while (end < a.size() && ! scaled (a, b, to, end))
            ++end;
        if (end >= a.size())
            return r;
        r.samples = (int) (end - from) + 1;     // `end` is the ramp's last sample: the first one exactly there
        r.end = end;

        const float lo = juce::jmin (fromGain, to), hi = juce::jmax (fromGain, to);
        const float step = std::abs (to - fromGain) / (float) r.samples;
        float last = fromGain;
        size_t lastAt = from > 0 ? from - 1 : 0;
        for (size_t i = from; i < end; ++i)
        {
            if (std::abs (a[i]) < 1.0e-2f)
                continue;
            const float g = b[i] / a[i];
            const float allowed = step * (float) (i - lastAt) * 1.02f + 1.0e-5f;
            if (g < lo - 1.0e-5f || g > hi + 1.0e-5f || std::abs (g - last) > allowed)
                r.straight = false;
            last = g;
            lastAt = i;
        }
        return r;
    }

    bool rampFits (const Ramp& r)
    {
        return r.straight && r.samples >= (int) (0.010 * sampleRate) && r.samples <= (int) (0.020 * sampleRate);
    }

    juce::String sayRamp (const Ramp& r)
    {
        return r.samples < 0 ? juce::String ("never got there")
                             : juce::String (r.samples * 1000.0 / sampleRate, 1) + " ms"
                                   + (r.straight ? juce::String (", in a straight line") : juce::String (", NOT in a straight line"));
    }

    float gainOf (float db) { return juce::Decibels::decibelsToGain (db); }

    /** OrbitAmp's first pack that has a Boost switch, and the selector slot it is in; and its first pack
        that has no switch at all — the X88-IR's. Leaves it on the latter. */
    struct OrbitPacks { int boost = -1, slot = -1, plain = -1, plain2 = -1; };

    OrbitPacks findPacks (AmpProcessor& amp)
    {
        OrbitPacks o;
        for (int i = 0; i < amp.preamp.packs.size(); ++i)
        {
            setParam (amp, params::preampDevice, (float) i);
            const auto sel = amp.preamp.selectors();
            if (sel.isEmpty() && o.plain >= 0 && o.plain2 < 0)
                o.plain2 = i;
            if (sel.isEmpty() && o.plain < 0)
                o.plain = i;
            for (int s = 0; s < sel.size() && o.boost < 0; ++s)
                if (sel.getReference (s).name.equalsIgnoreCase ("boost"))
                {
                    o.boost = i;
                    o.slot = s;
                }
        }
        if (o.plain >= 0)
            setParam (amp, params::preampDevice, (float) o.plain);
        return o;
    }
}

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);     // a gate that dies says how far it got
    const juce::ScopedJuceInitialiser_GUI juce;
    std::printf ("The boost's level\n");

    // EVERY AMP HAS IT: -3 to +9 dB, +3 out of the box.
    for (const auto* each : { &orbitAmp(), &catpitAmp(), &redPawAmp(), &frostPawAmp(), &amberPawAmp() })
    {
        auto a = make (*each);
        auto* p = dynamic_cast<juce::RangedAudioParameter*> (a->apvts.getParameter (params::boostLevel));
        check (p != nullptr && juce::exactlyEqual (p->convertFrom0to1 (p->getDefaultValue()), 3.0f)
                   && juce::exactlyEqual (p->getNormalisableRange().start, -3.0f)
                   && juce::exactlyEqual (p->getNormalisableRange().end, 9.0f)
                   && juce::exactlyEqual (param (*a, params::boostLevel), 3.0f),
               juce::String (each->name) + " has the boost's level, -3 to +9 dB, at +3 out of the box");
    }

    // ORBITAMP: the boost is the pack's own switch, the BOOST key.
    {
        auto probe = make (orbitAmp());
        const auto packs = findPacks (*probe);
        check (packs.boost >= 0 && packs.plain >= 0,
               "OrbitAmp's shelf has a pack with a Boost switch (" + juce::String (packs.boost)
                   + ") and one with no switch at all (" + juce::String (packs.plain) + ")");
        if (packs.boost < 0 || packs.plain < 0)
        {
            std::printf ("FAILED\n");
            return failed;
        }
        const auto boostSwitch = params::selectorId (params::preampId, packs.slot);

        // ON A PACK WITH NO BOOST SWITCH the switches' slots thrown are no boost; its BOOST is
        // preamp_boost. On a pack with one, preamp_boost is not read.
        setParam (*probe, params::selectorId (params::preampId, 0), 1.0f);
        setParam (*probe, params::selectorId (params::preampId, 1), 1.0f);
        check (! probe->boostOn(), "OrbitAmp on " + probe->preamp.selectedName()
                                       + ", which has no Boost switch: no boost, its slots thrown");
        setParam (*probe, params::preampBoost, 1.0f);
        check (probe->boostOn(), "...its BOOST, preamp_boost, on: the boost is on");
        setParam (*probe, params::preampDevice, (float) packs.boost);
        setParam (*probe, params::selectorId (params::preampId, packs.slot), 0.0f);
        check (! probe->boostOn(), "...on " + probe->preamp.selectedName() + ", its Boost switch off: no boost, preamp_boost on or not");

        Pair pair (orbitAmp());
        pair.both ([&] (AmpProcessor& a) { setParam (a, params::preampDevice, (float) packs.boost);
                                           setParam (a, params::limiterOn, 0.0f); });
        check (! pair.level->boostOn(), "OrbitAmp on " + pair.level->preamp.selectedName() + ", its Boost off: no boost");

        // OFF: the level is not heard — +9 plays what 0 dB plays, bit for bit.
        setParam (*pair.level, params::boostLevel, 9.0f);
        {
            const auto [a, b] = pair.play (40);
            check (same (a, b), "...its Boost off, the level at +9 plays what it plays at 0 dB, bit for bit");
        }

        // ON at +3: thrown in both, the same block — the capture changes in both, the level in one.
        setParam (*pair.level, params::boostLevel, 3.0f);
        pair.both ([&] (AmpProcessor& a) { setParam (a, boostSwitch, 1.0f); });
        check (pair.level->boostOn() && pair.level0->boostOn(), "...its Boost on, the BOOST key's switch: the boost is on");
        {
            const auto [a, b] = pair.play (40);
            const auto r = rampOf (a, b, 1.0f, gainOf (3.0f));
            check (rampFits (r), "...thrown, the level ramps to +3 dB in " + sayRamp (r));
            check (r.samples >= 0 && scaled (a, b, gainOf (3.0f), r.end),
                   "...and then what leaves is what leaves at 0 dB times +3 dB, exactly");
        }

        // ANOTHER LEVEL, the boost on: from +3 to +9 the same way.
        setParam (*pair.level, params::boostLevel, 9.0f);
        {
            const auto [a, b] = pair.play (20);
            const auto r = rampOf (a, b, gainOf (3.0f), gainOf (9.0f));
            check (rampFits (r) && scaled (a, b, gainOf (9.0f), r.end),
                   "...+9 picked with the boost on: there in " + sayRamp (r) + ", then exactly +9 dB");
        }

        // -3: quieter is a level too.
        setParam (*pair.level, params::boostLevel, -3.0f);
        {
            const auto [a, b] = pair.play (20);
            const auto r = rampOf (a, b, gainOf (9.0f), gainOf (-3.0f));
            check (rampFits (r) && scaled (a, b, gainOf (-3.0f), r.end),
                   "...-3 picked: down in " + sayRamp (r) + ", then exactly -3 dB");
        }

        // OFF AGAIN: back to unity in a ramp, and then bit for bit.
        pair.both ([&] (AmpProcessor& a) { setParam (a, boostSwitch, 0.0f); });
        {
            const auto [a, b] = pair.play (30);
            const auto r = rampOf (a, b, gainOf (-3.0f), 1.0f);
            check (rampFits (r) && r.samples >= 0
                       && same (std::vector<float> (a.begin() + (long) r.end, a.end()), std::vector<float> (b.begin() + (long) r.end, b.end())),
                   "...its Boost off again: back in " + sayRamp (r) + ", then bit for bit what 0 dB plays");
        }

        // THE AMP SWITCHED OFF takes its boost with it.
        pair.both ([&] (AmpProcessor& a) { setParam (a, boostSwitch, 1.0f); });
        setParam (*pair.level, params::preampOn, 0.0f);
        check (! pair.level->boostOn(), "...the AMP switched off, its Boost on: no boost");

        // THE LIMITER still has the last word: +9 on a hot guitar, the limiter as it opens.
        {
            auto hot = make (orbitAmp());
            setParam (*hot, params::preampDevice, (float) packs.boost);
            setParam (*hot, boostSwitch, 1.0f);
            setParam (*hot, params::boostLevel, 9.0f);
            Feed f;
            f.peakDb = 0.0f;
            const auto out = f.play (*hot, 60);
            float peak = 0.0f;
            for (size_t i = out.size() / 2; i < out.size(); ++i)
                peak = juce::jmax (peak, std::abs (out[i]));
            const float ceiling = param (*hot, params::limiterCeiling);
            check (param (*hot, params::limiterOn) > 0.5f && peak <= gainOf (ceiling) * 1.0005f && peak > gainOf (ceiling - 6.0f),
                   "...+9 on a hot guitar, the limiter on at " + say (ceiling) + ": what leaves peaks at "
                       + say (juce::Decibels::gainToDecibels (peak)));
        }

        // A SESSION keeps it.
        {
            auto a = make (orbitAmp());
            setParam (*a, params::boostLevel, 6.0f);
            juce::MemoryBlock m;
            a->getStateInformation (m);
            auto b = make (orbitAmp());
            b->setStateInformation (m.getData(), (int) m.getSize());
            b->pumpDeviceWork();
            check (juce::exactlyEqual (param (*b, params::boostLevel), 6.0f), "an OrbitAmp session keeps the boost's level, +6");
        }
    }

    // THE ROVERS: TURBO is the gear's second channel, the same pack with its Boost held on.
    for (const auto* each : { &redPawAmp(), &frostPawAmp(), &amberPawAmp() })
    {
        const juce::String name = each->name;
        Pair pair (*each);
        pair.both ([] (AmpProcessor& a) { setParam (a, params::limiterOn, 0.0f); });
        setParam (*pair.level, params::boostLevel, 6.0f);
        check (! pair.level->boostOn(), name + ", its first gear: no boost");
        {
            const auto [a, b] = pair.play (20);
            check (same (a, b), "...TURBO off, the level at +6 plays what 0 dB plays, bit for bit");
        }

        pair.both ([] (AmpProcessor& a) { channels::Engine::of (a).go (1); a.pumpDeviceWork(); });
        check (pair.level->boostOn(), "...TURBO, its Boost held on: the boost is on");
        {
            const auto [a, b] = pair.play (40);
            const auto r = rampOf (a, b, 1.0f, gainOf (6.0f));
            check (rampFits (r) && scaled (a, b, gainOf (6.0f), r.end),
                   "...to +6 dB in " + sayRamp (r) + ", then exactly +6 dB");
        }

        pair.both ([] (AmpProcessor& a) { channels::Engine::of (a).go (0); a.pumpDeviceWork(); });
        {
            const auto [a, b] = pair.play (30);
            const auto r = rampOf (a, b, gainOf (6.0f), 1.0f);
            check (! pair.level->boostOn() && rampFits (r) && r.samples >= 0
                       && same (std::vector<float> (a.begin() + (long) r.end, a.end()), std::vector<float> (b.begin() + (long) r.end, b.end())),
                   "...TURBO off again: back in " + sayRamp (r) + ", then bit for bit");
        }
    }

    // A ROVER'S PRESET keeps it.
    {
        auto a = make (redPawAmp());
        auto& engine = channels::Engine::of (*a);
        channels::Presets presets (*a, engine.channels);
        setParam (*a, params::boostLevel, 9.0f);
        const bool wrote = presets.store().write ("Boost Level", a->presetForSaving());
        setParam (*a, params::boostLevel, 0.0f);
        check (wrote && presets.load ("Boost Level") && juce::exactlyEqual (param (*a, params::boostLevel), 9.0f),
               "a RedPawAmp preset keeps the boost's level, +9");
    }

    // THE COCKPIT: the X88-IR's packs have no Boost switch, and its TURBO is preamp_boost.
    {
        Pair pair (catpitAmp());
        pair.both ([] (AmpProcessor& a) { setParam (a, params::limiterOn, 0.0f); });
        check (pair.level->preamp.selectors().isEmpty() && ! pair.level->boostOn(),
               "CatpitAmp on " + pair.level->preamp.selectedName() + ", which has no switch, TURBO off: no boost");
        {
            const auto [a, b] = pair.play (20);
            check (same (a, b), "...TURBO off, the level at +3 plays what 0 dB plays, bit for bit");
        }

        pair.both ([] (AmpProcessor& a) { setParam (a, params::preampBoost, 1.0f); });
        check (pair.level->boostOn(), "...TURBO on: the boost is on");
        {
            const auto [a, b] = pair.play (40);
            const auto r = rampOf (a, b, 1.0f, gainOf (3.0f));
            check (rampFits (r) && scaled (a, b, gainOf (3.0f), r.end),
                   "...to +3 dB in " + sayRamp (r) + ", then exactly +3 dB");
        }

        // ...its capture two positions up the dial, the throttle's DRIVE where it stands.
        auto& c = *pair.level;
        setParam (c, params::blockSmooth (params::preampId), 0.0f);
        setParam (c, params::preampGain, 3.0f);
        check (juce::exactlyEqual (c.preamp.dialDegrees(), 150.0) && juce::exactlyEqual (c.preamp.turboPushDb(), 0.0f),
               "...TURBO at 10:00 plays 12:00");
        setParam (c, params::preampGain, 10.0f);
        check (juce::exactlyEqual (c.preamp.dialDegrees(), 300.0) && juce::exactlyEqual (c.preamp.turboPushDb(), 6.0f),
               "...TURBO at 17:00 plays 17:00 6 dB harder");

        // ...and a channel keeps it: on, the next channel opens with it off, and coming back finds it on.
        auto& engine = channels::Engine::of (c);
        const bool went = engine.go (1);
        c.pumpDeviceWork();
        const bool offThere = param (c, params::preampBoost) < 0.5f && ! c.boostOn();
        const bool back = engine.go (0);
        c.pumpDeviceWork();
        check (went && offThere && back && param (c, params::preampBoost) > 0.5f && c.boostOn(),
               "...TURBO on, the next channel opens with it off, and coming back finds it on");
    }

    // TURBO ON A PACK WITH NO BOOST SWITCH: OrbitAmp on the X88-IR's first.
    {
        auto a = make (orbitAmp());
        const auto packs = findPacks (*a);      // leaves it on the pack with no switch
        juce::StringArray clock;
        for (int deg = 0; deg <= 300; deg += 30)
            clock.add (juce::String (deg));
        check (a->preamp.selectors().isEmpty() && a->preamp.gainPositions() == clock,
               "OrbitAmp on " + a->preamp.selectedName() + ": no switch, its Gain captured at 0, 30 ... 300 degrees, 7:00 to 17:00");

        // STEP, every hour of the dial: where it plays, and how much harder.
        setParam (*a, params::blockSmooth (params::preampId), 0.0f);
        bool off = true, table = true;
        juce::String rows;
        for (int hour = 7; hour <= 17; ++hour)
        {
            setParam (*a, params::preampGain, (float) (hour - 7));
            setParam (*a, params::preampBoost, 0.0f);
            off = off && juce::exactlyEqual (a->preamp.dialDegrees(), (hour - 7) * 30.0)
                      && juce::exactlyEqual (a->preamp.turboPushDb(), 0.0f);

            setParam (*a, params::preampBoost, 1.0f);
            const int plays = juce::jmin (hour + 2, 17);
            const float push = 3.0f * (float) juce::jmax (0, hour + 2 - 17);
            table = table && juce::exactlyEqual (a->preamp.dialDegrees(), (plays - 7) * 30.0)
                          && juce::exactlyEqual (a->preamp.turboPushDb(), push)
                          && juce::exactlyEqual (param (*a, params::preampGain), (float) (hour - 7));
            if (hour <= 9 || hour >= 16)
                rows << hour << ":00 plays " << plays << ":00" << (push > 0.0f ? " +" + juce::String ((int) push) + " dB" : juce::String())
                     << (hour == 9 ? " ... " : hour < 17 ? ", " : "");
        }
        check (off, "...BOOST off: every hour plays where DRIVE stands, nothing pushed");
        check (table, "...BOOST on, STEP, DRIVE left where it stands: " + rows);

        // SMOOTH: the same measure between the captures.
        setParam (*a, params::blockSmooth (params::preampId), 1.0f);
        struct Between { float gain; double deg; float push; const char* says; };
        for (const auto& c : { Between { 9.5f,  300.0, 4.5f, "16:30 plays 17:00 +4.5 dB" },
                               Between { 8.5f,  300.0, 1.5f, "15:30 plays 17:00 +1.5 dB" },
                               Between { 3.25f, 157.5, 0.0f, "10:15 plays 12:15" } })
        {
            setParam (*a, params::preampGain, c.gain);
            check (std::abs (a->preamp.dialDegrees() - c.deg) < 1.0e-3 && std::abs (a->preamp.turboPushDb() - c.push) < 1.0e-4f,
                   "...BOOST on, SMOOTH: " + juce::String (c.says));
        }

        // WHAT IT PLAYS: the boost at an hour is DRIVE two hours up, fed as much harder as the dial is
        // short of — bit for bit where nothing is fed harder; where it is, against the same push through
        // the input trim, which nothing between it and the AMP touches, and which as a parameter stands a
        // few millionths of a decibel off the whole number — so its output lands within a millionth, not
        // on the bit. Each pair moved along the dial alike, then prepared again, so both start their first
        // note where they stand.
        const auto twins = [&] (float drive, float twoUp, float harderDb) -> float     // how far apart, at most
        {
            auto t = make (orbitAmp()), q = make (orbitAmp());
            for (auto* x : { t.get(), q.get() })
            {
                setParam (*x, params::preampDevice, (float) packs.plain);
                setParam (*x, params::limiterOn, 0.0f);
                setParam (*x, params::boostLevel, 0.0f);
                setParam (*x, params::blockSmooth (params::preampId), 0.0f);
                setParam (*x, params::preampGain, drive);
            }
            setParam (*t, params::preampBoost, 1.0f);
            setParam (*q, params::preampGain, twoUp);
            setParam (*q, params::inTrim, harderDb);     // a whole decibel there is exact; the AMP's own input is a tenth off
            t->prepareToPlay (sampleRate, blockSize);
            q->prepareToPlay (sampleRate, blockSize);
            Feed ft, fq;
            const auto vt = ft.play (*t, 30), vq = fq.play (*q, 30);
            float apart = 0.0f;
            for (size_t i = 0; i < vt.size(); ++i)
                apart = juce::jmax (apart, std::abs (vt[i] - vq[i]));
            return apart;
        };
        check (juce::exactlyEqual (twins (0.0f, 2.0f, 0.0f), 0.0f), "...BOOST at 7:00 plays what 9:00 plays, bit for bit");
        const float at16 = twins (9.0f, 10.0f, 3.0f), at17 = twins (10.0f, 10.0f, 6.0f);
        check (at16 < 1.0e-6f, "...BOOST at 16:00 plays what 17:00 fed 3 dB harder plays, " + juce::String (at16) + " apart at most");
        check (at17 < 1.0e-6f, "...BOOST at 17:00 plays what 17:00 fed 6 dB harder plays, " + juce::String (at17) + " apart at most");

        // A CHANNEL KEEPS ITS BOOST, as it keeps a pack's own switch: left on, it is on when the hand comes
        // back; one never visited opens with it off.
        {
            auto c = make (orbitAmp());
            setParam (*c, params::preampDevice, (float) packs.plain);
            setParam (*c, params::preampBoost, 1.0f);
            AmpChannels channels (*c);
            const bool went = channels.play (packs.plain2);
            c->pumpDeviceWork();
            const bool offThere = param (*c, params::preampBoost) < 0.5f;
            const bool back = channels.play (packs.plain);
            c->pumpDeviceWork();
            check (packs.plain2 >= 0 && went && offThere && back && param (*c, params::preampBoost) > 0.5f,
                   "...BOOST on, another channel never visited opens with it off, and coming back finds it on");
        }

        // THROWN WHILE PLAYING, at 16:00: the capture goes a step up the dial the way a hand takes it
        // there, crossfaded by the player, and the push ramps in over 20 ms rather than a block.
        {
            auto b = make (orbitAmp());
            setParam (*b, params::preampDevice, (float) packs.plain);
            setParam (*b, params::blockSmooth (params::preampId), 0.0f);
            setParam (*b, params::preampGain, 9.0f);
            Feed f;
            (void) f.play (*b, 20);
            setParam (*b, params::preampBoost, 1.0f);
            std::vector<float> ramp { b->preamp.turboPushApplied() };
            for (int k = 0; k < 6; ++k)
            {
                (void) f.play (*b, 1);
                ramp.push_back (b->preamp.turboPushApplied());
            }

            const float g3 = gainOf (3.0f);
            check (juce::exactlyEqual (ramp[0], 1.0f) && ramp[1] > 1.0f && ramp[1] < g3
                       && juce::exactlyEqual (ramp[2], g3) && juce::exactlyEqual (ramp.back(), g3),
                   "...thrown at 16:00, the push ramps in over 20 ms: "
                       + juce::String (juce::Decibels::gainToDecibels (ramp[1]), 2) + " dB after the first block of "
                       + juce::String (blockSize) + ", 3 dB after the second");
        }
    }

    std::printf (failed == 0 ? "ALL PASSED\n" : "FAILED\n");
    return failed;
}
