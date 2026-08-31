// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Probe: what does the shipping block actually SEE in each installed pack? Prints per pack what the
// player was handed — the dial and its captured positions, the other selectors, every tone knob and
// the form it came in, whether the models' offsets came with the pack — straight from the same scan
// the plugin runs. Diagnostic, not a gate.

#include "core/CapturedBlock.h"

#include <cmath>
#include <cstdio>

int main()
{
    using namespace orbitamp;
    const juce::ScopedJuceInitialiser_GUI juceInit;

    for (auto slotKind : { device::DeviceLibrary::Slot::pedal, device::DeviceLibrary::Slot::preamp })
    {
        core::CapturedBlock block (slotKind);
        block.prepare (48000.0, 512, 2);   // as the plugin does: a stereo bus, of which MONO mode feeds one plane
        block.rescan (0);

        std::printf ("=== slot %s: %d packs ===\n",
                     slotKind == device::DeviceLibrary::Slot::pedal ? "pedal" : "preamp",
                     (int) block.packs.size());

        for (int i = 0; i < block.packs.size(); ++i)
        {
            block.select (i);
            const auto& pack = block.packs.getReference (i);
            const auto& player = block.player;

            std::printf ("%-22s", pack.displayName().toRawUTF8());

            if (! player.loaded())
            {
                std::printf ("  DID NOT LOAD\n");
                continue;
            }

            std::printf ("  dial=%s/%d° x%d", player.dialName().empty() ? "-" : player.dialName().c_str(),
                         player.dialSweep(), (int) block.gainPositions().size());

            for (const auto& s : block.selectors())
                std::printf ("  [%s x%d]", s.name.toRawUTF8(), (int) s.values.size());

            std::printf ("  tone:");
            for (const auto& t : block.tones())
                std::printf (" %s(%s)", t.name.c_str(),
                             ! t.sections.empty() ? "bands" : t.sweep > 0 ? "curve" : "switch");
            if (block.tones().empty())
                std::printf (" -");

            std::printf ("  files=%d  lag=%s", (int) player.stage().device.files.size(),
                         player.alignmentFromPack() ? "pack" : "none");

            // ...and what it COSTS: one second of mono audio through the block, after its models have
            // landed, as a share of real time on this machine. The number the DSP badge shows is this
            // one plus the scheduler's mood; this one is the block's own.
            {
                juce::AudioBuffer<float> buf (1, 512), dry (1, 512);
                block.setGain (5.0f, true);
                // Long enough for the player's cold threshold to pass: the cost printed is the cost
                // AT REST, which is the cost a settled session pays.
                for (int k = 0; k < 260; ++k) { buf.clear(); block.process (buf, dry); block.pump (nullptr); }

                const auto t0 = juce::Time::getHighResolutionTicks();
                const int blocks = 48000 / 512;
                for (int k = 0; k < blocks; ++k)
                {
                    for (int n = 0; n < 512; ++n)
                        buf.setSample (0, n, 0.2f * (float) std::sin (0.03 * (double) (k * 512 + n)));
                    block.process (buf, dry);
                }
                const double secs = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
                std::printf ("  cost=%.1f%% of real time (one plane of two)\n", 100.0 * secs / ((double) blocks * 512.0 / 48000.0));
            }
        }
    }

    return 0;
}
