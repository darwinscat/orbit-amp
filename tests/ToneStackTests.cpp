// Headless acceptance gate for core::ToneStack — the tone stack's response, checked against what a
// shelf/bell/cut is supposed to do, plus the invariant that matters most: the curve the UI draws is
// the filter chain that actually runs. Returns non-zero on any failure.
//
// JUCE-free, like the code under test: links only felitronics::eq.

#include "core/ToneStack.h"

#include <cmath>
#include <cstdio>
#include <vector>

using orbitamp::core::ToneStack;

namespace
{
    int failures = 0;

    void check (const char* what, double got, double want, double tol)
    {
        const bool ok = std::fabs (got - want) <= tol;
        if (! ok)
            ++failures;

        std::printf ("%-46s %8.3f  (want %7.3f +-%.2f)  %s\n", what, got, want, tol, ok ? "ok" : "FAIL");
    }

    constexpr double sampleRate = 48000.0;
    constexpr double pi = 3.14159265358979323846;
}

int main()
{
    ToneStack tone;
    tone.prepare (sampleRate, 2);

    // A stack at rest is bit-transparent, not "flat within rounding" — every band self-disables.
    check ("flat: 1 kHz", tone.magnitudeDb (1000.0), 0.0, 1.0e-9);
    check ("flat: 50 Hz", tone.magnitudeDb (50.0),   0.0, 1.0e-9);

    ToneStack::Settings s;

    s = {}; s.lowDb = 12.0; tone.setSettings (s);
    check ("low +12: 30 Hz (shelf plateau)",    tone.magnitudeDb (30.0),    12.0, 0.5);
    check ("low +12: 100 Hz (corner, half up)", tone.magnitudeDb (100.0),    6.0, 1.5);
    check ("low +12: 10 kHz (untouched)",       tone.magnitudeDb (10000.0),  0.0, 0.2);

    s = {}; s.midDb = -12.0; tone.setSettings (s);
    check ("mid -12: 600 Hz (centre)", tone.magnitudeDb (600.0), -12.0, 0.3);
    check ("mid -12: 30 Hz (clear)",   tone.magnitudeDb (30.0),    0.0, 0.6);

    s = {}; s.highDb = 12.0; tone.setSettings (s);
    check ("high +12: 15 kHz (plateau)",  tone.magnitudeDb (15000.0), 12.0, 0.6);
    check ("high +12: 50 Hz (untouched)", tone.magnitudeDb (50.0),     0.0, 0.2);

    s = {}; s.presenceDb = 12.0; tone.setSettings (s);
    check ("presence +12: 15 kHz (plateau)",  tone.magnitudeDb (15000.0), 12.0, 0.6);
    check ("presence +12: 200 Hz (clear)",    tone.magnitudeDb (200.0),    0.0, 0.3);

    // Presence must NOT be the high shelf in a second copy — that is the trap the measured hardware
    // fell into (1655 Hz and 1132 Hz, half an octave apart, i.e. one control twice). At the main
    // shelf's own corner presence has to have delivered almost nothing yet.
    {
        const double atMainCorner = tone.magnitudeDb (2800.0);
        const bool   ok = atMainCorner < 12.0 * 0.25;
        if (! ok)
            ++failures;

        std::printf ("%-46s %8.3f  (want < %.1f of a 12 dB boost)  %s\n",
                     "presence +12: barely started at 2.8 kHz", atMainCorner, 12.0 * 0.25, ok ? "ok" : "FAIL");
    }

    // The cuts are 12 dB/oct: one octave below the corner must be ~12 dB down.
    s = {}; s.hpfOn = true; s.hpfHz = 100.0; tone.setSettings (s);
    check ("hpf 100: 100 Hz (-3 dB corner)", tone.magnitudeDb (100.0),   -3.0, 0.6);
    check ("hpf 100: 50 Hz (one octave)",    tone.magnitudeDb (50.0),   -12.0, 1.5);
    check ("hpf 100: 1 kHz (passband)",      tone.magnitudeDb (1000.0),   0.0, 0.2);

    s = {}; s.lpfOn = true; s.lpfHz = 5000.0; tone.setSettings (s);
    check ("lpf 5k: 5 kHz (-3 dB corner)", tone.magnitudeDb (5000.0),  -3.0, 0.6);
    check ("lpf 5k: 10 kHz (one octave)",  tone.magnitudeDb (10000.0), -12.0, 1.5);

    // THE load-bearing one: the drawn curve must be the filters that run. A sine through process()
    // has to come out at exactly the level magnitudeDb() promised — otherwise the face lies.
    s = {}; s.lowDb = 9.0; tone.setSettings (s); tone.reset();

    const int n = (int) sampleRate;
    std::vector<float> buffer ((size_t) n);
    const double freq = 40.0;

    for (int i = 0; i < n; ++i)
        buffer[(size_t) i] = (float) std::sin (2.0 * pi * freq * i / sampleRate);

    float* channels[1] = { buffer.data() };
    tone.process (channels, 1, n);

    double peak = 0.0;
    for (int i = n / 2; i < n; ++i)   // second half only — past the filter's settling
        peak = std::max (peak, (double) std::fabs (buffer[(size_t) i]));

    check ("processed 40 Hz matches drawn curve", 20.0 * std::log10 (peak), tone.magnitudeDb (freq), 0.2);

    std::printf ("\n%s\n", failures != 0 ? "FAILURES" : "all checks passed");
    return failures;
}
