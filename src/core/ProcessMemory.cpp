// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "ProcessMemory.h"

#if defined (__APPLE__)
 #include <mach/mach.h>
#elif defined (_WIN32)
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <psapi.h>
#elif defined (__linux__)
 #include <cstdio>
 #include <unistd.h>
#endif

namespace orbitamp::core
{

std::uint64_t processMemoryBytes() noexcept
{
   #if defined (__APPLE__)
    task_vm_info_data_t info {};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;

    if (task_info (mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t> (&info), &count) != KERN_SUCCESS)
        return 0;

    return static_cast<std::uint64_t> (info.phys_footprint);

   #elif defined (_WIN32)
    PROCESS_MEMORY_COUNTERS counters {};
    counters.cb = sizeof (counters);

    if (! GetProcessMemoryInfo (GetCurrentProcess(), &counters, sizeof (counters)))
        return 0;

    return static_cast<std::uint64_t> (counters.WorkingSetSize);

   #elif defined (__linux__)
    // statm counts in pages: the whole program first, the resident set second.
    unsigned long long total = 0, resident = 0;
    int read = 0;

    if (std::FILE* f = std::fopen ("/proc/self/statm", "r"))
    {
        read = std::fscanf (f, "%llu %llu", &total, &resident);
        std::fclose (f);
    }

    const long page = sysconf (_SC_PAGESIZE);
    return read == 2 && page > 0 ? static_cast<std::uint64_t> (resident) * static_cast<std::uint64_t> (page) : 0;

   #else
    return 0;
   #endif
}

} // namespace orbitamp::core
