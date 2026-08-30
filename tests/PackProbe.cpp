// Probe: what does the shipping block actually SEE in each installed pack? Prints per pack what the
// player was handed — the dial and its captured positions, the other selectors, every tone knob and
// the form it came in, whether the models' offsets came with the pack — straight from the same scan
// the plugin runs. Diagnostic, not a gate.

#include "core/CapturedBlock.h"

#include <cstdio>

int main()
{
    using namespace orbitamp;
    const juce::ScopedJuceInitialiser_GUI juceInit;

    for (auto slotKind : { device::DeviceLibrary::Slot::pedal, device::DeviceLibrary::Slot::preamp })
    {
        core::CapturedBlock block (slotKind);
        block.prepare (48000.0, 512, 1);
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

            std::printf ("  files=%d  lag=%s\n", (int) player.stage().device.files.size(),
                         player.alignmentFromPack() ? "pack" : "none");
        }
    }

    return 0;
}
