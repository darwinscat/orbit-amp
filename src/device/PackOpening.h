// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "DeviceLibrary.h"
#include "../Parameters.h"

#include <felitronics/rigplayer/ToneKnobs.h>

#include <array>

namespace orbitamp::device
{

/** WHERE A PACK SAYS A PLAYER STARTS: its tone knobs, as the values of the captured block's measured
    slots, and its switches, as positions of the selector slots — read from the manifest alone, so it
    is known before the pack is loaded. A slot the pack does not claim stands in the middle, or on
    its first position.

    One reading for every place that needs it: a fresh instance's defaults (Parameters.cpp) and a
    channel the hand has never been on (AmpChannels.h). Two readers starting the same pack at
    different positions would make it two products. */
struct PackOpening
{
    std::array<float, (size_t) params::preampNumMeasured> meas;
    std::array<int,   (size_t) params::numSelectors>      sel {};

    PackOpening() { meas.fill (0.5f); }

    static PackOpening of (const DeviceLibrary::Pack& pack)
    {
        PackOpening o;

        for (const auto& st : pack.rig.chain)
        {
            if (st.kind != namz::rig::StageKind::Nam)
                continue;

            for (int t = 0; t < (int) st.tone.size() && t < params::preampNumMeasured; ++t)
            {
                const auto& tone  = st.tone[(size_t) t];
                const auto  start = felitronics::rigplayer::toneStart (tone);

                if (tone.sweep > 0)
                {
                    double norm = 0.5;
                    if (felitronics::rigplayer::toneNorm (tone, start, norm))
                        o.meas[(size_t) t] = (float) norm;
                }
                else
                {
                    const int n = (int) tone.positions.size();
                    for (int k = 0; k < n; ++k)
                        if (tone.positions[(size_t) k].value == start)
                            o.meas[(size_t) t] = n > 1 ? (float) k / (float) (n - 1) : 0.0f;
                }
            }

            int selSlot = 0;
            for (const auto& c : st.device.controls)
                if (c.role != namz::rig::Role::Gain && c.values.size() > 1 && selSlot < params::numSelectors)
                {
                    const auto def = namz::rig::defaultValue (c);
                    for (int k = 0; k < (int) c.values.size(); ++k)
                        if (c.values[(size_t) k] == def)
                            o.sel[(size_t) selSlot] = k;
                    ++selSlot;
                }

            break;
        }

        return o;
    }
};

} // namespace orbitamp::device
