// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_core/juce_core.h>

#include <string>

/** THE GPU'S REFUSALS, WRITTEN DOWN. A world whose GPU view will not start — no context current to build it
    in, a shader the driver would not compile or link — is drawn on the CPU instead, on its own: the rovers'
    windscreen falls back to the 1993 landscape (redpaw/view/Windscreen.cpp), the cockpit's sky to its CPU
    stars (catpit/CatpitGlSky.cpp). A player who sees that has no way to tell why, so each such refusal
    leaves a line here, in plain text, for them to send:

        2026-10-20 14:03:22  RedPawAmp  VST3 in Steinberg Cubase  |  GL: Apple M1 / Apple / 4.1 Metal - 89.4  |  Plains: ...

    the local date and time, the product, the format it was loaded as and the host where JUCE knows it
    (AmpProcessor::loadedAs), the context's renderer, vendor and version where there was one, and why.

    ONE FILE FOR THE FAMILY, gpu.log in Darwin's Cat's own folder, beside every product's settings folder
    (prefs::store writes under the same root): on the Mac ~/Library/Application Support/Darwin's Cat/gpu.log,
    on Windows %APPDATA%\Darwin's Cat\gpu.log (C:\Users\<name>\AppData\Roaming), on Linux
    ~/.config/Darwin's Cat/gpu.log ($XDG_CONFIG_HOME in place of ~/.config where it is set). Kept small: past
    capBytes it keeps its newest half.

    Written from the GL thread, which is where a context is found refusing: the log takes no lock but its own,
    which nothing on the message thread holds, makes only the line it writes — and, once past the cap, one copy
    of the file — and never throws. The app's only: the site's worlds go on saying theirs on stderr
    (gpu::note). */
namespace orbitamp::gpulog
{

/** Past this many bytes, the log keeps its newest half. */
inline constexpr juce::int64 capBytes = 256 * 1024;

/** The family's log, where it is on this machine. */
juce::File file();

/** One line as the log keeps it, without its date and time: `who` — the product, the format, the host —
    the driver's words for the context (gpu::driver(), "no context" where it is empty), and why. Each field
    on the line it is given: a driver's log of several lines is joined with " / ", and a very long one cut. */
juce::String line (const juce::String& who, const std::string& driver, const std::string& why);

/** `text` as one line at the end of `log`, after the local date and time — the newest half kept first
    where it would grow past `cap`. The folder is made where it is missing. Any thread; never throws. */
void append (const juce::File& log, const juce::String& text, juce::int64 cap = capBytes) noexcept;

/** A refusal, into the family's log: append (file(), line (who, driver, why)). */
void refusal (const juce::String& who, const std::string& driver, const std::string& why) noexcept;

} // namespace orbitamp::gpulog
