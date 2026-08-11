// Probe: what does the shipping CapturedStage actually SEE in each installed pack?
// Prints per pack: gain axis size, selector controls, first-file resolution — the truth the
// knobs are built from, straight from the same scan the plugin runs.

#include "core/CapturedStage.h"
#include "device/DeviceLibrary.h"

#include <cstdio>

int main()
{
    using namespace orbitamp;

    for (auto slotKind : { device::DeviceLibrary::Slot::pedal, device::DeviceLibrary::Slot::preamp })
    {
        const auto packs = device::DeviceLibrary::scan (slotKind);
        std::printf ("=== slot %s: %d packs ===\n",
                     slotKind == device::DeviceLibrary::Slot::pedal ? "pedal" : "preamp",
                     (int) packs.size());

        for (const auto& pack : packs)
        {
            core::CapturedStage stage;
            stage.setPack (&pack);

            const auto positions = stage.gainPositions();
            const auto sels      = stage.selectors();

            std::printf ("%-24s gainPositions=%2d  selectors=%d",
                         pack.displayName().toRawUTF8(), (int) positions.size(), (int) sels.size());

            for (const auto& s : sels)
                std::printf ("  [%s: %d values]", s.name.toRawUTF8(), (int) s.values.size());

            std::printf ("  hasGainAxis=%s\n", stage.hasGainAxis() ? "yes" : "NO");
        }
    }

    return 0;
}
