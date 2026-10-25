// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// THE GATES BINARY (CMakeLists.txt: THE GATES BINARY) — most of tests/gates.txt's gates in one program, run one
// at a time, each as its own process:
//
//     orbitamp_gates <gate>          or, as tools/run-gates.sh runs it,   ORBITAMP_GATE=<gate> orbitamp_gates
//
// Every gate is its own file still, under a main() of its own name (tests/GateMain.h); the build writes the
// table of them (<build>/gates-src/GatesTable.h) and what each reads: the build's factory folder, one that is
// not there, or the machine's. A gate it does not hold, or none named, prints the ones it does and fails.

#include "device/FactorySet.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "GatesTable.h"

namespace orbitamp::gates
{
namespace
{
    juce::File folder;
}

juce::File factory() { return folder; }
}

int main (int argc, char* argv[])
{
    const char* asked = argc > 1 ? argv[1] : std::getenv ("ORBITAMP_GATE");

    for (const auto& gate : gateTable)
    {
        if (asked == nullptr || std::strcmp (asked, gate.name) != 0)
            continue;

        switch (gate.factory)
        {
            case GateFactory::build:   orbitamp::gates::folder = juce::File (juce::CharPointer_UTF8 (ORBITAMP_GATES_FACTORY)); break;
            case GateFactory::none:    orbitamp::gates::folder = juce::File (juce::CharPointer_UTF8 (ORBITAMP_GATES_NO_FACTORY)); break;
            case GateFactory::machine: orbitamp::gates::folder = juce::File(); break;   // FactorySet finds the machine's
        }

        return gate.run();
    }

    std::printf ("orbitamp_gates: %s%s — the gates it holds:\n", asked == nullptr ? "no gate named" : "no gate ",
                 asked == nullptr ? "" : asked);

    for (const auto& gate : gateTable)
        std::printf ("  %s\n", gate.name);

    std::printf ("FAILED\n");
    return 1;
}
