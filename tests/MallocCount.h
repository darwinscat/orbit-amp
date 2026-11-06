// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

// THE MALLOC FAMILY, COUNTED where a program can see it — for the allocation gates (tests/ChainTests.cpp), which count
// `operator new` themselves. ORBITAMP_COUNT_ALLOCATIONS says whether to count at all (off under a sanitizer); this header
// defines ORBITAMP_COUNT_MALLOCS to say whether it could, and countMallocsHere / mallocsSoFar either way.
//
//   · On the Mac every malloc, calloc, realloc and aligned allocation passes libSystem's malloc_logger, set while counting.
//   · On glibc this binary's malloc, calloc, realloc, posix_memalign, aligned_alloc and memalign stand in front of the C
//     library's (ELF interposition: every library in the process calls these), count, and hand on to __libc_*.
//   · Elsewhere — the MSVC runtime — nothing is counted, and the gate says so rather than claim it.
//
// It counts the thread that asked to be counted, alone, and touches no thread-local: a thread-local's first touch can
// allocate, inside the allocator.

#include <atomic>
#include <cstddef>
#include <cstdint>

#if ! defined(ORBITAMP_COUNT_ALLOCATIONS)
  #error "the includer says whether allocations are counted (ORBITAMP_COUNT_ALLOCATIONS)"
#endif

#if ORBITAMP_COUNT_ALLOCATIONS && (defined(__APPLE__) || defined(__GLIBC__))
  #define ORBITAMP_COUNT_MALLOCS 1
  #include <pthread.h>

static std::atomic<long long> gMallocs { 0 };
static std::atomic<bool>      gMallocCounting { false };
static pthread_t              gMallocThread {};

static inline void countOneMalloc() noexcept
{
    if (gMallocCounting.load (std::memory_order_relaxed) && pthread_equal (pthread_self(), gMallocThread))
        gMallocs.fetch_add (1, std::memory_order_relaxed);
}

  #if defined(__APPLE__)
extern "C"
{
    typedef void (malloc_logger_t) (uint32_t type, uintptr_t, uintptr_t, uintptr_t, uintptr_t result, uint32_t skip);
    extern malloc_logger_t* malloc_logger;
}
static void logOneMalloc (uint32_t type, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint32_t)
{
    if ((type & 2u) != 0)   // MALLOC_LOG_TYPE_ALLOCATE
        countOneMalloc();
}
static void hookMallocs (bool on) { malloc_logger = on ? logOneMalloc : nullptr; }
  #else
    #include <cerrno>
extern "C"
{
    void* __libc_malloc (std::size_t) noexcept;
    void* __libc_calloc (std::size_t, std::size_t) noexcept;
    void* __libc_realloc (void*, std::size_t) noexcept;
    void* __libc_memalign (std::size_t, std::size_t) noexcept;
    void  __libc_free (void*) noexcept;

    void* malloc (std::size_t n) noexcept                    { countOneMalloc(); return __libc_malloc (n); }
    void* calloc (std::size_t a, std::size_t b) noexcept     { countOneMalloc(); return __libc_calloc (a, b); }
    void* realloc (void* p, std::size_t n) noexcept          { countOneMalloc(); return __libc_realloc (p, n); }
    void* memalign (std::size_t al, std::size_t n) noexcept  { countOneMalloc(); return __libc_memalign (al, n); }
    void* aligned_alloc (std::size_t al, std::size_t n) noexcept { countOneMalloc(); return __libc_memalign (al, n); }
    int   posix_memalign (void** out, std::size_t al, std::size_t n) noexcept
    {
        countOneMalloc();
        void* p = __libc_memalign (al, n);
        if (p == nullptr)
            return ENOMEM;
        *out = p;
        return 0;
    }
    void free (void* p) noexcept { __libc_free (p); }
}
static void hookMallocs (bool) {}
  #endif

static void countMallocsHere (bool on)
{
    gMallocThread = pthread_self();
    hookMallocs (on);
    gMallocCounting.store (on, std::memory_order_relaxed);
}
static long long mallocsSoFar() { return gMallocs.load (std::memory_order_relaxed); }
#else
  #define ORBITAMP_COUNT_MALLOCS 0
static void countMallocsHere (bool) {}
static long long mallocsSoFar() { return 0; }
#endif
