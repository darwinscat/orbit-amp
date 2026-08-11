// The bench the crew skipped: REAL nanoseconds per frame through the exact stage the plugin
// runs, on the exact model it ships. Loads a .namz (path as argv[1], or the factory SM7 first
// capture), prepares at 48 kHz / 512, and times mono processing over enough blocks to trust.
//
// Prints ns/block, ns/sample and the share of a 512-sample real-time budget — the same unit the
// footer meter speaks, so the two numbers can look each other in the eye.

#include <felitronics/nam/NamStage.h>
#include <juce_core/juce_core.h>

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
    std::printf ("mean:  %10.0f ns/block  (%.1f ns/sample)\n", perBlock, perBlock / block);
    std::printf ("worst: %10.0f ns/block\n", worstNs);
    std::printf ("share of real-time budget: %.2f%%  (worst %.2f%%)\n",
                 perBlock / budget * 100.0, worstNs / budget * 100.0);

    return 0;
}
