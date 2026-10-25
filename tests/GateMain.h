// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

/** WHAT A GATE'S main() IS CALLED. Built alone, a gate is a program of its own, and this is main(). In the
    gates binary (CMakeLists.txt: THE GATES BINARY) every gate is one file of one program — compiled once for
    every amp it runs as — and the build gives each its own name here; tests/Gates.cpp runs the one asked for. */
#ifndef ORBITAMP_GATE_MAIN
 #define ORBITAMP_GATE_MAIN main
#endif
