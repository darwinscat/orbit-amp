// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "GpuLog.h"

#include <mutex>

namespace orbitamp::gpulog
{

namespace
{
    /** A field as one line: its own line breaks joined, its ends trimmed, and no longer than a line of a log
        should be — a driver's log of a shader can run to pages. */
    juce::String oneLine (const juce::String& text)
    {
        constexpr int longest = 3000;
        auto s = text.trim().replace ("\r\n", "\n").replace ("\r", "\n").replace ("\t", " ");
        s = juce::StringArray::fromLines (s).joinIntoString (" / ").trim();
        return s.length() > longest ? s.substring (0, longest) + "..." : s;
    }

    /** The log past its cap: its newest half, from the first whole line in it. */
    void keepNewestHalf (const juce::File& log, juce::int64 cap)
    {
        juce::MemoryBlock all;
        if (! log.loadFileAsData (all))
            return;

        const auto keep = (size_t) juce::jmax ((juce::int64) 0, cap / 2);
        const auto size = all.getSize();
        if (size <= keep)
            return;

        const auto* bytes = static_cast<const char*> (all.getData());
        size_t from = size - keep;
        while (from < size && bytes[from - 1] != '\n')
            ++from;

        juce::TemporaryFile fresh (log);
        {
            juce::FileOutputStream out (fresh.getFile());
            if (! out.openedOk() || ! out.write (bytes + from, size - from))
                return;
        }
        fresh.overwriteTargetFileWithTemporary();
    }
}

juce::File file()
{
    // Where prefs::store keeps every product's settings (felitronics::appkit::SettingsStore), one step up:
    // JUCE's user application data, which on the Mac is bare ~/Library.
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);

   #if JUCE_MAC
    dir = dir.getChildFile ("Application Support");
   #endif

    return dir.getChildFile ("Darwin's Cat").getChildFile ("gpu.log");
}

juce::String line (const juce::String& who, const std::string& driver, const std::string& why)
{
    const auto gl = driver.empty() ? juce::String ("no context") : oneLine (juce::String::fromUTF8 (driver.c_str()));
    return oneLine (who) + "  |  GL: " + gl + "  |  " + oneLine (juce::String::fromUTF8 (why.c_str()));
}

void append (const juce::File& log, const juce::String& text, juce::int64 cap) noexcept
{
    try
    {
        // The log's own: only a writer of this log ever holds it, and only for the one line.
        static std::mutex writing;
        const std::scoped_lock held (writing);

        const auto entry = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S") + "  " + oneLine (text) + "\n";

        if (const auto dir = log.getParentDirectory(); ! dir.isDirectory() && ! dir.createDirectory())
            return;

        if (log.getSize() + (juce::int64) entry.getNumBytesAsUTF8() > cap)
            keepNewestHalf (log, cap);

        juce::FileOutputStream out (log);       // at the end of what is there
        if (out.openedOk())
            out.writeText (entry, false, false, nullptr);
    }
    catch (...)
    {
        // A log that cannot be written says nothing; the window it was about goes on, on the CPU.
    }
}

void refusal (const juce::String& who, const std::string& driver, const std::string& why) noexcept
{
    try
    {
        append (file(), line (who, driver, why));
    }
    catch (...)
    {
    }
}

} // namespace orbitamp::gpulog
