// The bench the crew skipped: REAL nanoseconds per frame through the exact stage the plugin
// runs, on the exact model it ships. Loads a .namz (path as argv[1], or the factory SM7 first
// capture), prepares at 48 kHz / 512, and times mono processing over enough blocks to trust.
//
// Prints ns/block, ns/sample and the share of a 512-sample real-time budget — the same unit the
// footer meter speaks, so the two numbers can look each other in the eye.

#include "../src/core/ScopeTap.h"
#include "../src/core/WaveRibbon.h"

#include <felitronics/nam/NamStage.h>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

int main (int argc, char** argv)
{
    constexpr double rate  = 48000.0;
    const int block = argc > 2 ? juce::jlimit (16, 4096, juce::String (argv[2]).getIntValue())
                               : 512;

    const juce::File file = argc > 1
        ? juce::File (juce::String (argv[1]))
        : juce::File ("~/Library/Application Support/Darwin's Cat/OrbitAmp/Factory/SM7/SM7-0001.namz");

    if (! file.existsAsFile())
    {
        std::printf ("no model at %s\n", file.getFullPathName().toRawUTF8());
        return 1;
    }

    juce::MemoryBlock bytes;
    file.loadFileAsData (bytes);

    felitronics::nam::NamStage stage;
    stage.prepare (rate, block);

    if (! stage.loadModelFromMemory (bytes.getData(), bytes.getSize()))
    {
        std::printf ("model refused: %s\n", file.getFullPathName().toRawUTF8());
        return 1;
    }

    std::printf ("model: %s  (%d bytes)\n", file.getFileName().toRawUTF8(), (int) bytes.getSize());

    std::vector<float> data ((size_t) block);
    float* io[1] = { data.data() };

    std::mt19937 rng (42);
    std::uniform_real_distribution<float> noise (-0.1f, 0.1f);

    const auto refill = [&]
    {
        for (auto& s : data)
            s = noise (rng);
    };

    // Warm the caches and the network's internal buffers before trusting the clock.
    for (int i = 0; i < 200; ++i)
    {
        refill();
        stage.process (io, 1, block, true);
    }

    const int blocks = 2048000 / block;   // ~the same audio length whatever the block
    double totalNs = 0.0, worstNs = 0.0;

    for (int i = 0; i < blocks; ++i)
    {
        refill();

        const auto a = std::chrono::steady_clock::now();
        stage.process (io, 1, block, true);
        const double ns = std::chrono::duration<double, std::nano> (
                              std::chrono::steady_clock::now() - a).count();

        totalNs += ns;
        worstNs = std::max (worstNs, ns);
    }

    const double perBlock = totalNs / blocks;
    const double budget   = block / rate * 1.0e9;

    std::printf ("blocks: %d x %d samples @ %.0f Hz\n", blocks, block, rate);
    std::printf ("stage mean:  %10.0f ns/block  (%.1f ns/sample)\n", perBlock, perBlock / block);
    std::printf ("stage worst: %10.0f ns/block\n", worstNs);
    std::printf ("stage share of budget: %.2f%%  (worst %.2f%%)\n",
                 perBlock / budget * 100.0, worstNs / budget * 100.0);

    // ---- the CARGO the plugin's block adds around the stage, itemised -----------------------
    using orbitamp::core::ScopeTap;
    using orbitamp::core::WaveRibbon;

    juce::AudioBuffer<float> buf (1, block), dry (1, block);

    // Three 512-tap convolvers — the same juce::dsp::Convolution a MeasuredFilter runs, cost
    // identical whatever the taps hold.
    juce::dsp::Convolution fir[3];
    {
        std::mt19937 irRng (7);
        for (auto& f : fir)
        {
            juce::AudioBuffer<float> ir (1, 512);
            for (int i = 0; i < 512; ++i)
                ir.setSample (0, i, noise (irRng));

            f.prepare ({ rate, (juce::uint32) block, 1 });
            f.loadImpulseResponse (std::move (ir), rate,
                                   juce::dsp::Convolution::Stereo::no,
                                   juce::dsp::Convolution::Trim::no,
                                   juce::dsp::Convolution::Normalise::no);
        }
    }

    ScopeTap  scope;
    WaveRibbon ribbon;
    ribbon.prepare (rate);

    const auto timeIt = [&] (const char* name, auto&& body)
    {
        double ns = 0.0;
        for (int i = 0; i < blocks; ++i)
        {
            refill();
            buf.copyFrom (0, 0, data.data(), block);

            const auto a = std::chrono::steady_clock::now();
            body();
            ns += std::chrono::duration<double, std::nano> (
                      std::chrono::steady_clock::now() - a).count();
        }
        std::printf ("%-14s %8.0f ns/block   %.2f%% of budget\n",
                     name, ns / blocks, ns / blocks / budget * 100.0);
    };

    timeIt ("3x FIR",   [&]
    {
        for (auto& f : fir)
        {
            juce::dsp::AudioBlock<float> b (buf);
            f.process (juce::dsp::ProcessContextReplacing<float> (b));
        }
    });
    timeIt ("dry copy", [&] { dry.copyFrom (0, 0, buf, 0, 0, block); });
    timeIt ("scope",    [&] { scope.write (dry.getReadPointer (0), buf.getReadPointer (0), block); });
    timeIt ("ribbon",   [&] { ribbon.write (dry.getReadPointer (0), buf.getReadPointer (0), block); });

    return 0;
}
