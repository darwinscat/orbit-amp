// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for THE GPU'S REFUSALS, WRITTEN DOWN (src/shaders/GpuLog.h):
//
//   - a refusal — a fake one: no GPU is asked here — leaves one line in the family's log, gpu.log in Darwin's
//     Cat's folder: the local date and time, the product, the format and the host, the driver's words for its
//     context ("no context" where there was none), and why, a driver's log of several lines joined into it;
//   - lines written from several threads at once each land whole;
//   - past its cap a log keeps its newest half, from a whole line, and the newest line is the last.
//
// Into the family's log it writes two lines and nothing else — in the home run-gates.sh made for it, which on
// Windows is the runner's own profile — and it takes nothing out of it: the threads and the caps are tried on
// logs of its own in the temporary folder. main() returns 0 when every check held.

#include "shaders/GpuLog.h"

#include <cstdio>
#include <thread>
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

    juce::StringArray linesOf (const juce::File& f)
    {
        juce::StringArray lines;
        lines.addLines (f.loadFileAsString());
        lines.removeEmptyStrings (false);
        return lines;
    }

    /** "YYYY-MM-DD HH:MM:SS  " at the start, as the log dates a line. */
    bool dated (const juce::String& line)
    {
        if (line.length() < 21)
            return false;
        for (int i = 0; i < 19; ++i)
        {
            const auto c = line[i];
            const bool digit = c >= '0' && c <= '9';
            if ((i == 4 || i == 7) ? c != '-' : i == 10 ? c != ' ' : (i == 13 || i == 16) ? c != ':' : ! digit)
                return false;
        }
        return line.substring (19, 21) == "  ";
    }

    /** Lines numbered #0, #1, ... into `log`, each about `width` bytes, under `cap`; false once the log is
        ever larger than its cap allows (a line past half of it). */
    bool fill (const juce::File& log, int count, int width, juce::int64 cap)
    {
        bool held = true;
        for (int i = 0; i < count; ++i)
        {
            gpulog::append (log, "#" + juce::String (i) + " " + juce::String::repeatedString ("x", width), cap);
            held = held && log.getSize() <= cap;
        }
        return held;
    }

    /** Whether the log holds a run of the numbered lines that ends with the newest. */
    bool newestRun (const juce::StringArray& lines, int newest)
    {
        if (lines.isEmpty())
            return false;
        for (int k = 0; k < lines.size(); ++k)
            if (! dated (lines[k]) || ! lines[k].substring (21).startsWith ("#" + juce::String (newest - (lines.size() - 1 - k)) + " "))
                return false;
        return true;
    }
}

int main()
{
    std::printf ("The GPU's log\n");

    // WHERE: beside every product's settings folder, in this run's own home.
    const auto log = gpulog::file();
    std::printf ("      %s\n", log.getFullPathName().toRawUTF8());
    auto root = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    root = root.getChildFile ("Application Support");
   #endif
    check (log == root.getChildFile ("Darwin's Cat").getChildFile ("gpu.log"),
           "the log is gpu.log in Darwin's Cat's folder, where the products' settings folders are");
    check (log.isAChildOf (juce::File::getSpecialLocation (juce::File::userHomeDirectory)), "...in the home this run was given");
    const int before = linesOf (log).size();

    // A REFUSAL, as a windscreen whose world will not build writes it.
    gpulog::refusal ("RedPawAmp  VST3 in Steinberg Cubase", "Fake Renderer / Fake Vendor / 4.1 Fake 1.0",
                     "Plains: the terrain shader did not build — fragment: ERROR: 0:12: 'height' : undeclared\n"
                     "ERROR: 0:13: '' : compilation terminated\n");
    const auto one = linesOf (log);
    check (log.existsAsFile() && one.size() == before + 1, "a refusal leaves one line in the log");
    const auto line = one[one.size() - 1];
    std::printf ("      %s\n", line.toRawUTF8());
    check (dated (line), "...dated with the local date and time");
    check (line.contains ("RedPawAmp  VST3 in Steinberg Cubase"), "...naming the product, the format and the host");
    check (line.contains ("GL: Fake Renderer / Fake Vendor / 4.1 Fake 1.0"), "...and the context's renderer, vendor and version");
    check (line.contains ("Plains: the terrain shader did not build") && line.contains ("undeclared / ERROR: 0:13"),
           "...and why, the driver's lines joined on it");

    gpulog::refusal ("CatpitAmp  Standalone", {}, "the sky: its shader did not build — link: error");
    const auto two = linesOf (log);
    check (two.size() == before + 2 && two[two.size() - 2] == line
               && two[two.size() - 1].contains ("CatpitAmp  Standalone  |  GL: no context  |  the sky:"),
           "a second refusal goes under the first, and one with no context says so");

    const auto scratch = [] (const char* name)
    {
        auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
        f.deleteFile();
        return f;
    };

    // FROM SEVERAL GL THREADS AT ONCE: every line lands, whole.
    {
        const auto threaded = scratch ("gpu-log-gate-threads.log");
        std::vector<std::thread> threads;
        for (int t = 0; t < 4; ++t)
            threads.emplace_back ([t, &threaded]
            {
                for (int i = 0; i < 50; ++i)
                    gpulog::append (threaded, gpulog::line ("thread " + juce::String (t), "GL", ("refusal " + juce::String (i) + " of 50").toStdString()));
            });
        for (auto& t : threads)
            t.join();

        const auto all = linesOf (threaded);
        int whole = 0;
        for (const auto& l : all)
            whole += dated (l) && l.contains ("  |  GL: GL  |  refusal ") && l.endsWith (" of 50") ? 1 : 0;
        check (all.size() == 200 && whole == 200, "two hundred lines from four threads at once land whole: " + juce::String (whole));
        threaded.deleteFile();
    }

    // THE CAP: past it, the newest half, from a whole line.
    {
        const auto small = scratch ("gpu-log-gate-small.log");
        constexpr juce::int64 cap = 4096;
        check (fill (small, 300, 90, cap), "a log capped at 4 KB never grows past it, three hundred lines on");
        const auto kept = linesOf (small);
        check (small.getSize() >= cap / 2 - 200 && newestRun (kept, 299),
               "...and keeps its newest half, whole lines ending with the newest: " + juce::String (kept.size()) + " lines, "
                   + juce::String (small.getSize()) + " bytes");
        small.deleteFile();

        const auto big = scratch ("gpu-log-gate-big.log");
        check (fill (big, 3000, 110, gpulog::capBytes) && newestRun (linesOf (big), 2999) && big.getSize() >= gpulog::capBytes / 2,
               "past the family's own cap, " + juce::String (gpulog::capBytes / 1024) + " KB, a log keeps its newest half: "
                   + juce::String (big.getSize()) + " bytes");
        big.deleteFile();
    }

    std::printf (failed == 0 ? "ALL PASSED\n" : "FAILED\n");
    return failed;
}
