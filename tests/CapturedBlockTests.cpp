// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// The captured-block gate: a pack on disk, through the block the plugin plays it with, checked without
// a sound card. Every installed pack sounds; the dial changes the sound and never steps; a position the
// pack linked to its neighbour is fed softer, as its own `input_db` says; a tone knob of either form —
// a measured ladder or bands — reaches the audio, its reference plays flat, and OURS parks it. Skips
// cleanly when no pack is installed.
//
// The player's own law — which captures, the crossfade, the filter design — is checked where it lives
// (felitronics-core's rigplayer tests). What is checked here is the TRANSLATION: a host's 0..10 and 0..1
// into that player, and the pack's bytes into its hands.

#include "core/CapturedBlock.h"

#include <juce_events/juce_events.h>

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int    blockSize  = 512;

    int failures = 0;

    void report (const juce::String& what, bool ok, const juce::String& detail = {})
    {
        std::printf ("%-52s %s  %s\n", what.toRawUTF8(), ok ? "ok  " : "FAIL", detail.toRawUTF8());
        if (! ok)
            ++failures;
    }

    using Block = orbitamp::core::CapturedBlock;

    struct Sine
    {
        double phase = 0.0;

        void fill (juce::AudioBuffer<float>& buf, double hz, float amplitude)
        {
            for (int i = 0; i < buf.getNumSamples(); ++i, ++phase)
                buf.setSample (0, i, amplitude * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                                   * hz * phase / sampleRate));
        }
    };

    /** Plays `seconds` of a sine through the block and returns what came out — after a settle,
        because the player asks for its models on the first block and a host lands them off a timer.
        The pump runs inline here, the way a driver without a message loop runs it. */
    std::vector<float> run (Block& b, double seconds, double hz = 220.0, float amplitude = 0.25f,
                            int settleBlocks = 24)
    {
        juce::AudioBuffer<float> buf (1, blockSize), dry (1, blockSize);
        Sine sine;
        std::vector<float> out;

        const int blocks = (int) std::ceil (seconds * sampleRate / blockSize) + settleBlocks;
        for (int k = 0; k < blocks; ++k)
        {
            sine.fill (buf, hz, amplitude);
            b.process (buf, dry);
            b.pump (nullptr);

            if (k >= settleBlocks)
                out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + blockSize);
        }

        return out;
    }

    double rms (const std::vector<float>& x)
    {
        double sum = 0.0;
        for (const float s : x)
            sum += (double) s * s;
        return std::sqrt (sum / (double) std::max<size_t> (1, x.size()));
    }

    double rms (const float* x, int n)
    {
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
            sum += (double) x[i] * x[i];
        return std::sqrt (sum / std::max (1, n));
    }

    /** How DIFFERENT two outputs are after matching their levels, as a percentage of signal — the
        question for a distortion, whose gain knob changes the SHAPE long before the loudness. */
    double shapeDifference (const std::vector<float>& a, const std::vector<float>& b)
    {
        const double ra = rms (a), rb = rms (b);
        if (ra < 1.0e-9 || rb < 1.0e-9 || a.size() != b.size())
            return 0.0;

        double diff = 0.0;
        for (size_t i = 0; i < a.size(); ++i)
        {
            const double d = a[i] / ra - b[i] / rb;
            diff += d * d;
        }
        return 100.0 * std::sqrt (diff / (double) a.size());
    }

    double toDb (double ratio) { return 20.0 * std::log10 (std::max (1.0e-9, ratio)); }

    /** The panel's 0..10 for a captured position, from its degrees. */
    float gainFor (const namz::rig::Control& dial, const std::string& value)
    {
        const int deg = namz::rig::detail::degrees (value);
        return dial.sweep > 0 && deg >= 0 ? 10.0f * (float) deg / (float) dial.sweep : 5.0f;
    }

    const namz::rig::Control* dialOf (const Block& b)
    {
        if (! b.player.loaded())
            return nullptr;
        for (const auto& c : b.player.stage().device.controls)
            if (c.name == b.player.dialName())
                return &c;
        return nullptr;
    }
}

int main()
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    std::printf ("orbitamp captured-block gate\n\n");

    Block boost (orbitamp::device::DeviceLibrary::Slot::pedal);
    Block preamp (orbitamp::device::DeviceLibrary::Slot::preamp);
    boost.prepare (sampleRate, blockSize, 1);
    preamp.prepare (sampleRate, blockSize, 1);
    boost.rescan (0);
    preamp.rescan (0);

    if (boost.packs.isEmpty() && preamp.packs.isEmpty())
    {
        std::printf ("no packs installed — nothing to check\n");
        return 0;
    }

    // ---- every pack sounds — through the block, off the pump, at the dial's middle ----
    std::array<float, (size_t) Block::numMeasured> midTone {};
    midTone.fill (0.5f);

    for (auto* b : { &boost, &preamp })
        for (int i = 0; i < b->packs.size(); ++i)
        {
            b->select (i);
            b->setGain (5.0f, true);
            b->updateToneIfMoved (midTone);
            const auto out = run (*b, 0.1);
            report ("sounds: " + b->packs.getReference (i).displayName(),
                    b->isReady() && rms (out) > 1.0e-4,
                    juce::String (toDb (rms (out)), 1) + " dBFS");
        }

    // ---- the rest on the richest pack: a captured dial of three or more positions, and as many
    //      of a linked position, a swept tone knob and a tone switch as any pack offers ----
    Block& b = boost.packs.isEmpty() ? preamp : boost;
    int chosen = -1, best = -1;
    for (int i = 0; i < b.packs.size(); ++i)
    {
        b.select (i);
        const auto* d = dialOf (b);
        if (d == nullptr || d->values.size() < 3)
            continue;

        int score = 0;
        for (const auto& f : b.player.stage().device.files) if (f.inputDb < -0.5) { score += 4; break; }
        for (const auto& t : b.tones()) score += t.sweep > 0 ? 2 : 1;
        if (score > best) { best = score; chosen = i; }
    }

    if (chosen < 0)
    {
        std::printf ("\nno pack with a captured dial — the dial checks are skipped\n");
    }
    else
    {
        b.select (chosen);
        const auto* dial = dialOf (b);
        const auto& dev  = b.player.stage().device;
        std::printf ("\ndevice: %s — %s over %d positions\n\n",
                     b.packs.getReference (chosen).displayName().toRawUTF8(),
                     dial->name.c_str(), (int) dial->values.size());

        // the sweep actually changes the sound
        b.setGain (0.0f, false);
        const auto bottom = run (b, 0.1);
        b.setGain (10.0f, false);
        const auto top = run (b, 0.1);
        report ("the dial changes the sound", shapeDifference (bottom, top) > 10.0,
                juce::String (shapeDifference (bottom, top), 1) + "% between the ends");

        // the handover never steps: sweep the dial one position at a time while playing, and the
        // steepest block-to-block level move stays inside what one captured step is worth
        {
            std::vector<double> atRest;
            for (const auto& v : dial->values)
            {
                b.setGain (gainFor (*dial, v), false);
                atRest.push_back (rms (run (b, 0.05)));
            }
            double lo = 1.0e9, hi = 0.0;
            for (const double r : atRest) { lo = std::min (lo, r); hi = std::max (hi, r); }
            const double span    = hi / std::max (1.0e-9, lo);
            const double allowed = std::max (1.25, std::pow (span, 1.0 / (double) (atRest.size() - 1)) * 1.2);

            b.setGain (gainFor (*dial, dial->values.front()), true);
            run (b, 0.1);

            juce::AudioBuffer<float> buf (1, blockSize), dry (1, blockSize);
            Sine sine;
            double steepest = 1.0, last = -1.0;
            std::string farSide;

            for (size_t v = 1; v < dial->values.size(); ++v)
            {
                b.setGain (gainFor (*dial, dial->values[v]), true);
                for (int k = 0; k < 16; ++k)   // 170 ms: the incoming model lands and the mix walks over
                {
                    sine.fill (buf, 220.0, 0.25f);
                    b.process (buf, dry);
                    b.pump (nullptr);
                    const double r = rms (buf.getReadPointer (0), blockSize);
                    if (last > 0.0 && r > 1.0e-6)
                        steepest = std::max (steepest, std::max (r / last, last / r));
                    last = r;
                }
            }
            farSide = b.player.settings().count (dial->name) ? b.player.settings().at (dial->name) : "";

            std::printf ("handover: span %.2fx across the dial, steepest block-to-block %.3fx, allowed %.3fx\n",
                         span, steepest, allowed);
            report ("a model change walks in, never steps", steepest <= allowed,
                    juce::String (steepest, 3) + "x per block against " + juce::String (allowed, 3) + "x");
            report ("...and the far side is the capture it was sent to", farSide == dial->values.back(),
                    juce::String (farSide) + " vs " + juce::String (dial->values.back()));
        }

        // a linked position — a file the pack points two settings at — is fed softer, by its own number
        {
            int linked = -1, own = -1;
            for (size_t i = 0; i < dev.files.size() && linked < 0; ++i)
                if (dev.files[i].inputDb < -0.5)
                    for (size_t j = 0; j < dev.files.size(); ++j)
                        if (j != i && dev.files[j].id == dev.files[i].id && std::abs (dev.files[j].inputDb) < 0.01)
                        { linked = (int) i; own = (int) j; }

            if (linked < 0)
            {
                std::printf ("no linked position in this pack — the input_db check is skipped\n");
            }
            else
            {
                const auto at = [&] (const namz::rig::FileEntry& f)
                {
                    const auto it = f.settings.find (dial->name);
                    return it != f.settings.end() ? it->second : std::string();
                };

                b.setGain (gainFor (*dial, at (dev.files[(size_t) own])), false);
                const auto loud = run (b, 0.1);
                b.setGain (gainFor (*dial, at (dev.files[(size_t) linked])), false);
                const auto soft = run (b, 0.1);

                const double softRms = rms (soft), loudRms = rms (loud);
                std::printf ("link: %s at %s plays %s's model %.1f dB softer in -> out %.1f dB, shape %.1f%% apart\n",
                             dev.files[(size_t) linked].id.c_str(), at (dev.files[(size_t) linked]).c_str(),
                             at (dev.files[(size_t) own]).c_str(), dev.files[(size_t) linked].inputDb,
                             toDb (softRms / loudRms), shapeDifference (soft, loud));

                // Less into a distortion comes out as a different shape, or as less — either is the
                // attenuation ARRIVING; an alias that ignored its input_db would show neither. What
                // it must not do is come out louder — within the half-decibel a clipper's output
                // level wanders with its drive, which is not louder in any sense a hand can hear.
                report ("a linked position is fed softer, as the pack says",
                        (shapeDifference (soft, loud) > 1.0 || softRms < loudRms * 0.9)
                            && toDb (softRms / loudRms) <= 0.5,
                        juce::String (toDb (softRms / loudRms), 1) + " dB");

                // …AND THE METER'S NUMBER HAS TO CARRY IT. `modelFeed` is what the IN wall adds
                // before its green zone means anything: the zone describes the MODEL's door, and on
                // this rung the alias trim is the last gain standing in front of it. The wall used
                // to be told the pack's stated input level and nothing else, so here it was drawing
                // the zone against a number the pack's own attenuation away from the door.
                const auto withComp = b.modelFeed();
                b.setInputTrims (false);
                const auto without = b.modelFeed();
                b.setInputTrims (true);

                const double stated = dev.files[(size_t) linked].inputDb;
                report ("...and the meter's number carries that trim",
                        withComp.single && without.single
                            && std::abs ((withComp.db - without.db) - stated) < 1.0e-9,
                        juce::String (withComp.db - without.db, 2) + " dB of the stated "
                            + juce::String (stated, 2)
                            + (withComp.single && without.single ? "" : " — and it split"));
            }
        }

    }

    // a tone knob of either form reaches the audio, and OURS takes it out — on the pack chosen above,
    // and again on the first pack that ships a knob as BANDS, because the two forms are two paths
    const auto toneCheck = [] (Block& blk, bool wantBands)
    {
            const auto knobs = blk.tones();
            blk.setGain (5.0f, false);

            int knob = -1;
            for (int i = 0; i < (int) knobs.size() && i < Block::numMeasured; ++i)
                if (knobs[(size_t) i].sweep > 0 && (! wantBands || ! knobs[(size_t) i].sections.empty())) { knob = i; break; }

            if (knob < 0)
            {
                std::printf ("no swept tone knob on this device — the tone checks are skipped\n");
            }
            else
            {
                const auto& t = knobs[(size_t) knob];
                const bool bands = ! t.sections.empty();
                std::printf ("tone: %s, %s\n", t.name.c_str(), bands ? "as bands" : "as a measured ladder");

                // where to listen: the frequency the knob promises to move most, inside its trusted
                // band — for a ladder from its own tables, for bands at the first band's corner
                double probeHz = 1000.0;
                if (! bands && t.positions.size() >= 2)
                {
                    const auto grid = felitronics::lineareq::logFreqGrid (t.grid.fLo, t.grid.fHi, t.grid.points);
                    const auto& a = t.positions.front().db;
                    const auto& z = t.positions.back().db;
                    int iLo = 0, iHi = t.grid.points - 1;
                    if (t.trusted.hiIndex > t.trusted.loIndex)
                    {
                        iLo = juce::jlimit (0, t.grid.points - 1, t.trusted.loIndex);
                        iHi = juce::jlimit (0, t.grid.points - 1, t.trusted.hiIndex);
                    }
                    double biggest = -1.0;
                    for (int i = iLo; i <= iHi && i < (int) a.size() && i < (int) z.size() && i < (int) grid.size(); ++i)
                        if (const double d = std::abs (a[(size_t) i] - z[(size_t) i]); d > biggest)
                        { biggest = d; probeHz = grid[(size_t) i]; }
                }
                else if (bands)
                {
                    probeHz = t.sections.front().hz;
                }

                std::array<float, (size_t) Block::numMeasured> values {};
                values.fill (0.5f);

                // the reference plays flat: what the picture says, and what comes out
                double refNorm = 0.5;
                felitronics::rigplayer::toneNorm (t, t.reference, refNorm);
                values[(size_t) knob] = (float) refNorm;
                blk.setRaw (false);
                blk.updateToneIfMoved (values);
                const auto atRef = run (blk, 0.1, probeHz);
                double worst = 0.0;
                for (const double hz : { 100.0, 300.0, 1000.0, 3000.0, 8000.0 })
                    worst = std::max (worst, std::abs (blk.toneDb (hz)));
                report ("the reference draws flat: " + juce::String (t.name), worst < 0.05,
                        juce::String (worst, 3) + " dB");

                // the two ends of the knob: the level at the probe frequency moves
                values[(size_t) knob] = 0.0f;
                blk.updateToneIfMoved (values);
                const auto atMin = run (blk, 0.1, probeHz);
                const double drawMin = blk.toneDb (probeHz);
                values[(size_t) knob] = 1.0f;
                blk.updateToneIfMoved (values);
                const auto atMax = run (blk, 0.1, probeHz);
                const double drawMax = blk.toneDb (probeHz);

                const double playedDb  = toDb (rms (atMax) / std::max (1.0e-9, rms (atMin)));
                const double promisedDb = drawMax - drawMin;
                std::printf ("tone at %.0f Hz: picture %.1f dB end to end, played %.1f dB\n",
                             probeHz, promisedDb, playedDb);

                report ("turning the knob reaches the audio: " + juce::String (t.name),
                        std::abs (playedDb) > 0.5 && std::abs (promisedDb) > 0.5
                            && (playedDb > 0.0) == (promisedDb > 0.0),
                        juce::String (playedDb, 1) + " dB played, " + juce::String (promisedDb, 1) + " dB drawn");

                // OURS: the knob is out of the signal, wherever the slot stands
                blk.setRaw (true);
                const auto parked = run (blk, 0.1, probeHz);
                report ("OURS parks the device's own tone", shapeDifference (parked, atRef) < 2.0
                            && std::abs (toDb (rms (parked) / std::max (1.0e-9, rms (atRef)))) < 0.5,
                        juce::String (toDb (rms (parked) / std::max (1.0e-9, rms (atRef))), 2) + " dB from the reference");
                blk.setRaw (false);
            }
    };

    if (chosen >= 0)
    {
        b.select (chosen);
        std::printf ("\ntone on %s:\n", b.packs.getReference (chosen).displayName().toRawUTF8());
        toneCheck (b, false);
    }

    for (auto* other : { &boost, &preamp })
    {
        bool done = false;
        for (int i = 0; i < other->packs.size() && ! done; ++i)
        {
            other->select (i);
            for (const auto& t : other->tones())
                if (! t.sections.empty() && t.sweep > 0)
                {
                    std::printf ("\ntone as bands on %s:\n", other->packs.getReference (i).displayName().toRawUTF8());
                    toneCheck (*other, true);
                    done = true;
                    break;
                }
        }
        if (done)
            break;
    }

    // ---- THE PACK'S OWN TWO LEVELS, which this repo does not implement and must not undo ----
    //
    // `chain[].input_db` and `chain[].output_db` are applied by felitronics::rigplayer, always, with
    // no switch anywhere. Nothing here had to be written for them — which is exactly why they need a
    // gate here: what arrives through a pin can leave through one, and a single stray setNormalize or
    // a level added a second time in this repo would be invisible until somebody exported a pack and
    // wondered why it sounded different in the plugin.
    {
        std::printf ("\n");
        report ("normalizing is the default, and nothing here turns it off", boost.player.normalize());

        Block& lv = boost.packs.isEmpty() ? preamp : boost;
        if (lv.packs.isEmpty())
        {
            std::printf ("no pack to state a level with — the level checks are skipped\n");
        }
        else
        {
            lv.select (0);
            const auto& pk = lv.packs.getReference (0);
            const auto bytesOfPack = [&pk] (const std::string& id) -> std::vector<std::byte>
            {
                const auto blk = orbitamp::device::DeviceLibrary::readBinaryEntry (pk, juce::String (id));
                const auto* p = static_cast<const std::byte*> (blk.getData());
                return std::vector<std::byte> (p, p + blk.getSize());
            };

            const auto plain = lv.player.stage();
            const double quiet = toDb (rms (run (lv, 0.1)));

            // 1. The pack's OUTPUT level reaches the block's output — the whole point of the release:
            //    a boost that stops cooking the preamp it feeds.
            auto lower = plain;
            lower.outputDb = -6.0;
            lv.player.load (lower, bytesOfPack);
            const double after = toDb (rms (run (lv, 0.1)));
            report ("output_db -6 leaves the block 6 dB down", std::abs ((quiet - after) - 6.0) < 0.35,
                    juce::String (quiet - after, 2) + " dB");

            // 2. …and the meters can say both numbers, which is what the second needle draws.
            report ("the face reads the pack's two levels",
                    std::abs (lv.packOutputDb() + 6.0) < 1.0e-9 && std::abs (lv.packInputDb()) < 1.0e-9,
                    juce::String (lv.packInputDb(), 2) + " / " + juce::String (lv.packOutputDb(), 2) + " dB");

            // 3. The pack's INPUT level lands at the NETWORK's door — asserted as an equivalence, not
            //    as a level: on a captured device this is a drive control, and a compressive pedal
            //    turns 6 dB of input into a fraction of a decibel out (the first pack here does
            //    exactly that: 0.29 dB). So the claim is the one that is true whatever the device
            //    does with it — `input_db` of -6 is the same thing as feeding the block 6 dB less.
            constexpr float minus6 = 0.5011872f;
            auto softer = plain;
            softer.inputDb = -6.0;
            lv.player.load (softer, bytesOfPack);
            const double byPack = toDb (rms (run (lv, 0.1)));
            lv.player.load (plain, bytesOfPack);
            const double byHand = toDb (rms (run (lv, 0.1, 220.0, 0.25f * minus6)));
            report ("input_db -6 is the same as feeding the block 6 dB less",
                    std::abs (byPack - byHand) < 0.35,
                    juce::String (byPack - byHand, 2) + " dB apart");

            // 4. Pack Level Comp is a bypass for the ALIAS trims and for NOTHING else. Both readings
            //    below are taken with it off, so whatever the trims were doing cancels between them
            //    and the only difference left is where the 6 dB came from. If the comp had taken the
            //    pack's own level out with the trims, the first would stand 6 dB above the second.
            lv.setInputTrims (false);
            lv.player.load (softer, bytesOfPack);
            const double offByPack = toDb (rms (run (lv, 0.1)));
            lv.player.load (plain, bytesOfPack);
            const double offByHand = toDb (rms (run (lv, 0.1, 220.0, 0.25f * minus6)));
            report ("Pack Level Comp bypasses the alias trims, never the pack's own level",
                    std::abs (offByPack - offByHand) < 0.35,
                    juce::String (offByPack - offByHand, 2) + " dB apart");
            lv.setInputTrims (true);

            // 5. …AND THE IN WALL'S NUMBER IS THE WHOLE STACK. `modelFeed` is what the meter adds
            //    before its green zone means anything: the zone describes the MODEL's door, and the
            //    wall used to be told the pack's stated input level and nothing else — so on a
            //    linked rung it was drawing the zone against a number the alias trim away from the
            //    door. Asserted as two DIFFERENCES, so whatever else stands in the chain here (the
            //    dial's own extend, a device with no captured axis) cancels between the readings and
            //    only the two gains under test are left.
            lv.setInputTrims (false);
            lv.player.load (plain, bytesOfPack);
            const double bare = lv.modelFeed().db;    // no stated level, no alias trim

            auto stated = plain;
            stated.inputDb = -3.0;
            for (auto& f : stated.device.files)       // every capture, so whichever one sounds says it
                f.inputDb = -6.0;

            lv.player.load (stated, bytesOfPack);
            const auto byLevel = lv.modelFeed();      // the pack's own level, the comp still off
            lv.setInputTrims (true);
            const auto byBoth = lv.modelFeed();       // …and the alias trim on top of it

            report ("the IN wall's number carries the level AND the alias trim",
                    byLevel.single && byBoth.single
                        && std::abs ((byLevel.db - bare) + 3.0) < 1.0e-9
                        && std::abs ((byBoth.db - byLevel.db) + 6.0) < 1.0e-9,
                    juce::String (byLevel.db - bare, 2) + " of the stated -3, "
                        + juce::String (byBoth.db - byLevel.db, 2) + " of the trimmed -6");

            lv.player.load (plain, bytesOfPack);      // …and the device is left as it was found
        }
    }

    std::printf ("\n%s\n", failures == 0 ? "all checks passed" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
