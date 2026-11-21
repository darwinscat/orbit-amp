// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for OrbitAmp's DRIVE (params::driveOn and its block, core/DrivePedal.h): a modest pedal before the AMP.
//
//   - NOT OFFERED, OrbitAmp plays what it played before the block existed, bit for bit on the compiler that recorded
//     it (tests/golden/orbitamp-before-drive.f32, written by the tree before the block), and so does a session saved by
//     that tree (tests/golden/orbitamp-before-drive.state) — every parameter it had comes back as it was;
//   - no other amp has a parameter of the block's;
//   - offered (its checklist, or SETUP — the same parameter), it stands in the strip before the AMP, its arrow its
//     STANDBY/ON, and it plays; out, it is not run at all;
//   - a session keeps its type, GAIN and the type's EQ.
//
// ORBITAMP_DRIVE_RECORD=<folder> writes the two golden files instead of checking them — run by the tree BEFORE the
// block, never by the tree under test. ORBITAMP_DRIVE_LISTEN=<riff.wav> with ORBITAMP_DRIVE_LISTEN_OUT=<folder> renders
// the riff through a fresh OrbitAmp — the block off, then each type at GAIN noon — for ears, and checks nothing.
// main() returns 0 when every check held.

#include "GateMain.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/DriveBlock.h"
#include "ui/SettingsList.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#ifndef ORBITAMP_DRIVE_GOLDEN
 #error "the build says where the golden files are (CMakeLists.txt: gate_in_binary(drive ...))"
#endif

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
    constexpr int goldenBlocks = 32;

    /** A product as it plays, without its first start: nothing gathered from the player's folders. */
    Product quiet (const Product& p)
    {
        auto q = p;
        q.firstStart = nullptr;
        return q;
    }

    const Product& orbitAmp() { static const Product p = quiet (products::orbitAmp()); return p; }

    std::unique_ptr<AmpProcessor> make (const Product& p)
    {
        auto amp = std::make_unique<AmpProcessor> (p);
        amp->inlineLoads = true;
        amp->prepareToPlay (sampleRate, blockSize);
        amp->pumpDeviceWork();
        return amp;
    }

    /** A guitar's phrase, the same every run: a plucked chord every quarter of a second, each string's partials dying
        away, peaking near -12 dBFS. */
    float phrase (long long n)
    {
        const double t = (double) n / sampleRate;
        const double pluck = std::fmod (t, 0.25);
        const int chord = (int) (t / 0.25) % 4;
        static constexpr double roots[4][3] = { { 82.41, 123.47, 164.81 }, { 110.0, 164.81, 220.0 },
                                                { 98.0, 146.83, 196.0 }, { 73.42, 110.0, 146.83 } };
        double s = 0.0;
        for (const double f : roots[chord])
            for (int h = 1; h <= 6; ++h)
                s += std::exp (-pluck * (4.0 + 3.0 * h)) / (double) h
                     * std::sin (juce::MathConstants<double>::twoPi * f * h * pluck);
        return (float) (0.08 * s);
    }

    /** What leaves, channel 0, for `blocks` blocks of the phrase — pumped every block, as the timer would. */
    std::vector<float> play (AmpProcessor& amp, int blocks, long long& at)
    {
        std::vector<float> out;
        juce::AudioBuffer<float> buf (2, blockSize);
        juce::MidiBuffer midi;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < blockSize; ++i, ++at)
            {
                buf.setSample (0, i, phrase (at));
                buf.setSample (1, i, phrase (at));
            }
            amp.processBlock (buf, midi);
            amp.pumpDeviceWork();
            out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + blockSize);
        }
        return out;
    }

    std::vector<float> play (AmpProcessor& amp, int blocks)
    {
        long long at = 0;
        return play (amp, blocks, at);
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

    const char* const driveIds[] = { params::drivePresent, params::driveOn, params::driveType, params::driveGain,
                                     params::driveTone, params::driveAttack, params::driveBright, params::driveTight };

    void walk (juce::Component& c, const std::function<void (juce::Component&)>& f)
    {
        f (c);
        for (auto* child : c.getChildren())
            walk (*child, f);
    }

    bool shownIn (juce::Component& c, juce::Component& top)
    {
        for (auto* p = &c; p != nullptr && p != &top; p = p->getParentComponent())
            if (! p->isVisible())
                return false;
        return true;
    }

    juce::File golden (const char* ext) { return juce::File (ORBITAMP_DRIVE_GOLDEN).withFileExtension (ext); }

    /** What the tree before the block played, and whether this binary is the compiler that recorded it. */
    struct Before
    {
        std::vector<float> played;
        juce::MemoryBlock state;
        bool recorder = false;
    };

    Before before()
    {
        Before b;
        juce::MemoryBlock bytes;
        if (golden ("f32").loadFileAsData (bytes))
        {
            juce::MemoryInputStream in (bytes, false);
            while (! in.isExhausted())
                b.played.push_back (in.readFloat());
        }
        (void) golden ("state").loadFileAsData (b.state);
       #if defined (__APPLE__) && defined (__aarch64__)
        b.recorder = golden ("compiler").loadFileAsString().trim() == juce::String (__VERSION__);
       #endif
        return b;
    }

    /** Bit for bit on the recording compiler; within 1e-5 (-100 dB) elsewhere, whose libm and vector code may move a
        last bit — NaN when the lengths differ or a sample is not finite. */
    float apart (const std::vector<float>& a, const std::vector<float>& b)
    {
        if (a.size() != b.size() || a.empty())
            return std::numeric_limits<float>::quiet_NaN();
        float worst = 0.0f;
        for (size_t i = 0; i < a.size(); ++i)
        {
            if (! std::isfinite (a[i]) || ! std::isfinite (b[i]))
                return std::numeric_limits<float>::quiet_NaN();
            worst = std::max (worst, std::abs (a[i] - b[i]));
        }
        return worst;
    }

    bool matches (const Before& was, const std::vector<float>& now, const juce::String& what)
    {
        const float d = apart (was.played, now);
        const float allowed = was.recorder ? 0.0f : 1.0e-5f;
        check (std::isfinite (d) && d <= allowed,
               what + " (" + (was.recorder ? "bit for bit, the recording compiler" : "within 1e-5, another compiler")
                   + ": " + juce::String (d, 9) + " apart)");
        return std::isfinite (d) && d <= allowed;
    }

    /** The tree before the block writes what it plays and what it saves. */
    int record (const juce::File& folder)
    {
        auto amp = make (orbitAmp());
        const auto played = play (*amp, goldenBlocks);
        juce::MemoryOutputStream out;
        for (const float s : played)
            out.writeFloat (s);
        juce::MemoryBlock state;
        amp->getStateInformation (state);

        (void) folder.createDirectory();
        const auto name = juce::File (ORBITAMP_DRIVE_GOLDEN).getFileName();
        const bool ok = folder.getChildFile (name).withFileExtension ("f32").replaceWithData (out.getData(), out.getDataSize())
                     && folder.getChildFile (name).withFileExtension ("state").replaceWithData (state.getData(), state.getSize())
                     && folder.getChildFile (name).withFileExtension ("compiler").replaceWithText (juce::String (__VERSION__) + "\n");
        float peak = 0.0f;
        for (const float s : played)
            peak = std::max (peak, std::abs (s));
        std::printf ("%s %d samples (peak %.4f) and %d bytes of state into %s\n", ok ? "recorded" : "COULD NOT RECORD",
                     (int) played.size(), (double) peak, (int) state.getSize(), folder.getFullPathName().toRawUTF8());
        return ok && peak > 1.0e-3f ? 0 : 1;
    }
}

namespace
{
    /** For ears: the riff through a fresh OrbitAmp at its own rate — a second of silence first, for the AMP's model to
        land and warm, cut off again — the block off, then each type at GAIN noon, its EQ as it comes. */
    int listen (const juce::File& riffFile, const juce::File& folder)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (riffFile));
        if (reader == nullptr)
        {
            std::printf ("COULD NOT READ %s\n", riffFile.getFullPathName().toRawUTF8());
            return 1;
        }
        const double rate = reader->sampleRate;
        const int len = (int) reader->lengthInSamples;
        juce::AudioBuffer<float> riff (1, len);
        reader->read (&riff, 0, len, 0, true, false);
        (void) folder.createDirectory();

        // One take: a fresh OrbitAmp, the block off (type < 0) or the type at GAIN noon with the clip law `clip`.
        const auto render = [&] (int type, int clip)
        {
            auto amp = std::make_unique<AmpProcessor> (orbitAmp());
            amp->inlineLoads = true;
            if (type >= 0)
            {
                setParam (*amp, params::drivePresent, 1.0f);
                setParam (*amp, params::driveType, (float) type);
                setParam (*amp, params::driveGain, 5.0f);
                setParam (*amp, params::driveClipTmp, (float) clip);
            }
            amp->prepareToPlay (rate, blockSize);
            amp->pumpDeviceWork();
            const int lead = (int) rate, tail = 2 * (int) rate, total = lead + len + tail;
            juce::AudioBuffer<float> out (2, len + tail), buf (2, blockSize);
            juce::MidiBuffer midi;
            for (int at = 0; at < total; at += blockSize)
            {
                const int n = std::min (blockSize, total - at);
                buf.setSize (2, n, false, false, true);
                for (int i = 0; i < n; ++i)
                {
                    const int k = at + i - lead;
                    const float x = k >= 0 && k < len ? riff.getSample (0, k) : 0.0f;
                    buf.setSample (0, i, x);
                    buf.setSample (1, i, x);
                }
                amp->processBlock (buf, midi);
                amp->pumpDeviceWork();
                for (int i = 0; i < n; ++i)
                    if (const int k = at + i - lead; k >= 0)
                        for (int ch = 0; ch < 2; ++ch)
                            out.setSample (ch, k, buf.getSample (ch, i));
            }
            return out;
        };
        const auto write = [&] (const juce::File& file, const juce::AudioBuffer<float>& out)
        {
            (void) file.getParentDirectory().createDirectory();
            (void) file.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
            std::unique_ptr<juce::AudioFormatWriter> writer (stream == nullptr ? nullptr : wav.createWriterFor (stream.get(), rate, 2, 24, {}, 0));
            if (writer == nullptr)
                return false;
            (void) stream.release();
            writer->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
            std::printf ("%s: peak %.1f dBFS, RMS %.1f dBFS\n", file.getFileName().toRawUTF8(),
                         (double) juce::Decibels::gainToDecibels (out.getMagnitude (0, 0, out.getNumSamples())),
                         (double) juce::Decibels::gainToDecibels (out.getRMSLevel (0, 0, out.getNumSamples())));
            return true;
        };

        const std::pair<const char*, int> takes[] = { { "0-block-off", -1 }, { "1-ts-noon", 0 }, { "2-precision-noon", 1 },
                                                      { "3-33-noon", 2 }, { "4-sd1-noon", 3 } };
        for (const auto& [name, type] : takes)
            if (! write (folder.getChildFile (juce::String (name) + ".wav"), render (type, 0)))
                return 1;

        // TEMPORARY: TS at GAIN noon under each clip law, levels matched to DIODE's RMS.
        float diodeRms = 0.0f;
        for (int clip = 0; clip < params::driveClips.size(); ++clip)
        {
            auto out = render (0, clip);
            const float rms = out.getRMSLevel (0, 0, out.getNumSamples());
            if (clip == 0)
                diodeRms = rms;
            else if (rms > 0.0f)
                out.applyGain (diodeRms / rms);
            if (! write (folder.getChildFile ("clippers").getChildFile ("ts-noon-" + juce::String (clip + 1) + "-"
                                                                          + params::driveClips[clip].toLowerCase() + ".wav"), out))
                return 1;
        }
        return 0;
    }
}

int ORBITAMP_GATE_MAIN()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);     // a gate that dies says how far it got
    const juce::ScopedJuceInitialiser_GUI juce;

    if (const auto folder = juce::SystemStats::getEnvironmentVariable ("ORBITAMP_DRIVE_RECORD", {}); folder.isNotEmpty())
        return record (juce::File (folder));
    if (const auto riff = juce::SystemStats::getEnvironmentVariable ("ORBITAMP_DRIVE_LISTEN", {}); riff.isNotEmpty())
        return listen (juce::File (riff), juce::File (juce::SystemStats::getEnvironmentVariable ("ORBITAMP_DRIVE_LISTEN_OUT", {})));

    std::printf ("OrbitAmp's DRIVE\n");
    const auto was = before();
    check (was.played.size() == (size_t) (goldenBlocks * blockSize) && was.state.getSize() > 0,
           "the tree before the block left what it played and what it saved (tests/golden/orbitamp-before-drive.*)");

    // NOT OFFERED: what OrbitAmp played before the block, fresh and from a session of then.
    {
        auto amp = make (orbitAmp());
        matches (was, play (*amp, goldenBlocks), "a fresh OrbitAmp plays what it played before the block");
    }
    {
        auto amp = make (orbitAmp());
        amp->setStateInformation (was.state.getData(), (int) was.state.getSize());
        amp->pumpDeviceWork();
        amp->prepareToPlay (sampleRate, blockSize);
        amp->pumpDeviceWork();
        matches (was, play (*amp, goldenBlocks), "OrbitAmp on a session saved before the block plays what it played then");
    }

    // NO OTHER AMP HAS IT: not a parameter of the block's, not a place in its chain.
    {
        juce::StringArray had;
        for (const auto* each : { &products::catpitAmp(), &products::redPawAmp(), &products::frostPawAmp(), &products::amberPawAmp(),
                                  &products::orbitBass(), &products::redGrooveBass(), &products::deepGrooveBass() })
        {
            auto amp = make (quiet (*each));
            bool any = amp->chainHolds (params::rowDrive);
            for (const char* id : driveIds)
                any = any || amp->apvts.getParameter (id) != nullptr;
            for (auto* p : amp->getParameters())
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p); r != nullptr && r->getParameterID().startsWith ("drive"))
                    any = true;
            if (any)
                had.add (each->name);
        }
        check (had.isEmpty(), "no other amp has the block: no drive_ parameter, no DRIVE in its chain" + (had.isEmpty() ? juce::String() : " — " + had.joinIntoString (", ")));
    }

    // ORBITAMP: out of the strip out of the box, on and on TS at noon once there.
    {
        auto amp = make (orbitAmp());
        check (amp->chainHolds (params::rowDrive) && amp->linkRemovable (params::rowDrive) && ! amp->linkShown (params::rowDrive)
                   && param (*amp, params::driveOn) > 0.5f && juce::roundToInt (param (*amp, params::driveType)) == 0
                   && juce::exactlyEqual (param (*amp, params::driveGain), 5.0f),
               "OrbitAmp holds the DRIVE, out of its strip out of the box — ON, TS, GAIN 5 for when it comes in");
        check (juce::String (params::chainLinks[(size_t) params::rowDrive].onParam) == params::driveOn
                   && params::rowDrive + 1 == params::rowPreamp && params::rowDrive > params::rowComp,
               "in the chain after COMP, right before the AMP, its arrow writing drive_on");
    }

    // A DRIVE IN STANDBY is not run, and is as long as when it plays: offered and switched off, OrbitAmp plays what it
    // plays without it, 63 samples later — and tells the host so; out of the strip again, it does not. The GATE, the
    // REVERB and the LIMITER are off in both: the gate keys off the raw guitar ahead of the chain, the room's modulation
    // runs on the clock and the limiter on its grid, so with them on the two are not one signal shifted. Read from
    // the moment the AMP's model has landed and warmed (before it OrbitAmp is silent either way).
    {
        auto plain = make (orbitAmp());
        auto amp = make (orbitAmp());
        const int before = amp->getLatencySamples();
        for (auto* a : { plain.get(), amp.get() })
            for (const char* id : { params::gateOn, params::reverbOn, params::limiterOn })
                setParam (*a, id, 0.0f);
        setParam (*amp, params::drivePresent, 1.0f);
        setParam (*amp, params::driveOn, 0.0f);
        for (auto* a : { plain.get(), amp.get() })
        {
            a->prepareToPlay (sampleRate, blockSize);
            a->pumpDeviceWork();
        }
        const int offered = amp->getLatencySamples();
        const auto without = play (*plain, 3 * goldenBlocks);
        const auto now = play (*amp, 3 * goldenBlocks);
        const int lat = core::DrivePedal::latencySamples();
        size_t from = 0;
        while (from < without.size() && std::abs (without[from]) < 1.0e-3f)
            ++from;
        from += 9600;   // the model's warm and its fade in (core::CapturedBlock) are on the clock too
        float d = 0.0f, loud = 0.0f;
        for (size_t i = from; i + (size_t) lat < now.size(); ++i)
        {
            d = std::max (d, std::abs (now[i + (size_t) lat] - without[i]));
            loud = std::max (loud, std::abs (without[i]));
        }
        check (from < now.size() / 2 && loud > 0.05f && d < 1.0e-5f,
               "offered and in STANDBY: what OrbitAmp plays without it, 63 samples later (" + juce::String (d, 9) + " apart over "
                   + juce::String ((int) (now.size() - from)) + " samples)");
        setParam (*amp, params::drivePresent, 0.0f);
        check (offered == before + lat && amp->getLatencySamples() == before,
               "...the host told 63 samples more while it stands in the strip, and back when it leaves ("
                   + juce::String (before) + ", " + juce::String (offered) + ", " + juce::String (amp->getLatencySamples()) + ")");
    }

    // OFFERED AND ON, every type plays — something else than before, and nothing that is not a number.
    for (int t = 0; t < params::driveTypes.size(); ++t)
    {
        auto amp = make (orbitAmp());
        setParam (*amp, params::drivePresent, 1.0f);
        setParam (*amp, params::driveType, (float) t);
        amp->prepareToPlay (sampleRate, blockSize);
        amp->pumpDeviceWork();
        const auto now = play (*amp, goldenBlocks);
        float apartFrom = 0.0f;
        bool finite = true;
        for (size_t i = 0; i < now.size() && i < was.played.size(); ++i)
        {
            finite = finite && std::isfinite (now[i]);
            apartFrom = std::max (apartFrom, std::abs (now[i] - was.played[i]));
        }
        check (finite && apartFrom > 1.0e-3f, "offered, " + params::driveTypes[t] + " plays: the AMP hears the pedal ("
                                                   + juce::String (apartFrom, 4) + " apart from before)");
    }

    // A SESSION KEEPS IT: its place, its switch, its type, GAIN and every type's EQ.
    {
        auto a = make (orbitAmp());
        const std::pair<const char*, float> set[] = { { params::drivePresent, 1.0f }, { params::driveOn, 0.0f },
                                                      { params::driveType, 2.0f }, { params::driveGain, 7.3f },
                                                      { params::driveTone, 2.2f }, { params::driveAttack, 9.0f },
                                                      { params::driveBright, 1.5f }, { params::driveTight, 8.1f } };
        for (const auto& [id, v] : set)
            setParam (*a, id, v);
        juce::MemoryBlock m;
        a->getStateInformation (m);
        auto b = make (orbitAmp());
        b->setStateInformation (m.getData(), (int) m.getSize());
        b->pumpDeviceWork();
        bool kept = true;
        juce::String said;
        for (const auto& [id, v] : set)
        {
            const float back = param (*b, id);
            kept = kept && std::abs (back - v) < 0.006f;
            said << id << " " << juce::String (back, 2) << "  ";
        }
        check (kept, "a session keeps the DRIVE: " + said);
    }

    // OFFERED IN SETUP: its PEDALS page's DRIVE row, clicked, puts it in the strip — its arrow there, its tile on the face,
    // the window wider by it — as one undo step; the checklist lists it beside COMP.
    {
        auto amp = make (orbitAmp());
        std::unique_ptr<juce::AudioProcessorEditor> ed (amp->createEditor());
        auto* editor = dynamic_cast<AmpEditor*> (ed.get());
        const SettingsList::Row* row = nullptr;
        DriveBlock* tile = nullptr;
        if (editor != nullptr)
            walk (*editor, [&] (juce::Component& c)
                  {
                      if (auto* list = dynamic_cast<SettingsList*> (&c))
                          for (const auto& r : list->shownRows())
                              if (r.name == "DRIVE")
                                  row = &r;
                      if (auto* d = dynamic_cast<DriveBlock*> (&c))
                          tile = d;
                  });
        check (row != nullptr && tile != nullptr, "OrbitAmp's SETUP has a DRIVE row, and its face a DRIVE tile");
        if (editor != nullptr && row != nullptr && tile != nullptr)
        {
            const auto stripHas = [&]
            {
                for (const auto& r : AmpEditor::stripRows (*amp))
                    if (r.name == juce::String ("DRIVE"))
                        return r.present;
                return false;
            };
            const int least0 = editor->leastDesignWidth();
            const bool before = ! stripHas() && ! shownIn (*tile, *editor) && ! row->get();
            row->set (true);
            amp->pumpDeviceWork();
            const bool after = amp->linkShown (params::rowDrive) && stripHas() && shownIn (*tile, *editor) && row->get()
                            && editor->leastDesignWidth() == least0 + FaceplateView::driveWidth + FaceplateView::colGap;
            const auto knobs = tile->shownKnobs().joinIntoString (" ");
            setParam (*amp, params::driveType, 1.0f);
            const auto precision = tile->shownKnobs().joinIntoString (" ");
            setParam (*amp, params::driveType, 2.0f);
            const auto thirtyThree = tile->shownKnobs().joinIntoString (" ");
            setParam (*amp, params::driveType, 0.0f);
            juce::String list;
            for (const auto& [r, in] : editor->checklistRows())
                list << params::chainLinks[(size_t) r].name << (in ? "+ " : "- ");
            amp->history.undo();
            amp->pumpDeviceWork();
            const bool undone = ! amp->linkShown (params::rowDrive) && ! stripHas() && ! shownIn (*tile, *editor)
                             && editor->leastDesignWidth() == least0;
            check (before && after && undone && knobs == "GAIN TONE" && precision == "GAIN ATTACK BRIGHT" && thirtyThree == "GAIN TIGHT"
                       && list == "COMP- DRIVE+ ",
                   "SETUP's DRIVE row puts it in the strip and on the face, the window wider by it, one undo step back ("
                       + knobs + " | " + precision + " | " + thirtyThree + "; the checklist " + list + ")");
        }
    }

    std::printf ("%s\n", failed == 0 ? "PASSED" : "FAILED");
    return failed == 0 ? 0 : 1;
}
