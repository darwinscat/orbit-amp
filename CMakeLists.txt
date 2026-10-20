cmake_minimum_required(VERSION 3.22)

# ----------------------------------------------------------------------------
# orbit-amp — the device / voicing PLAYER plugin (C++/JUCE). Felitronics line, by Darwin's Cat.
#
# The chain:  preamp (voicing) -> EQ -> delay -> reverb -> cabinet.
#
# One CMake config builds VST3 + AU + Standalone. JUCE 8 and the felitronics libraries are
# fetched by CMake (FetchContent) — NOT vendored, NOT system-installed. Each dependency is
# HYBRID: a local sibling checkout is used when present (co-development), otherwise a pinned
# release is fetched, so a clean clone still configures.
# ----------------------------------------------------------------------------

# THE OLDEST macOS THE BINARIES RUN ON. Left unsaid, the compiler marks every binary for the macOS of
# the machine that built it — OrbitAmp 0.7.0 and CatpitAmp 0.2.0 went out marked for macOS 26, and a Mac
# one release behind would not open them. 11.0, Big Sur, is the first macOS on Apple Silicon, so as far
# back as the arm64 half of a universal binary can go — and both halves say the same. A cache default,
# so a configure can still say otherwise (-DCMAKE_OSX_DEPLOYMENT_TARGET=…); and before project(),
# because that is where CMake reads it. Elsewhere than the Mac nothing reads it at all.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "The oldest macOS the binaries run on")

# THE FAMILY'S VERSION, said here and nowhere else. Every amp this tree builds is released together, as
# Darwin's Cat Amps, under one tag, vX.Y.Z — and this is its number: every amp shows it, stamps its bundles
# with it and compares the family's releases against it (add_amp). The release refuses a tag that says
# another (installer/version.sh, which reads it from this line).
project(OrbitAmp VERSION 0.8.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # clangd / IDE indexing

# Auto-install built plug-ins into the user's plug-in folders (the everyday dev loop).
# Turn OFF for CI / secondary worktrees so an experiment never overwrites the installed build.
option(ORBITAMP_COPY_PLUGIN "Copy built plug-ins to the user plug-in folders after build" ON)

# Universal binary on Apple Silicon + Intel. Configure-time only; skipped if the caller chose.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS build archs" FORCE)
endif()

include(FetchContent)

# ----------------------------------------------------------------------------
# JUCE — pinned tag, with a local-source-dir escape hatch. ORBITAMP_JUCE_DIR defaults to the
# JUCE tree another Felitronics project already fetched, so a first build here costs no second
# full clone — but only while that tree is the pinned version; any other tree is passed over and
# the pin is fetched, so a local build never quietly runs on a JUCE the CI does not. Bump the tag
# deliberately.
# ----------------------------------------------------------------------------
set(ORBITAMP_JUCE_TAG "8.0.16-security" CACHE STRING "JUCE git tag to build against")
set(ORBITAMP_JUCE_DIR "$ENV{HOME}/IdeaProjects/orbit-capture/app/tests/build/_deps/juce-src"
    CACHE PATH "Local JUCE source tree (used if it is the pinned version); else the pin is fetched")
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" _orbitamp_juce_want "${ORBITAMP_JUCE_TAG}")
set(_orbitamp_juce_have "")
if(ORBITAMP_JUCE_DIR AND EXISTS "${ORBITAMP_JUCE_DIR}/CMakeLists.txt")
    file(STRINGS "${ORBITAMP_JUCE_DIR}/CMakeLists.txt" _orbitamp_juce_have
        REGEX "^project\\(JUCE VERSION [0-9.]+" LIMIT_COUNT 1)
    string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" _orbitamp_juce_have "${_orbitamp_juce_have}")
    if(NOT _orbitamp_juce_have STREQUAL _orbitamp_juce_want)
        message(STATUS "JUCE: local tree is ${_orbitamp_juce_have}, the pin is ${_orbitamp_juce_want} — passed over")
    endif()
endif()
if(_orbitamp_juce_have AND _orbitamp_juce_have STREQUAL _orbitamp_juce_want)
    message(STATUS "JUCE: local source — ${ORBITAMP_JUCE_DIR}")
    FetchContent_Declare(juce SOURCE_DIR "${ORBITAMP_JUCE_DIR}")
else()
    message(STATUS "JUCE: fetching ${ORBITAMP_JUCE_TAG} from GitHub")
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${ORBITAMP_JUCE_TAG}
        GIT_SHALLOW    TRUE)
endif()
FetchContent_MakeAvailable(juce)

# ----------------------------------------------------------------------------
# CLAP format via the MIT clap-juce-extensions wrapper (native CLAP is slated for JUCE 9).
# Pinned like JUCE — the same commit OrbitCab ships. Upstream stopped tagging in 2022, so a
# commit hash is their de-facto release channel.
# ----------------------------------------------------------------------------
set(ORBITAMP_CLAP_EXT_TAG "51a9359315298de632cf44e9d7524940868441e6" CACHE STRING "clap-juce-extensions git commit to build against")
FetchContent_Declare(
    clap-juce-extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG        ${ORBITAMP_CLAP_EXT_TAG})
FetchContent_MakeAvailable(clap-juce-extensions)

# ----------------------------------------------------------------------------
# felitronics-core — shared, JUCE-free, header-only DSP core (eq / dynamics / saturation /
# neural / ...). Nothing is linked yet: the chain stages land one at a time and each adds its
# module to target_link_libraries below. Wired now so that's a one-line change, not a rebuild
# of the dependency graph. Its self-tests do NOT build from here (guarded by PROJECT_IS_TOP_LEVEL).
# ----------------------------------------------------------------------------
# THE TAG THAT CARRIES THE GEOMETRY. core::BypassWire asks
# StreamResampler::pairDelayHostSamples() for the round-trip cost of a rate match, and v0.30.0 is the
# first release with it — v0.29.0 had neither that function nor the kernel behind it, still carrying
# the cubic and the ceil(3*h/m)+3 arithmetic this wire was written against and outgrew.
#
# That gap was not a footnote while it lasted: it is why this branch's CI was red BY CONSTRUCTION for
# its whole life, which was documented here rather than worked around. Measured twice, not assumed —
# replaying CI's own compile command with no sibling checkout failed on the missing symbol, and PR
# #32's first run failed the same way at the same line. The pin moves ONCE, here, inside the PR that
# needs it, so that landing the fix and landing its dependency are one commit and never a window
# where main builds against a core that cannot answer.
#
# ⚠️ AND IT IS THE TAG CI USES, NOT WHAT YOU SEE LOCALLY. The hybrid override below prefers a sibling
# ../felitronics-core checkout, so a local build shows that checkout's HEAD however this line reads.
# Proving anything about the pin means emptying the override — all six of them, not this one — and
# letting FetchContent take the tag.
#
# v0.51.0 is the first release felitronics-guitar-core accepts: nam, rigplayer and poweramp left core
# for that repository in it (see below). v0.60.0 is the first with felitronics::dehum, the HumRemover
# the input's HUM stage is built on. v0.61.0 is the freshest, and the first with felitronics::delay and
# felitronics::reverb — the echo and the room, which were this repository's own stages until they moved
# there, so nothing older builds here. Between v0.31.0 and
# v0.53.0, nothing this plugin calls plays differently INSIDE a running stream —
# the probe loops render bit-identically on both — and what moved is at a stream's edges: a re-prepared
# capture no longer answers with the previous stream's tail, its warm-up is counted at the new rate, a
# trim switched off stays off through the prepare. The cabinet hands CabConvolver taps already at the
# host's rate, so the v0.34.0 rate factor and the v0.32.0 edge fix have nothing to act on here, and the
# v0.53.0 glides are in stages this plugin does not link. Past v0.53.0 two things reach this graph, and
# neither plays here: WaveShaper gains four shapes while Asym — the only one the power amp's TubeStage
# sets — keeps its expressions, and eq's Tilt at slope 6 turns first-order, a band no block here builds.
#
# v0.61.0 DOES play differently, in the two stages it brings, and it was measured before they replaced
# ours: both renders side by side on the same program at 44.1, 48 and 96 kHz, in blocks of 64, 512 and a
# ragged run. At knobs that stand still the echo is within 2.4e-7 of ours and the five plain rooms within
# 3e-14; MODULATED is within 6.5e-5 at full mix on a hot program — its chorus reads its line at a position
# kept in double where ours rounded it to float, so that distance is the tail's slope times a rounding and
# halves with the level. With a knob moving, the echo's head now LANDS: ours parked short of a new time
# for good, the repeats 28 samples early at 1.5 s and 112 at 6 s (48 kHz), and the old echo is the new
# one read at that parked head to within 4e-8 — a time, not a level or a tone. A fresh room no longer
# leaks the dry into its first 10 ms, and an Intel build plays the arm64 sound: the framework's Freeverb
# under ours quantised its state on x86-64, up to 1e-6.
set(ORBITAMP_FCORE_TAG "v0.61.0" CACHE STRING "felitronics-core release tag (fetched when no local checkout)")

set(ORBITAMP_FCORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../felitronics-core"
    CACHE PATH "Local felitronics-core checkout (used if present); else the pinned release is fetched")
if(ORBITAMP_FCORE_DIR AND EXISTS "${ORBITAMP_FCORE_DIR}/CMakeLists.txt")
    message(STATUS "felitronics-core: local checkout — ${ORBITAMP_FCORE_DIR}")
    FetchContent_Declare(felitronics_core SOURCE_DIR "${ORBITAMP_FCORE_DIR}")
else()
    message(STATUS "felitronics-core: fetching ${ORBITAMP_FCORE_TAG} from GitHub")
    FetchContent_Declare(felitronics_core
        GIT_REPOSITORY https://github.com/darwinscat/felitronics-core.git
        GIT_TAG        ${ORBITAMP_FCORE_TAG}
        GIT_SHALLOW    TRUE)
endif()
# The cabinet convolves through felitronics::convolution's CabConvolver, and its audio FFT is pffft
# (SIMD, BSD) — the backend OrbitCab ships. Set before core is made available so the fftpffft module
# is built; the convolution module then carries the definition to every target that links it, which
# is what keeps the choice uniform across translation units.
set(FELITRONICS_WITH_PFFFT ON CACHE BOOL "orbit-amp's cabinet convolver uses the pffft SIMD FFT" FORCE)

# The neural runtime — felitronics::nam, and the pack player over it, felitronics::rigplayer. Since core
# v0.51.0 both live in felitronics-guitar-core, made available right AFTER core below, by the same rule
# as core: a sibling checkout when present, the pinned tag otherwise. It builds them by default, so
# there is no switch to throw here any more (core refuses the old FELITRONICS_WITH_NAM outright). The
# NAM module fetches NeuralAmpModelerCore and the namz codec itself — the overrides below only spare a
# second clone of trees this machine already has, and fall back to the pins when they are gone. From v0.1.1
# it takes Eigen from Darwin's Cat's own mirror, github.com/darwinscat/eigen, instead of NeuralAmpModelerCore's
# submodule on gitlab.com: a build now asks GitHub alone.
set(ORBITAMP_GUITAR_TAG "v0.1.1" CACHE STRING "felitronics-guitar-core release tag (fetched when no local checkout)")
set(ORBITAMP_GUITAR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../felitronics-guitar-core"
    CACHE PATH "Local felitronics-guitar-core checkout (used if present); else the pinned release is fetched")

# ...and a build directory configured before the move still carries the FELITRONICS_WITH_NAM=ON this
# file used to FORCE into its cache, which core now stops the configure on. We put it there; we take it
# out, so an existing build directory configures again instead of asking its owner for a -U.
unset(FELITRONICS_WITH_NAM CACHE)

set(ORBITAMP_NAM_CORE_SRC "$ENV{HOME}/IdeaProjects/orbit-nam-capture/app/build-hard/_deps/nam_core_src-src"
    CACHE PATH "Local NeuralAmpModelerCore checkout; empty fetches the pin")
set(ORBITAMP_NAMZ_SRC "$ENV{HOME}/IdeaProjects/namz"
    CACHE PATH "Local namz checkout; empty fetches the pin")

# The local NAM copy is a SHORTCUT, not a source of truth: the NAM module pins the engine, and a
# copy sitting on a different commit would silently build a different DSP than the pin says. So the
# shortcut is taken only when it agrees with the pin — read out of the NAM module's own CMakeLists (in
# felitronics-guitar-core since core v0.51.0), since the pin is not known to us until that is configured,
# which is after this must be decided.
if(EXISTS "${ORBITAMP_NAM_CORE_SRC}/.git")
    set(_oa_nam_pin "")
    if(EXISTS "${ORBITAMP_GUITAR_DIR}/modules/nam/CMakeLists.txt")
        file(READ "${ORBITAMP_GUITAR_DIR}/modules/nam/CMakeLists.txt" _oa_nam_cml)
        string(REGEX MATCH "FELITRONICS_NAM_CORE_TAG \"([^\"]+)\"" _oa_nam_match "${_oa_nam_cml}")
        set(_oa_nam_pin "${CMAKE_MATCH_1}")
    endif()

    # ...and a build directory that was configured before the pin moved keeps the OLD value in its
    # cache, because the NAM module declares the pin as a CACHE variable and a cache entry wins over a
    # new default. Same failure, one layer down: the build would still compile the engine we replaced.
    if(_oa_nam_pin AND DEFINED FELITRONICS_NAM_CORE_TAG AND NOT FELITRONICS_NAM_CORE_TAG STREQUAL _oa_nam_pin)
        message(STATUS "NeuralAmpModelerCore: cached pin ${FELITRONICS_NAM_CORE_TAG} is stale — the NAM module pins ${_oa_nam_pin}")
        set(FELITRONICS_NAM_CORE_TAG "${_oa_nam_pin}" CACHE STRING "" FORCE)
    endif()

    find_package(Git QUIET)
    set(_oa_nam_here "")
    if(GIT_EXECUTABLE AND _oa_nam_pin)
        execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
            WORKING_DIRECTORY "${ORBITAMP_NAM_CORE_SRC}"
            OUTPUT_VARIABLE _oa_nam_here OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse "${_oa_nam_pin}^{commit}"
            WORKING_DIRECTORY "${ORBITAMP_NAM_CORE_SRC}"
            OUTPUT_VARIABLE _oa_nam_want OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    endif()

    if(_oa_nam_here AND _oa_nam_want AND _oa_nam_here STREQUAL _oa_nam_want)
        message(STATUS "NeuralAmpModelerCore: local copy is on the pin (${_oa_nam_pin}) — using it")
        set(FELITRONICS_NAM_CORE_SRC "${ORBITAMP_NAM_CORE_SRC}" CACHE PATH "" FORCE)
        set(ORBITAMP_NAM_CORE_USED "${ORBITAMP_NAM_CORE_SRC}")
    else()
        message(STATUS "NeuralAmpModelerCore: local copy is NOT on the NAM module's pin (${_oa_nam_pin}) — fetching the pin instead")
        set(FELITRONICS_NAM_CORE_SRC "" CACHE PATH "" FORCE)
        set(ORBITAMP_NAM_CORE_USED "${CMAKE_BINARY_DIR}/_deps/nam_core_src-src")
    endif()
endif()
if(EXISTS "${ORBITAMP_NAMZ_SRC}/include/namz.h")
    set(FELITRONICS_NAMZ_SRC "${ORBITAMP_NAMZ_SRC}" CACHE PATH "" FORCE)
endif()

# ...and when there is NO local checkout — CI — the tag the NAM module would otherwise pin. The device
# page reads a pack's photograph and the box's own history (`Rig::picture`, `gear.year`,
# `serial_number`, `designed_in`, `made_in`), all of which were newer than the commit the NAM module
# carried. Pinned HERE rather than waited for: a library release exists to move the library's own code,
# and holding this behind one would leave the plugin unbuildable on CI for no reason of its own.
set(ORBITAMP_NAMZ_TAG "v4.1.0" CACHE STRING "namz release tag CI builds against (no local checkout)")
set(FELITRONICS_NAMZ_TAG "${ORBITAMP_NAMZ_TAG}" CACHE STRING "" FORCE)

FetchContent_MakeAvailable(felitronics_core)
if(ORBITAMP_GUITAR_DIR AND EXISTS "${ORBITAMP_GUITAR_DIR}/CMakeLists.txt")
    message(STATUS "felitronics-guitar-core: local checkout — ${ORBITAMP_GUITAR_DIR}")
    FetchContent_Declare(felitronics_guitar_core SOURCE_DIR "${ORBITAMP_GUITAR_DIR}")
else()
    message(STATUS "felitronics-guitar-core: fetching ${ORBITAMP_GUITAR_TAG} from GitHub")
    FetchContent_Declare(felitronics_guitar_core
        GIT_REPOSITORY https://github.com/darwinscat/felitronics-guitar-core.git
        GIT_TAG        ${ORBITAMP_GUITAR_TAG}
        GIT_SHALLOW    TRUE)
endif()
FetchContent_MakeAvailable(felitronics_guitar_core)

# ----------------------------------------------------------------------------
# felitronics-appkit — the family's shared app-side JUCE infrastructure (brand kit, update
# check, device glyphs). Header-only INTERFACE target: it never links JUCE itself, we supply
# the modules transitively. This is also where the faceplate render kit MOVES once it settles
# here (see CLAUDE.md — build it here first, move it later).
# ----------------------------------------------------------------------------
set(ORBITAMP_APPKIT_TAG "v0.22.0" CACHE STRING "felitronics-appkit release tag (fetched when no local checkout)")
set(ORBITAMP_APPKIT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../felitronics-appkit"
    CACHE PATH "Local felitronics-appkit checkout (used if present); else the pinned release is fetched")
if(ORBITAMP_APPKIT_DIR AND EXISTS "${ORBITAMP_APPKIT_DIR}/CMakeLists.txt")
    message(STATUS "felitronics-appkit: local checkout — ${ORBITAMP_APPKIT_DIR}")
    FetchContent_Declare(felitronics_appkit SOURCE_DIR "${ORBITAMP_APPKIT_DIR}")
else()
    message(STATUS "felitronics-appkit: fetching ${ORBITAMP_APPKIT_TAG} from GitHub")
    FetchContent_Declare(felitronics_appkit
        GIT_REPOSITORY https://github.com/darwinscat/felitronics-appkit.git
        GIT_TAG        ${ORBITAMP_APPKIT_TAG}
        GIT_SHALLOW    TRUE)
endif()

# A pack ships its device's photograph as WebP (rig.json's `picture`), and an include path cannot
# carry a codec — appkit's opt-in half brings libwebp, pinned and fetched like JUCE. The capture
# app that WRITES the picture turns the same switch on; this is the reading end of it.
set(FELITRONICS_APPKIT_WEBP ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(felitronics_appkit)

# ----------------------------------------------------------------------------
# OrbitAmpVersion.h — git-describe build stamp, baked into the binary (end users have no repo).
# Regenerated on EVERY build (the target is always out-of-date) so the stamp reflects the tree AT
# BUILD TIME; also generated once at configure so clangd + the first compile have the header. Never
# fails the build (missing git => "0.0.0-dev"/"unknown"). It feeds the footer's version stamp — the
# popover's commit / build / environment lines and the dependency rows.
# ----------------------------------------------------------------------------
find_package(Git QUIET)
set(ORBITAMP_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${ORBITAMP_GENERATED_DIR}")
set(ORBITAMP_VERSION_HEADER "${ORBITAMP_GENERATED_DIR}/OrbitAmpVersion.h")

# Build architecture label for the stamp (Universal when a fat binary, else the single arch).
if(APPLE)
    list(LENGTH CMAKE_OSX_ARCHITECTURES _oa_narch)
    if(_oa_narch GREATER 1)
        set(_oa_arch "Universal")
    elseif(CMAKE_OSX_ARCHITECTURES)
        set(_oa_arch "${CMAKE_OSX_ARCHITECTURES}")
    else()
        set(_oa_arch "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
else()
    set(_oa_arch "${CMAKE_SYSTEM_PROCESSOR}")
endif()

# Build OS label for the stamp.
if(APPLE)
    set(_oa_os "macOS")
elseif(WIN32)
    set(_oa_os "Windows")
elseif(UNIX)
    set(_oa_os "Linux")
else()
    set(_oa_os "${CMAKE_SYSTEM_NAME}")
endif()

# Mirror the felitronics-core FetchContent decision above so kCoreVersion names the checkout used.
if(ORBITAMP_FCORE_DIR AND EXISTS "${ORBITAMP_FCORE_DIR}/CMakeLists.txt")
    set(_oa_fcore_local ON)
else()
    set(_oa_fcore_local OFF)
endif()

# ...and the same for felitronics-guitar-core, where the NAM stage and the pack player come from.
if(ORBITAMP_GUITAR_DIR AND EXISTS "${ORBITAMP_GUITAR_DIR}/CMakeLists.txt")
    set(_oa_guitar_local ON)
else()
    set(_oa_guitar_local OFF)
endif()

# ...and the same for felitronics-appkit: the badge's dependency rows name what was actually built
# against, sibling or pin, never the other one.
if(ORBITAMP_APPKIT_DIR AND EXISTS "${ORBITAMP_APPKIT_DIR}/CMakeLists.txt")
    set(_oa_appkit_local ON)
else()
    set(_oa_appkit_local OFF)
endif()

# The engine's sources as the build ACTUALLY resolved them — the local copy only when it agreed
# with the NAM module's pin, the fetched pin otherwise (see the guard above). The stamp must name what
# was compiled, not what was nearby.
set(_oa_namcore_dir "${ORBITAMP_NAM_CORE_USED}")
if(NOT _oa_namcore_dir)
    set(_oa_namcore_dir "${CMAKE_BINARY_DIR}/_deps/nam_core_src-src")
endif()

# ...and namz, the codec the packs are written in — mirroring the override test above, which keys on
# the header rather than a CMakeLists (namz is consumed by felitronics-core's nam module, not by us).
if(ORBITAMP_NAMZ_SRC AND EXISTS "${ORBITAMP_NAMZ_SRC}/include/namz.h")
    set(_oa_namz_local ON)
else()
    set(_oa_namz_local OFF)
endif()

set(_oa_ver_args
    -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
    -DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}
    -DOUT_FILE=${ORBITAMP_VERSION_HEADER}
    -DARCH=${_oa_arch}
    -DOS=${_oa_os}
    -DFCORE_LOCAL=${_oa_fcore_local}
    -DFCORE_DIR=${ORBITAMP_FCORE_DIR}
    -DFCORE_TAG=${ORBITAMP_FCORE_TAG}
    -DGUITAR_LOCAL=${_oa_guitar_local}
    -DGUITAR_DIR=${ORBITAMP_GUITAR_DIR}
    -DGUITAR_TAG=${ORBITAMP_GUITAR_TAG}
    -DAPPKIT_LOCAL=${_oa_appkit_local}
    -DAPPKIT_DIR=${ORBITAMP_APPKIT_DIR}
    -DAPPKIT_TAG=${ORBITAMP_APPKIT_TAG}
    -DNAMZ_LOCAL=${_oa_namz_local}
    -DNAMZ_DIR=${ORBITAMP_NAMZ_SRC}
    -DNAMZ_TAG=${ORBITAMP_NAMZ_TAG}
    -DNAMCORE_DIR=${_oa_namcore_dir}
    -DNAMCORE_TAG=${FELITRONICS_NAM_CORE_TAG})

# Generate once at configure time too, so the header exists for IDEs / clangd before the first build.
execute_process(COMMAND ${CMAKE_COMMAND} ${_oa_ver_args}
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateOrbitAmpVersion.cmake")

add_custom_target(OrbitAmpVersionGen ALL
    COMMAND ${CMAKE_COMMAND} ${_oa_ver_args}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateOrbitAmpVersion.cmake"
    BYPRODUCTS "${ORBITAMP_VERSION_HEADER}"
    COMMENT "OrbitAmp: baking OrbitAmpVersion.h build stamp"
    VERBATIM)

# ----------------------------------------------------------------------------
# THE FACTORY FOLDER: every file of data a Darwin's Cat amp plays — the packs, the cabinet shelf, each
# face's pictures — ONE copy of each on the machine. No amp carries any of it in its binary: three amps in
# four formats and two architectures made the same 26 MB into a release of 369 MB. The family's installer
# lays the folder out once, as a part of it the player cannot untick (installer/):
#
#   macOS    /Library/Application Support/Darwin's Cat/Factory
#   Windows  C:\ProgramData\Darwin's Cat\Factory
#   Linux    <share>/darwinscat-amps/Factory — ~/.local/share for the player alone, /usr/local/share for
#            every user (installer/linux/install.sh)
#
# Inside it the packs lie at its top (<pack>.orbitrig.zip), the cabinet shelf in IRs/<folder>/<file> as
# the cabinet catalog has it, and each face's pictures in Faces/<Name>/<file>. EACH AMP SEES ONLY WHAT ITS
# OWN LIST NAMES, sets/<Name>.txt, one path to the line (src/device/FactorySet.h): the folder is the
# family's, and what one amp plays is not every amp's.
#
# What an amp plays it says in its row of the table (add_amp: ART, SET, WHOLE_SHELF), and add_amp says it
# here (factory_carries). After the table, family_factory() makes of it the folder of every amp in the
# build tree, <build>/Factory — the target Family_Factory, which every amp and every gate that reads the
# folder waits for — and the folder of the amps installer/family.ini names and nothing else,
# <build>/Shipped/Factory (Family_ShippedFactory, part of Family_All), which every installer lays out.
#
# ORBITAMP_DEV_FACTORY: an amp built here reads <build>/Factory FIRST, where it is there, so it plays the
# files built with it whatever is installed. A release passes it OFF (.github/workflows/release.yml,
# installer/release.sh): its binaries leave the machine, and the macOS release is built on a working
# laptop, where the build folder is there. A gate reads <build>/Factory alone, whatever this says, and
# never the machine's (gate_reads_factory).
# ----------------------------------------------------------------------------
option(ORBITAMP_DEV_FACTORY "Amps read <build>/Factory first, where it is there (OFF for a release)" ON)
set(ORBITAMP_FACTORY_DIR "${CMAKE_CURRENT_BINARY_DIR}/Factory")
set(ORBITAMP_SHIPPED_FACTORY_DIR "${CMAKE_CURRENT_BINARY_DIR}/Shipped/Factory")

# factory_carries(<Name> FILES <files...> AT <paths...>): <Name> plays each file, which lies in the folder
# at the path beside it. One path is one file: two files said for one path stop the configure, since the
# folder could hold only one of them. Paths have spaces in them, so each is kept under its hash.
function(factory_carries name)
    cmake_parse_arguments(PARSE_ARGV 1 FC "" "" "FILES;AT")
    list(LENGTH FC_FILES files)
    list(LENGTH FC_AT paths)
    if(NOT files EQUAL paths)
        message(FATAL_ERROR "factory_carries(${name}): ${files} files for ${paths} paths")
    endif()
    if(files EQUAL 0)
        return()
    endif()
    math(EXPR last "${files} - 1")
    foreach(i RANGE ${last})
        list(GET FC_FILES ${i} file)
        list(GET FC_AT ${i} at)
        string(MD5 key "${at}")
        get_property(from GLOBAL PROPERTY ORBITAMP_FACTORY_FROM_${key})
        if("${from}" STREQUAL "")
            set_property(GLOBAL PROPERTY ORBITAMP_FACTORY_FROM_${key} "${file}")
            set_property(GLOBAL APPEND PROPERTY ORBITAMP_FACTORY_PATHS "${at}")
        elseif(NOT "${from}" STREQUAL "${file}")
            message(FATAL_ERROR "The factory's ${at} would be two files: ${from} and ${file} (${name})")
        endif()
        get_property(own GLOBAL PROPERTY ORBITAMP_FACTORY_LIST_${name})
        if(NOT at IN_LIST own)
            set_property(GLOBAL APPEND PROPERTY ORBITAMP_FACTORY_LIST_${name} "${at}")
        endif()
    endforeach()
endfunction()

# family_factory(): the folder of what every amp said, after the table — one copy of each file, each amp's
# list beside them — and the release's, of the family's amps alone (FAMILY_AMPS, THE FAMILY'S RELEASE).
function(family_factory)
    set(shippedPaths "")
    foreach(amp IN LISTS FAMILY_AMPS)
        get_property(own GLOBAL PROPERTY ORBITAMP_FACTORY_LIST_${amp})
        foreach(at IN LISTS own)
            if(NOT at IN_LIST shippedPaths)
                list(APPEND shippedPaths "${at}")
            endif()
        endforeach()
    endforeach()

    get_property(paths GLOBAL PROPERTY ORBITAMP_FACTORY_PATHS)
    set(laid "")
    set(shipped "")
    foreach(at IN LISTS paths)
        string(MD5 key "${at}")
        get_property(from GLOBAL PROPERTY ORBITAMP_FACTORY_FROM_${key})
        set(into "${ORBITAMP_FACTORY_DIR}/${at}")
        list(APPEND laid "${into}")
        if(at IN_LIST shippedPaths)
            list(APPEND into "${ORBITAMP_SHIPPED_FACTORY_DIR}/${at}")
            list(APPEND shipped "${ORBITAMP_SHIPPED_FACTORY_DIR}/${at}")
        endif()
        foreach(to IN LISTS into)
            get_filename_component(folder "${to}" DIRECTORY)
            add_custom_command(OUTPUT "${to}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${folder}"
                COMMAND "${CMAKE_COMMAND}" -E copy "${from}" "${to}"
                DEPENDS "${from}"
                VERBATIM)
        endforeach()
    endforeach()

    # The packs are zipped by the build (THE SET), by one target: these wait for it, rather than run the
    # zips' rules themselves and race it.
    add_custom_target(Family_Factory DEPENDS ${laid})
    add_dependencies(Family_Factory OrbitAmpSetPacks)
    add_custom_target(Family_ShippedFactory DEPENDS ${shipped})
    add_dependencies(Family_ShippedFactory OrbitAmpSetPacks)
    add_dependencies(Family_All Family_ShippedFactory)

    foreach(amp IN LISTS AMPS)
        get_property(own GLOBAL PROPERTY ORBITAMP_FACTORY_LIST_${amp})
        list(JOIN own "\n" lines)
        file(GENERATE OUTPUT "${ORBITAMP_FACTORY_DIR}/sets/${amp}.txt" CONTENT "${lines}\n")
        if(amp IN_LIST FAMILY_AMPS)
            file(GENERATE OUTPUT "${ORBITAMP_SHIPPED_FACTORY_DIR}/sets/${amp}.txt" CONTENT "${lines}\n")
        endif()
    endforeach()

    list(LENGTH paths every)
    list(LENGTH shippedPaths family)
    message(STATUS "The factory folder: ${every} files in the build's, ${family} of them in the release's")
endfunction()

# A GATE READS THE BUILD'S FACTORY FOLDER ALONE, never the machine's — or, given one, a folder that is not
# there (gate_reads_factory(<target> <folder>)), to play an amp with nothing installed.
function(gate_reads_factory target)
    set(folder "${ORBITAMP_FACTORY_DIR}")
    if(ARGC GREATER 1)
        set(folder "${ARGV1}")
    endif()
    target_compile_definitions(${target} PRIVATE ORBITAMP_GATE_FACTORY="${folder}")
    add_dependencies(${target} Family_Factory)
endfunction()

# ----------------------------------------------------------------------------
# THE FAMILY. Every Darwin's Cat amp this tree builds is one call of add_amp(), and the calls stand
# together further down as one table (THE AMPS): what tells an amp from the others is said in its row,
# and what they share is said once, here. Each amp is the same JUCE plug-in — VST3, AU and the
# Standalone, and a CLAP through clap-juce-extensions — over the same processor; what differs is its
# face, its name, its number and what it carries.
#
#   add_amp(<Name>  CODE <code>  [BY_NAME]
#           SOURCES <files...>  LIBRARIES <targets...>  [ART <files...>]  [SET <packs...>]  [WHOLE_SHELF])
#
#   <Name>      the target, the PRODUCT_NAME and the stem of every bundle it builds; in lower case, its
#               bundle id and its CLAP id, com.darwinscat.<name>; and its icon, resources/icon/<Name>-*.
#               It is the name of the amp's Product too (src/Product.h), which the amp's own file says —
#               its settings, its presets and its update check go by it — and its plug-in's entry
#               (createPluginFilter, beside the Product, compiled in under ORBITAMP_PLUGIN_ENTRY) makes
#               a processor of that Product.
#   CODE        its 4-char plug-in code, unique per plugin (OrbitCab holds `Orbt`). LOCK it before the
#               first release: changing it afterwards makes hosts treat it as a different plugin. The
#               manufacturer code is the family's, Dcat (Darwin's Cat — >=1 uppercase, AU rule; same as
#               OrbitCab).
#   BY_NAME     not part of the default build (see below).
#   SOURCES     what it is compiled from; LIBRARIES what it links, ahead of JUCE's recommended flags.
#   ART         its renders, in the factory folder's Faces/<Name>, each under its own file's name, where its
#               face reads them (src/face/Renders.h).
#   SET         the packs it plays, by their folders' names in the tree's set (THE SET), less `.orbitrig` —
#               at the factory folder's top, zipped.
#   WHOLE_SHELF the whole cabinet shelf, the folders the cabinet catalog gives OrbitAmp alone with Darwin's
#               Cat's own; without it, Darwin's Cat's own alone.
#
# Whatever it plays is in the family's factory folder and in its own list there (THE FACTORY FOLDER), never
# in its binary.
#
# Every amp's version is THE FAMILY'S, the project's (above): the amps are released together, so no row
# says a number of its own. What an amp was given is kept on its target, where its gates read it
# (amp_console): a gate is the amp compiled again, never a second copy of its lists.
# ----------------------------------------------------------------------------

# THE CODECS NO AMP READS: an amp opens WAV and AIFF — its demo loops, a player's IRs — and nothing else,
# but registerBasicFormats() (src/core/DemoPlayer.h) pulls in every codec JUCE builds, so the ones it would
# never be handed are not built: Ogg Vorbis, FLAC, and Windows Media. Said for every amp and its gates alike.
set(AMP_UNREAD_CODECS JUCE_USE_OGGVORBIS=0 JUCE_USE_FLAC=0 JUCE_USE_WINDOWS_MEDIA_FORMAT=0)

function(add_amp name)
    cmake_parse_arguments(PARSE_ARGV 1 AMP "BY_NAME;WHOLE_SHELF" "CODE" "SOURCES;LIBRARIES;ART;SET")
    string(TOLOWER "${name}" id)

    # Its icon. macOS 26 sets any icon that is not its own rounded square on a grey plate — the family's
    # disc came out as a small disc on a light square — so the Mac wears the amp's mark on the rounded
    # square the system expects (824 of the 1024 canvas, margins and all), the violet round its edge, and
    # it is shown as drawn. Windows and Linux keep the disc, or the same square on the whole canvas.
    if(APPLE)
        set(icon "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/${name}-macOS-1024.png")
    else()
        set(icon "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/${name}-1024.png")
    endif()

    juce_add_plugin(${name}
        VERSION                     "${PROJECT_VERSION}"
        PRODUCT_NAME                "${name}"
        COMPANY_NAME                "Darwin's Cat"
        BUNDLE_ID                   "com.darwinscat.${id}"
        PLUGIN_MANUFACTURER_CODE    Dcat
        PLUGIN_CODE                 ${AMP_CODE}
        FORMATS                     VST3 AU Standalone
        VST3_CATEGORIES             "Fx" "Distortion"
        AU_MAIN_TYPE                "kAudioUnitType_Effect"
        IS_SYNTH                    FALSE
        NEEDS_MIDI_INPUT            FALSE
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        # Standalone is an effect → it needs the system audio INPUT. Under the macOS hardened
        # runtime that means NSMicrophoneUsageDescription in the app's Info.plist plus the
        # audio-input entitlement at signing time. Plug-in formats run in the host and don't.
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT  "${name} needs audio input to process your signal."
        # Bundle / executable icon. One 1024-square PNG is enough: juceaide builds the macOS .icns for
        # every bundle and the Windows .ico from it. No ICON_SMALL on purpose — see OrbitCab: juceaide
        # reads the pair small-then-big while JUCEUtils appends big-then-small, and passing both can
        # swap the slots.
        ICON_BIG                    "${icon}"
        COPY_PLUGIN_AFTER_BUILD     ${ORBITAMP_COPY_PLUGIN})

    target_sources(${name} PRIVATE ${AMP_SOURCES})

    # Wrap the same JUCE plugin as a CLAP (.clap) — one extra format, no source changes. The wrapper
    # links its own headers into the plug-in, ahead of everything the amp links below.
    clap_juce_extensions_plugin(TARGET ${name}
        CLAP_ID       "com.darwinscat.${id}"
        CLAP_FEATURES audio-effect "guitar" "amplifier")

    # The CLAP wrapper ships its own default clap.icns (the generic CLAP logo) and JUCE's ICON_BIG
    # does not reach it. Overwrite it in place with the AppIcon.icns juceaide already generated for
    # the JUCE formats, keeping the wrapper's Info.plist CFBundleIconFile=clap.icns reference valid.
    # macOS only — the CLAP on Windows/Linux is a plain shared library, no icon resource.
    if(APPLE)
        add_custom_command(TARGET ${name}_CLAP POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy
                    "$<TARGET_PROPERTY:${name},JUCE_ICON_FILE>"
                    "$<TARGET_BUNDLE_DIR:${name}_CLAP>/Contents/Resources/clap.icns"
            VERBATIM)
    endif()

    # THE FAMILY'S version, not the plugin wrapper's. `JucePlugin_VersionString` exists only inside a
    # target juce_add_plugin built, and the headless gates compile the very same UI sources without it —
    # so a face that read it built as a plugin and failed as a test. This one is defined for everything
    # that compiles a face. Which amp it is, is not on the compiler's line: it is the Product the
    # processor is made with (src/Product.h), and only the plug-in's own entry is compiled here, in the
    # amp's own file — a gate, or a binary of several amps, makes its processors itself.
    target_compile_definitions(${name} PUBLIC
        ORBITAMP_VERSION="${PROJECT_VERSION}")
    target_compile_definitions(${name} PRIVATE ORBITAMP_PLUGIN_ENTRY=1)
    target_compile_definitions(${name} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0   # JUCE 8 has no splash requirement on any tier, incl. AGPLv3
        ${AMP_UNREAD_CODECS}           # ...nor a codec it never reads (above)
        $<$<PLATFORM_ID:Windows>:JUCE_ASIO=1>)   # the standalone offers ASIO beside WASAPI; JUCE carries the SDK

    target_include_directories(${name} PUBLIC "${ORBITAMP_GENERATED_DIR}")   # OrbitAmpVersion.h
    add_dependencies(${name} OrbitAmpVersionGen)                            # ...rebaked before every build

    # EVERY AMP LINKS WITH LINK-TIME OPTIMISATION, JUCE's recommended flags — once OrbitAmp's alone: the
    # code a face never calls goes, and a release binary is smaller for it. Its gates link without it
    # (amp_console), in seconds rather than minutes.
    set(libraries ${AMP_LIBRARIES}
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
    target_link_libraries(${name} PRIVATE ${libraries})

    # WHAT IT PLAYS, in the family's factory folder (THE FACTORY FOLDER): its pictures, its packs and
    # Darwin's Cat's own cabinets — and the shelf's other folders, where it plays the whole shelf.
    set(files "")
    set(at "")
    foreach(render IN LISTS AMP_ART)
        get_filename_component(base "${render}" NAME)
        list(APPEND files "${render}")
        list(APPEND at "Faces/${name}/${base}")
    endforeach()
    foreach(pack IN LISTS AMP_SET)
        if(NOT pack IN_LIST ORBITAMP_SET_NAMES)
            message(FATAL_ERROR "${name} plays the pack ${pack}, which resources/set/Devices has not")
        endif()
        list(APPEND files "${ORBITAMP_SET_ZIPPED}/${pack}.orbitrig.zip")
        list(APPEND at "${pack}.orbitrig.zip")
    endforeach()
    list(APPEND files ${ORBITAMP_CABINETS_EVERY_PRODUCT})
    list(APPEND at ${ORBITAMP_CABINETS_EVERY_PRODUCT_AT})
    if(AMP_WHOLE_SHELF)
        list(APPEND files ${ORBITAMP_CABINETS_ORBITAMP})
        list(APPEND at ${ORBITAMP_CABINETS_ORBITAMP_AT})
    endif()
    factory_carries(${name} FILES ${files} AT ${at})
    add_dependencies(${name} Family_Factory)
    if(ORBITAMP_DEV_FACTORY)
        target_compile_definitions(${name} PRIVATE ORBITAMP_DEV_FACTORY="${ORBITAMP_FACTORY_DIR}")
    endif()

    # Every format the amp is built as on this platform — AU on the Mac alone.
    set(formats "")
    foreach(fmt IN ITEMS VST3 AU Standalone CLAP)
        if(TARGET ${name}_${fmt})
            list(APPEND formats ${name}_${fmt})
        endif()
    endforeach()

    # A RELEASE BINARY CARRIES NO LOCAL SYMBOLS — tens of thousands of internal names, a fifth of a face's
    # binary. Its exports stay, which is all a host looks up (GetPluginFactory, the AU's factory,
    # clap_entry): `ld -x` on the Mac, `-s` on Linux, at the link of every format, the CLAP's too. A crash
    # report from a released amp shows addresses, not the names of its own functions. MSVC writes its
    # symbols to a .pdb beside the binary, which no installer packs, and is left as it is.
    foreach(target IN LISTS formats)
        if(APPLE)
            target_link_options(${target} PRIVATE $<$<CONFIG:Release>:LINKER:-x>)
        elseif(UNIX)
            target_link_options(${target} PRIVATE $<$<CONFIG:Release>:-s>)
        endif()
    endforeach()

    # BY_NAME: not part of the default build. OrbitAmp's own build — a contributor's, a pull request's —
    # must never fail on a face. Whoever wants one asks for it by name: one format (`cmake --build build
    # --target CatpitAmp_Standalone`), or all of them — `--target CatpitAmp_All`; the family's release
    # asks for every amp it carries (Family_All).
    if(AMP_BY_NAME)
        set_target_properties(${name} ${formats} PROPERTIES EXCLUDE_FROM_ALL TRUE)
    endif()

    # <Name>_All is every format: the CLAP joins it here, since its wrapper does not add itself.
    add_dependencies(${name}_All ${name}_CLAP)

    set_property(TARGET ${name} PROPERTY AMP_SOURCES ${AMP_SOURCES})
    set_property(TARGET ${name} PROPERTY AMP_LIBRARIES ${libraries})
    set(AMPS ${AMPS} ${name} PARENT_SCOPE)
endfunction()

# AN AMP'S GATES are the amp compiled again under a main() of its own — its sources, its libraries, the
# family's version and its name, as add_amp was given them, and the build's factory folder for what it
# plays — and are built only when asked for by name. The gate makes its processors of the amp's Product,
# ORBITAMP_GATE_PRODUCT — orbitamp::products::<name> with its first letter small (src/Product.h).
# CHANNELS is how many channels the gate holds it to (GATE_CHANNELS), said apart from the amp's lineup.
# NO_FACTORY plays it with no factory folder at all.
function(amp_console target product amp main)
    cmake_parse_arguments(PARSE_ARGV 4 GATE "NO_FACTORY" "CHANNELS" "")
    get_target_property(sources ${amp} AMP_SOURCES)
    get_target_property(libraries ${amp} AMP_LIBRARIES)
    list(REMOVE_ITEM libraries juce::juce_recommended_lto_flags)   # a gate links in seconds, not minutes
    juce_add_console_app(${target} PRODUCT_NAME "${product}")
    target_sources(${target} PRIVATE ${main} ${sources})
    target_include_directories(${target} PRIVATE src "${ORBITAMP_GENERATED_DIR}")
    add_dependencies(${target} OrbitAmpVersionGen)
    string(SUBSTRING "${amp}" 0 1 first)
    string(SUBSTRING "${amp}" 1 -1 rest)
    string(TOLOWER "${first}" first)
    target_compile_definitions(${target} PRIVATE
        ORBITAMP_VERSION="${PROJECT_VERSION}"
        ORBITAMP_PRODUCT_NAME="${amp}"
        ORBITAMP_GATE_PRODUCT=orbitamp::products::${first}${rest})
    if(GATE_CHANNELS)
        target_compile_definitions(${target} PRIVATE GATE_CHANNELS=${GATE_CHANNELS})
    endif()
    target_compile_definitions(${target} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 ${AMP_UNREAD_CODECS})
    target_link_libraries(${target} PRIVATE ${libraries})
    if(GATE_NO_FACTORY)
        gate_reads_factory(${target} "${CMAKE_CURRENT_BINARY_DIR}/no-factory/Factory")
    else()
        gate_reads_factory(${target})
    endif()
    set_target_properties(${target} PROPERTIES EXCLUDE_FROM_ALL TRUE)
endfunction()

# ...and its set as the build lays it out in the factory folder, the set's own reading of it and nothing
# more, beside the family's cabinet shelf. LIBRARIES is what else the gate's reading needs.
function(amp_set_test target product amp main)
    cmake_parse_arguments(PARSE_ARGV 4 GATE "" "CHANNELS" "LIBRARIES")
    juce_add_console_app(${target} PRODUCT_NAME "${product}")
    target_sources(${target} PRIVATE
        ${main}
        src/channels/Set.cpp)
    target_include_directories(${target} PRIVATE src)
    target_compile_definitions(${target} PRIVATE ORBITAMP_PRODUCT_NAME="${amp}")
    if(GATE_CHANNELS)
        target_compile_definitions(${target} PRIVATE GATE_CHANNELS=${GATE_CHANNELS})
    endif()
    target_compile_definitions(${target} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(${target} PRIVATE
        OrbitAmpCabinetCatalog           # the family's cabinet shelf, by the catalog's table
        ${GATE_LIBRARIES}
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    gate_reads_factory(${target})
    set_target_properties(${target} PROPERTIES EXCLUDE_FROM_ALL TRUE)
endfunction()

# ----------------------------------------------------------------------------
# THE SET: every pack and every cabinet a Darwin's Cat amp plays, ONE copy of each in the tree —
# resources/set/Devices for the packs, under their own names, and resources/set/IRs for the cabinets
# (resources/set/IRs.md says what they are). Each product says which of them it plays, and no product
# keeps a folder of its own; the build lays them out in the factory folder (THE FACTORY FOLDER).
# ----------------------------------------------------------------------------
set(ORBITAMP_SET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources/set")

# A PACK IS KEPT AS A FOLDER and carried as a zip. In the tree each pack is a folder,
# resources/set/Devices/<name>.orbitrig — its manifest, its models and its picture side by side, so a
# change to a pack is a change to its rig.json that git can show, not a new blob. Every build zips each
# folder into <name>.orbitrig.zip in the build tree — the file the factory folder holds, under the name it
# always had. .gitattributes keeps git's
# hands off the folders' line endings: what is zipped is the files byte for byte, on every system.
#
# The zip is made by a tool of our own (tools/PackZip.cpp, JUCE's zip writer), not by `cmake -E tar`:
# CMake's zip writer puts into every entry the access and change times the file has on disk, its
# owner, its permissions and a date in the machine's own time zone, whatever --mtime says — so the
# same folder made other bytes on another checkout, another day or another machine, and an unchanged
# pack would be a changed file in every release and every installed folder. Ours
# puts in the files' names and bytes and nothing else, in the order of their names, so the same folder
# makes the same zip on every build and every system.
#
# Each folder's files are listed by a glob, CONFIGURE_DEPENDS, so a file added to a pack, or a pack
# added to Devices, is zipped from the next build on; the list is also written out, and rewritten only
# when it changes, so a file taken out of a pack makes the zip again too (as JUCE's binary data does).
# A name with a leading dot is the Finder's (.DS_Store), not the pack's. Every zip is made by one
# target, OrbitAmpSetPacks, which the factory folder waits for: a rule that several targets ran would
# race itself in a parallel build.
juce_add_console_app(orbitamp_pack_zip PRODUCT_NAME "OrbitAmpPackZip")
target_sources(orbitamp_pack_zip PRIVATE tools/PackZip.cpp)
target_compile_definitions(orbitamp_pack_zip PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(orbitamp_pack_zip PRIVATE
    juce::juce_core
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

set(ORBITAMP_SET_ZIPPED "${CMAKE_CURRENT_BINARY_DIR}/set/Devices")
file(GLOB ORBITAMP_SET_FOLDERS LIST_DIRECTORIES true CONFIGURE_DEPENDS "${ORBITAMP_SET_DIR}/Devices/*.orbitrig")
set(ORBITAMP_SET_PACKS "")
set(ORBITAMP_SET_NAMES "")       # each pack's name, its folder's less `.orbitrig`
foreach(folder IN LISTS ORBITAMP_SET_FOLDERS)
    if(NOT IS_DIRECTORY "${folder}")
        continue()
    endif()
    get_filename_component(pack "${folder}" NAME)
    string(REGEX REPLACE "\\.orbitrig$" "" name "${pack}")
    list(APPEND ORBITAMP_SET_NAMES "${name}")
    file(GLOB files LIST_DIRECTORIES false CONFIGURE_DEPENDS "${folder}/*")
    list(FILTER files EXCLUDE REGEX "/\\.[^/]*$")
    list(SORT files)

    set(listed "${ORBITAMP_SET_ZIPPED}/${pack}.files")
    list(JOIN files "\n" lines)
    set(before "")
    if(EXISTS "${listed}")
        file(READ "${listed}" before)
    endif()
    if(NOT before STREQUAL lines)
        file(WRITE "${listed}" "${lines}")
    endif()

    add_custom_command(OUTPUT "${ORBITAMP_SET_ZIPPED}/${pack}.zip"
        COMMAND orbitamp_pack_zip "${listed}" "${ORBITAMP_SET_ZIPPED}/${pack}.zip"
        DEPENDS orbitamp_pack_zip "${listed}" ${files}
        COMMENT "Zipping ${pack}"
        VERBATIM)
    list(APPEND ORBITAMP_SET_PACKS "${ORBITAMP_SET_ZIPPED}/${pack}.zip")
endforeach()
add_custom_target(OrbitAmpSetPacks DEPENDS ${ORBITAMP_SET_PACKS})

# WHAT ORBITAMP PLAYS: every pack in resources/set/Devices — every folder there, zipped — but the ones
# put away. A pack added there is OrbitAmp's from the next build on, with nobody to remember to list it.
#
# PUT AWAY, the Victory V4's six: in the tree still, for the faces that play them in a build of this tree
# (FrostPawAmp, AmberPawAmp), but no longer the family's devices — the family will play the V4 again from
# new captures, with the power amp inside — and in no amp of the release, so in no installer.
set(ORBITAMP_SET_PUT_AWAY "V4 Copper" "V4 Copper TB" "V4 Sheriff Ch1" "V4 Sheriff Ch2" "V4 Kraken Ch1" "V4 Kraken Ch2")
foreach(pack IN LISTS ORBITAMP_SET_PUT_AWAY)
    if(NOT pack IN_LIST ORBITAMP_SET_NAMES)
        message(FATAL_ERROR "ORBITAMP_SET_PUT_AWAY names ${pack}, which resources/set/Devices has not")
    endif()
endforeach()
set(ORBITAMP_PLAYS ${ORBITAMP_SET_NAMES})
list(REMOVE_ITEM ORBITAMP_PLAYS ${ORBITAMP_SET_PUT_AWAY})
list(LENGTH ORBITAMP_PLAYS ORBITAMP_SET_COUNT)
message(STATUS "OrbitAmp Devices: ${ORBITAMP_SET_COUNT} in the factory folder")

# ----------------------------------------------------------------------------
# Embedded assets. The display FACE is not among them: appkit carries Michroma itself, whole, as
# code, so a product that embedded it too was putting a second copy of the same font in every
# binary and parsing both at startup. The mark still comes from appkit's assets/ — referenced
# there, NOT copied here, so there is one copy of the file in the ecosystem.
# felitronics_appkit_SOURCE_DIR is set by FetchContent above and points at whichever source won
# (local checkout or fetched release).
# ----------------------------------------------------------------------------

# THE CABINET CATALOG (resources/cabinets.txt): every cabinet IR the family carries, each said once — its
# number, its name, the folder of the shelf it lies in and its file. cmake/CabinetCatalog.cmake reads it
# into the files the factory folder holds, in the order of their numbers — Darwin's Cat's own, which every
# amp plays, and the folders OrbitAmp alone plays (add_amp: WHOLE_SHELF) — and into the table the code
# plays them by, generated/CabinetCatalog.h: the `cab_ir` parameter's names and default (src/Parameters.h)
# and the shelf's folders (src/device/FactoryShelf.h). A cabinet joining the shelf is a line there, and its
# file; nothing here or in the code lists one. What compiles Parameters.h links OrbitAmpCabinetCatalog for
# the table — whatever links OrbitAmpData has it already.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/CabinetCatalog.cmake")
orbitamp_cabinet_catalog("${CMAKE_CURRENT_SOURCE_DIR}/resources/cabinets.txt"
                         "${CMAKE_CURRENT_SOURCE_DIR}/resources"
                         "${ORBITAMP_GENERATED_DIR}/CabinetCatalog.h")
add_library(OrbitAmpCabinetCatalog INTERFACE)
target_include_directories(OrbitAmpCabinetCatalog INTERFACE "${ORBITAMP_GENERATED_DIR}")   # CabinetCatalog.h

juce_add_binary_data(OrbitAmpData
    SOURCES
        "${felitronics_appkit_SOURCE_DIR}/assets/catlogo.svg"

        # The trademark notice, as text rather than as a string literal: it is a legal sentence that
        # the README carries word for word, and a file can be diffed against that one.
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/notice.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/disclaimer.txt"   # ...and its long form, with the list
)
# ...and the table the cabinet shelf is played by: its IRs themselves lie in the factory folder.
target_link_libraries(OrbitAmpData INTERFACE OrbitAmpCabinetCatalog)

# ----------------------------------------------------------------------------
# THE SHADERS: every GLSL program the faces draw with — the windscreens' worlds and the cockpit's sky —
# as files in resources/shaders, a folder a world and what several share in common/, put together by
# src/shaders/Shaders.h. One copy of each, as files rather than string literals, so that whatever else
# draws these views — the site, the same C++ in WebAssembly — reads the very same text. Built into the
# binary of every target that compiles a renderer; a file put there is carried from the next build on,
# with nobody to remember to list it. Binary data knows a file by its name alone, and the worlds' files
# share names (plains/sky.frag, ice/sky.frag), so their paths are written beside it in the same order.
# ----------------------------------------------------------------------------
set(ORBITAMP_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources/shaders")
file(GLOB_RECURSE ORBITAMP_SHADER_FILES CONFIGURE_DEPENDS
    "${ORBITAMP_SHADER_DIR}/*.glsl" "${ORBITAMP_SHADER_DIR}/*.vert" "${ORBITAMP_SHADER_DIR}/*.frag")
list(SORT ORBITAMP_SHADER_FILES)
set(_orbitamp_shader_paths "")
foreach(file IN LISTS ORBITAMP_SHADER_FILES)
    file(RELATIVE_PATH path "${ORBITAMP_SHADER_DIR}" "${file}")
    string(APPEND _orbitamp_shader_paths "    \"${path}\",\n")
endforeach()
file(CONFIGURE OUTPUT "${ORBITAMP_GENERATED_DIR}/ShaderPaths.h" @ONLY CONTENT
"// Written by CMakeLists.txt (THE SHADERS): the paths in resources/shaders of the files ShaderData carries, in its order.
#pragma once
namespace orbitamp::shaders
{
inline constexpr const char* embeddedPaths[] =
{
@_orbitamp_shader_paths@};
}
")
juce_add_binary_data(OrbitAmpShaders
    NAMESPACE   ShaderData
    HEADER_NAME ShaderData.h
    SOURCES     ${ORBITAMP_SHADER_FILES})
target_include_directories(OrbitAmpShaders INTERFACE "${ORBITAMP_GENERATED_DIR}")   # ShaderPaths.h
set_target_properties(OrbitAmpShaders PROPERTIES EXCLUDE_FROM_ALL TRUE)   # OrbitAmp itself draws none

# BAKE-ART — the faces' pictures as they ship (tools/bake-art/BakeArt.cpp): each face's renders as painted,
# in resources/<face>/source, corrected and cut once into the lossless WebP its product carries, in
# resources/<face>. No product builds it or waits for it: it is run by hand, on a Mac, when a render or a
# number it was measured to changes — `--target orbitamp_bake_art`, then the tool, or the tool --check.
juce_add_console_app(orbitamp_bake_art PRODUCT_NAME "OrbitAmpBakeArt")
target_sources(orbitamp_bake_art PRIVATE
    tools/bake-art/BakeArt.cpp
    tools/bake-art/Rover.cpp
    tools/bake-art/Cockpit.cpp
    src/face/Renders.cpp)
target_include_directories(orbitamp_bake_art PRIVATE src)
target_compile_definitions(orbitamp_bake_art PRIVATE
    ORBITAMP_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
target_link_libraries(orbitamp_bake_art PRIVATE
    felitronics::appkit_webp
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
set_target_properties(orbitamp_bake_art PROPERTIES EXCLUDE_FROM_ALL TRUE)

# ----------------------------------------------------------------------------
# WHAT THE AMPS ARE MADE OF — the lists their rows in THE AMPS take up, each said once.
# ----------------------------------------------------------------------------
# The engine every amp plays through, and what each part of it is for.
set(AMP_ENGINE_LIBRARIES
    felitronics::analysis            # spectrum taps + panes behind the EQ curves
    felitronics::appkit              # shared app-side kit (header-only; JUCE comes from us)
    felitronics::appkit_webp         # juce::Image <- WebP, for the picture a pack ships
    felitronics::core                # core::BypassWire reads the resampler's delay geometry
    felitronics::dehum               # the hum off the input, ahead of the trim
    felitronics::delay               # the echo (TapeDelay) and its knob law — sync, DARK, ranges
    felitronics::reverb              # the room (AmpReverb), and the reverb face's picture of it
    felitronics::dynamics            # the noise gate (felitronics::dynamics::NoiseGate)
    felitronics::eq                  # matched biquads behind the tone stack (core::ToneStack)
    felitronics::lineareq            # the tone curve read off the player's grid (core::CapturedBlock)
    felitronics::convolution         # the cabinet's convolver (core::CabinetIr → CabConvolver, pffft)
    felitronics::measurement         # reference-unity IR normalization (core::CabinetIr)
    felitronics::nam                 # NAM inference — the captured devices actually play
    felitronics::rigplayer           # the pack player: captures, their crossfade, tone, alignment
    felitronics::limiter             # the safety at the door: a true-peak limiter with lookahead
    juce::juce_audio_utils           # pulls audio_processors / audio_basics / gui_basics ...
    juce::juce_dsp                   # the chain stages will build on this
    juce::juce_data_structures)      # ValueTree behind the workspace / presets

# OrbitAmp — the player: the processor, its face (src/ui), and OrbitAmp itself — its Product, its first
# start, its plug-in's entry (OrbitAmpStart.cpp).
set(ORBITAMP_SOURCES
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/OrbitAmpStart.cpp
    src/Parameters.cpp
    src/ui/Theme.cpp
    src/ui/FaceplateView.cpp
    src/ui/CapturedBlockPanel.cpp
    src/ui/CabinetBlock.cpp
    src/ui/EqSection.cpp
    src/ui/DelayBlock.cpp
    src/ui/ReverbBlock.cpp
    src/ui/Chrome.cpp
    src/ui/DemoStrip.cpp
    src/ui/Footer.cpp
    src/ui/SetupPanel.cpp
    src/ui/library/DeviceListView.cpp
    src/ui/library/IrTreeView.cpp)

# A FACE is the family's console (src/face) over the family's channels (src/channels), on the same
# processor: what the cockpit and every rover are compiled from before their own.
set(FACE_SOURCES
    src/PluginProcessor.cpp
    src/Parameters.cpp
    src/ui/Theme.cpp
    src/core/ProcessMemory.cpp
    src/channels/Channels.cpp
    src/channels/Engine.cpp
    src/channels/Presets.cpp
    src/channels/Set.cpp
    src/face/Cabs.cpp
    src/face/Console.cpp
    src/face/Controls.cpp
    src/face/Midi.cpp
    src/face/Monitor.cpp
    src/face/Picture.cpp
    src/face/Renders.cpp
    src/face/Sheet.cpp
    src/face/Standalone.cpp)

# CatpitAmp — the cockpit: a second face over the same processor (src/catpit), one processor and a face
# for each product, which the product names (src/Product.h). Its renders are in resources/catpit, as
# bake-art made them, and lie in its own folder of the factory, Faces/CatpitAmp. What the cockpit is made
# of is said once, for the plug-in and for its gates, which are the cockpit compiled again under a main()
# of their own.
set(CATPITAMP_RENDERS
    cockpit.webp lamp-0.webp lamp-1.webp lamp-2.webp lamp-3.webp midi.txt knob-drive.webp knob-tone.webp
    throttle-grip.webp guard-closed.webp guard-open.webp guard-on.webp fader-cap.webp sky-clean.jpg
    sky-edge.jpg sky-crunch.jpg sky-hi-gain.jpg mark.svg)
list(TRANSFORM CATPITAMP_RENDERS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/resources/catpit/")
set(CATPITAMP_SOURCES
    ${FACE_SOURCES}
    src/catpit/CatpitArt.cpp
    src/catpit/CatpitControls.cpp
    src/catpit/CatpitLineup.cpp
    src/catpit/CatpitSpace.cpp
    src/catpit/CatpitGlSky.cpp
    src/catpit/CatpitSkyGl.cpp
    src/catpit/CatpitEditor.cpp
    src/shaders/Shaders.cpp)

# THE WORLDS a windscreen looks out on — views that take values and no processor, so they stand on their
# own. RedPawAmp's is the drive across Mars (src/redpaw/view); FrostPawAmp's the same windscreen looking
# out on an ice moon (src/frostpaw/view), the drive handed to it as an IceDrive; AmberPawAmp's the same
# windscreen as a helicopter's canopy over Titan's methane sea (src/amberpaw/view), the drive handed to
# it as a TitanDrive. Each rover takes up its world's sources with the windscreen's own.
set(REDPAW_VIEW_SOURCES
    src/redpaw/view/Mars1993.cpp
    src/redpaw/view/PlainsWorld.cpp
    src/redpaw/view/PlainsGl.cpp
    src/redpaw/view/Windscreen.cpp
    src/shaders/Shaders.cpp)
set(FROSTPAW_VIEW_SOURCES
    src/frostpaw/view/IceWorld.cpp
    src/frostpaw/view/IceGl.cpp)
set(AMBERPAW_VIEW_SOURCES
    src/amberpaw/view/TitanWorld.cpp
    src/amberpaw/view/TitanGl.cpp)

# A world's demo: a bare window with the windscreen in it over one world — the sea's in the canopy's
# shape — for looking at it and measuring it. Built only when asked for by name. No audio.
function(view_demo target product)
    cmake_parse_arguments(PARSE_ARGV 2 DEMO "" "" "SOURCES;DEFINITIONS")
    juce_add_gui_app(${target} PRODUCT_NAME "${product}")
    target_sources(${target} PRIVATE src/redpaw/view/WindscreenDemo.cpp ${DEMO_SOURCES})
    target_include_directories(${target} PRIVATE src)
    target_compile_definitions(${target} PRIVATE ${DEMO_DEFINITIONS} JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(${target} PRIVATE
        OrbitAmpShaders
        juce::juce_gui_extra
        juce::juce_opengl
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    set_target_properties(${target} PROPERTIES EXCLUDE_FROM_ALL TRUE)
endfunction()
view_demo(redpaw_view_demo   "RedPawViewDemo"   SOURCES ${REDPAW_VIEW_SOURCES})
view_demo(frostpaw_view_demo "FrostPawViewDemo" SOURCES ${REDPAW_VIEW_SOURCES} ${FROSTPAW_VIEW_SOURCES}
    DEFINITIONS ORBITAMP_VIEW_DEMO_ICE=1)
view_demo(amberpaw_view_demo "AmberPawViewDemo" SOURCES ${REDPAW_VIEW_SOURCES} ${AMBERPAW_VIEW_SOURCES}
    DEFINITIONS ORBITAMP_VIEW_DEMO_TITAN=1)

# The sea's CPU half against the prototype it was ported from: the start, five scripted flights, the
# gears, Saturn, the wipers' panes. JUCE-free, links only the world.
add_executable(titan_world_test tests/TitanWorldTests.cpp src/amberpaw/view/TitanWorld.cpp)
target_include_directories(titan_world_test PRIVATE src)
target_compile_features(titan_world_test PRIVATE cxx_std_20)

# THE ROVER'S FACE IS WORN BY MORE THAN ONE PRODUCT (src/redpaw/RedPawSkin.h): what every rover is made
# of is said once here — the renders by the file names the face asks for, as bake-art made them, the
# sources, the libraries — and each product adds its own skin's file and its own renders. The lamps a
# dashboard paints lit, cut out of it, are a rover's own too: there where its picture paints them.
set(ROVER_RENDERS
    dashboard.webp midi.txt mark.svg knob-drive.webp knob-tone.webp lever.webp lever-glow.webp
    slider-cap.webp fader-cap.webp fader-cap-light.webp toggle-up.webp toggle-down.webp key-up.webp
    key-down.webp turbo-up.webp turbo-down.webp)
set(ROVER_LAMPS lamp-0.webp lamp-1.webp lamp-2.webp lamp-3.webp)

set(REDPAWAMP_RENDERS ${ROVER_RENDERS} ${ROVER_LAMPS} turbo-lid.webp)     # ...its lamps, and TURBO's hatch shut, its own
list(TRANSFORM REDPAWAMP_RENDERS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/resources/redpaw/")
# ...and the STILL it ships, one frame of its world, where there is one: never another rover's.
file(GLOB REDPAWAMP_STILL CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/redpaw/still.jpg")
list(APPEND REDPAWAMP_RENDERS ${REDPAWAMP_STILL})

# Every other rover's renders are in resources/<face>, under the rover's names; one that is not there
# yet is RedPawAmp's. So a render of its own is a file dropped in there and nothing more: the folder is
# looked at again on every build. ...and renders a rover may carry or not — the maker's mark raised in
# its own metal, instead of engraved; TURBO's hatch shut, instead of open on PARK too: there when the
# file is, and nothing in its place when it is not. Never RedPawAmp's: a rusted hatch on frosted steel
# would be a patch.
function(rover_renders out face)
    set(folder "${CMAKE_CURRENT_SOURCE_DIR}/resources/${face}")
    file(GLOB own CONFIGURE_DEPENDS "${folder}/*")
    set(renders "")
    foreach(render IN LISTS ROVER_RENDERS)
        if("${folder}/${render}" IN_LIST own)
            list(APPEND renders "${folder}/${render}")
        else()
            list(APPEND renders "${CMAKE_CURRENT_SOURCE_DIR}/resources/redpaw/${render}")
        endif()
    endforeach()
    foreach(render IN ITEMS emblem.webp turbo-lid.webp still.jpg ${ROVER_LAMPS})
        if("${folder}/${render}" IN_LIST own)
            list(APPEND renders "${folder}/${render}")
        endif()
    endforeach()
    set(${out} "${renders}" PARENT_SCOPE)
endfunction()
rover_renders(FROSTPAWAMP_RENDERS frostpaw)
rover_renders(AMBERPAWAMP_RENDERS amberpaw)

# What a rover is made of — for the plug-in and for its gates, which are the rover compiled again
# under a main() of their own — before its skin and its world.
set(ROVER_SOURCES
    ${FACE_SOURCES}
    src/redpaw/RedPawArt.cpp
    src/redpaw/RedPawControls.cpp
    src/redpaw/RedPawLineup.cpp
    src/redpaw/RedPawEditor.cpp
    ${REDPAW_VIEW_SOURCES})
set(ROVER_LIBRARIES
    OrbitAmpData
    OrbitAmpShaders                  # the windscreens' shaders
    ${AMP_ENGINE_LIBRARIES}
    juce::juce_opengl)               # the windscreen, on the GPU

# ----------------------------------------------------------------------------
# THE AMPS — the family's table, one row an amp (THE FAMILY says what a row says).
#
# The amps are RELEASED TOGETHER, as Darwin's Cat Amps: one version, the project's, and one tag, vX.Y.Z,
# for every amp installer/family.ini names (.github/workflows/release.yml). Until 0.8.0 the cockpit had a
# version and tags of its own, catpit-vX.Y.Z; from 0.8.0 on every amp's update check reads the family's.
# ----------------------------------------------------------------------------

# OrbitAmp — the player of the whole library, and the default build. It plays THE SET whole but what is
# put away (ORBITAMP_PLAYS, above), and the whole cabinet shelf.
add_amp(OrbitAmp      CODE Oamp
    SOURCES   ${ORBITAMP_SOURCES}
    LIBRARIES
        OrbitAmpData                 # the mark, the notice, the disclaimer — BinaryData::*
        ${AMP_ENGINE_LIBRARIES}
    SET       ${ORBITAMP_PLAYS}
    WHOLE_SHELF)

# CatpitAmp — the cockpit. Out of the tree's set it plays what its four channels play: the X88-IR's four
# packs — a channel finds its own by the capture it holds, not by the file's name. The cabinets CAB steps
# through are Darwin's Cat's own of the family's shelf. All of it lies in the factory folder, under the
# cockpit's own list there (src/channels/Set.h); without it, the cockpit would play only what the player
# puts in the Devices folder.
add_amp(CatpitAmp     CODE Ctpt    BY_NAME
    ART       ${CATPITAMP_RENDERS}
    SOURCES   ${CATPITAMP_SOURCES}
    LIBRARIES
        OrbitAmpData
        OrbitAmpShaders              # the sky's shaders
        ${AMP_ENGINE_LIBRARIES}
        juce::juce_opengl            # the sky, on the GPU
    SET       "X88-IR Normal" "X88-IR Altered" "X88-IR Crunch" "X88-IR Overdrive")

# RedPawAmp — the rover: a third face over the same processor (src/redpaw), the dashboard of a Mars rover
# with the drive across Mars in its windscreen. Four channels — the Friedman IR-X's two, each with its
# Boost off and on — picked as two gears and TURBO. Built like the cockpit: the family's console over the
# family's channels, its own renders in resources/redpaw, and its own set — the packs its channels play,
# the IR-X's two, each found by the capture it holds.
add_amp(RedPawAmp     CODE Rdpw    BY_NAME
    ART       ${REDPAWAMP_RENDERS}
    SOURCES   ${ROVER_SOURCES} src/redpaw/RedPawSkin.cpp
    LIBRARIES ${ROVER_LIBRARIES}
    SET       "IR-X Ch1 PowerAmp" "IR-X Ch2 PowerAmp")

# FrostPawAmp — the ice moon's rover: a fourth face over the same processor, RedPawAmp's dashboard worn
# over a picture of its own (src/frostpaw/FrostPawSkin.cpp) — frosted stainless steel, amber lamps,
# ice-blue rings. The same controls, TURBO and PARK: the rover's, shared, not copied — over channels of
# its own, six on the V4's six packs in three gears, and the rovers' cabinets; its set under its own list
# in the family's folder (src/channels/Set.h), where a file another product plays too is one file. Not in
# the family's release yet (installer/family.ini), so none of it is in the release's factory folder.
add_amp(FrostPawAmp   CODE Frpw    BY_NAME
    ART       ${FROSTPAWAMP_RENDERS}
    SOURCES   ${ROVER_SOURCES} ${FROSTPAW_VIEW_SOURCES} src/frostpaw/FrostPawSkin.cpp
    LIBRARIES ${ROVER_LIBRARIES}
    SET       "V4 Copper" "V4 Copper TB" "V4 Sheriff Ch1" "V4 Sheriff Ch2" "V4 Kraken Ch1" "V4 Kraken Ch2")

# AmberPawAmp — a helicopter over Titan's methane sea: a fifth face over the same processor, the rovers'
# dashboard worn over a cockpit of its own (src/amberpaw/AmberPawSkin.cpp) — dark gunmetal under a canopy
# of three panes, its tuner a gauge, its meters dots — and out of its canopy, the flight over the sea.
# The same controls, TURBO and PARK: the rovers', shared, not copied — over the V4's six channels in three
# gears, its set for now FrostPawAmp's until it has captures of its own, and the rovers' cabinets. Not in
# the family's release yet (installer/family.ini).
add_amp(AmberPawAmp   CODE Ambp    BY_NAME
    ART       ${AMBERPAWAMP_RENDERS}
    SOURCES   ${ROVER_SOURCES} ${AMBERPAW_VIEW_SOURCES} src/amberpaw/AmberPawSkin.cpp
    LIBRARIES ${ROVER_LIBRARIES}
    SET       "V4 Copper" "V4 Copper TB" "V4 Sheriff Ch1" "V4 Sheriff Ch2" "V4 Kraken Ch1" "V4 Kraken Ch2")

# ----------------------------------------------------------------------------
# THE FACES' GATES — each a face's own, so built only by name (amp_console, amp_set_test).
# ----------------------------------------------------------------------------
# The cockpit's memory probe — not a gate, a measuring instrument: what the cockpit holds with its
# window built, and with it SHUT, which is the figure that matters in a host and the one the
# standalone cannot give.
amp_console(catpitamp_memory_probe  "CatpitAmpMemoryProbe"  CatpitAmp  tests/MemoryProbe.cpp)

# AN AMP'S GATES, the same two for the cockpit and every rover, run as that amp — what tells one amp
# from another, the gates read in tests/AmpGate.h. Its channels with no window at all — as many as
# CHANNELS says, each with its own memory and what it opens on, moved by CHANNEL the way a host moves
# it; the rover's gears and the Boost each holds, PARK, the cockpit's DRIVE on its dial — and its set as
# it — and its set as the build lays it out in the factory folder, under its own list, whole and nothing
# else; that one reads pack manifests (DeviceLibrary.h, through namz).
set(AMP_SET_GATE_LIBRARIES felitronics::nam juce::juce_core juce::juce_events juce::juce_gui_basics)
amp_console(catpitamp_channel_test    "CatpitAmpChannelTest"    CatpitAmp    tests/AmpChannelTests.cpp  CHANNELS 4)
amp_set_test(catpitamp_set_test       "CatpitAmpSetTest"        CatpitAmp    tests/AmpSetTests.cpp      CHANNELS 4
    LIBRARIES ${AMP_SET_GATE_LIBRARIES})
amp_console(redpawamp_channel_test    "RedPawAmpChannelTest"    RedPawAmp    tests/AmpChannelTests.cpp  CHANNELS 4)
amp_set_test(redpawamp_set_test       "RedPawAmpSetTest"        RedPawAmp    tests/AmpSetTests.cpp      CHANNELS 4
    LIBRARIES ${AMP_SET_GATE_LIBRARIES})
amp_console(frostpawamp_channel_test  "FrostPawAmpChannelTest"  FrostPawAmp  tests/AmpChannelTests.cpp  CHANNELS 6)
amp_set_test(frostpawamp_set_test     "FrostPawAmpSetTest"      FrostPawAmp  tests/AmpSetTests.cpp      CHANNELS 6
    LIBRARIES ${AMP_SET_GATE_LIBRARIES})
amp_console(amberpawamp_channel_test  "AmberPawAmpChannelTest"  AmberPawAmp  tests/AmpChannelTests.cpp  CHANNELS 6)
amp_set_test(amberpawamp_set_test     "AmberPawAmpSetTest"      AmberPawAmp  tests/AmpSetTests.cpp      CHANNELS 6
    LIBRARIES ${AMP_SET_GATE_LIBRARIES})

# AN AMP WITH NO FACTORY FOLDER — copied off a zip without it, or the folder deleted: it plays, and says
# where the folder belongs instead of opening an empty face (src/ui/FactoryMissing.h). OrbitAmp, the
# cockpit and the rovers' dashboard, which FrostPawAmp and AmberPawAmp wear too.
amp_console(orbitamp_no_factory_test  "OrbitAmpNoFactoryTest"   OrbitAmp     tests/NoFactoryTests.cpp   NO_FACTORY)
amp_console(catpitamp_no_factory_test "CatpitAmpNoFactoryTest"  CatpitAmp    tests/NoFactoryTests.cpp   NO_FACTORY)
amp_console(redpawamp_no_factory_test "RedPawAmpNoFactoryTest"  RedPawAmp    tests/NoFactoryTests.cpp   NO_FACTORY)

# ----------------------------------------------------------------------------
# THE FAMILY'S RELEASE: `--target Family_All` builds every format of every amp installer/family.ini
# names — the list each family installer packs (build-family-pkg.sh, darwinscat-amps.iss,
# build-family-linux.sh) — so what is built for the release and what is packed are one list, said in one
# place. The file is read the way those read it, by its `amps=` line, and is looked at again when it
# changes; a name there that is no amp here stops the configure, since an installer would go looking for
# bundles nothing builds.
# ----------------------------------------------------------------------------
set(FAMILY_INI "${CMAKE_CURRENT_SOURCE_DIR}/installer/family.ini")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FAMILY_INI}")
file(STRINGS "${FAMILY_INI}" FAMILY_AMPS REGEX "^amps=")
string(REGEX REPLACE "^amps=" "" FAMILY_AMPS "${FAMILY_AMPS}")
string(REGEX MATCHALL "[^ \t\r]+" FAMILY_AMPS "${FAMILY_AMPS}")
if(NOT FAMILY_AMPS)
    message(FATAL_ERROR "installer/family.ini names no amps")
endif()
add_custom_target(Family_All)
foreach(amp IN LISTS FAMILY_AMPS)
    if(NOT amp IN_LIST AMPS)
        message(FATAL_ERROR "installer/family.ini names ${amp}, which is no amp of this tree (${AMPS})")
    endif()
    add_dependencies(Family_All ${amp}_All)
endforeach()
message(STATUS "The family's release (Family_All): ${FAMILY_AMPS}")

# ...and the factory folder of every amp, and of the family's alone (THE FACTORY FOLDER).
family_factory()

# ----------------------------------------------------------------------------
# Headless acceptance gate for the EQ link. JUCE-free — it links only felitronics::eq, which is
# the point of keeping core::EqLink free of JUCE: the DSP is testable without a host, a GUI, or
# an audio device. Non-zero exit on any failure.
# ----------------------------------------------------------------------------
# Diagnostics, not gates: what the stage sees in packs, how hot the chain runs, and offline
# listening renders — the instruments that caught the fuzz.
function(orbitamp_probe target product main)
    juce_add_console_app(${target} PRODUCT_NAME "${product}")
    target_sources(${target} PRIVATE ${main})
    target_include_directories(${target} PRIVATE src)
    target_compile_definitions(${target} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(${target} PRIVATE
        felitronics::lineareq felitronics::nam felitronics::rigplayer felitronics::limiter
        felitronics::delay felitronics::reverb   # Parameters.h names the delay's ladder and the room's characters from them
        OrbitAmpCabinetCatalog                   # ...and the cabinets from the catalog's table
        juce::juce_dsp juce::juce_events juce::juce_gui_basics juce::juce_audio_formats juce::juce_audio_processors
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endfunction()
orbitamp_probe(orbitamp_pack_probe    "OrbitAmpPackProbe"    tests/PackProbe.cpp)
orbitamp_probe(orbitamp_render_probe  "OrbitAmpRenderProbe"  tests/RenderProbe.cpp)
orbitamp_probe(orbitamp_level_probe   "OrbitAmpLevelProbe"   tests/LevelProbe.cpp)

# The NAM bench — not a gate, a measuring instrument: ns/frame through the shipping stage.
add_executable(orbitamp_nam_bench tests/NamBench.cpp)
target_link_libraries(orbitamp_nam_bench PRIVATE felitronics::nam juce::juce_dsp juce::juce_core)
# WEB_BROWSER/CURL off like every other juce-linking gate: without it juce_core reaches for
# curl/curl.h, which a bare Linux CI runner does not have — the first Linux build found it.
target_compile_definitions(orbitamp_nam_bench PRIVATE JUCE_STANDALONE_APPLICATION=1
                                                       JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

add_executable(orbitamp_eqlink_test tests/EqLinkTests.cpp)
target_include_directories(orbitamp_eqlink_test PRIVATE src)
target_link_libraries(orbitamp_eqlink_test PRIVATE felitronics::eq)
target_compile_features(orbitamp_eqlink_test PRIVATE cxx_std_20)

# Same gate for the tuner's ear, and JUCE-free for the same reason: signals with a KNOWN pitch go
# in, the cents error comes out. Links nothing at all — core::PitchTracker is plain C++.
add_executable(orbitamp_tuner_test tests/TunerTests.cpp)
target_include_directories(orbitamp_tuner_test PRIVATE src)
target_compile_features(orbitamp_tuner_test PRIVATE cxx_std_20)

# Same gate for the gate's LEARN: known traces in, the threshold out — and the arithmetic held to
# what OrbitAmp's GateConsole did before the faces came to share it. core::GateLearn is plain C++.
add_executable(orbitamp_gatelearn_test tests/GateLearnTests.cpp)
target_include_directories(orbitamp_gatelearn_test PRIVATE src)
target_compile_features(orbitamp_gatelearn_test PRIVATE cxx_std_20)

# Same gate for the reverb — felitronics::reverb's AmpReverb, the room this plugin plays. It needs no
# JUCE any more (the room no longer stands on the framework's Freeverb); it stays a console app so its
# binary lands where the release workflows look for every gate's.
juce_add_console_app(orbitamp_reverb_test PRODUCT_NAME "OrbitAmpReverbTest")
target_sources(orbitamp_reverb_test PRIVATE tests/ReverbStageTests.cpp)
target_include_directories(orbitamp_reverb_test PRIVATE src)
target_compile_definitions(orbitamp_reverb_test PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(orbitamp_reverb_test PRIVATE
    felitronics::reverb
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ----------------------------------------------------------------------------
# Standalone code signing (macOS, dev loop).
#
# JUCE ad-hoc signs the app, and an ad-hoc signature is recomputed from the binary's contents — so
# every rebuild is a DIFFERENT app as far as macOS is concerned, and TCC cannot remember the
# microphone grant. Result: the permission dialog on every single launch.
#
# Signing with a stable Developer ID fixes that: the grant is keyed to the identity + bundle ID, so
# it is asked once and remembered across rebuilds. The plug-ins are untouched — they run inside a
# host, which owns the audio input.
#
# The identity is a PREFIX, not a full name: codesign resolves it against the login keychain, so no
# personal identity is baked into this public repo. If no Developer ID is installed (CI, a clean
# machine) the step is skipped and the ad-hoc build still works — it just keeps asking.
# ----------------------------------------------------------------------------
if(APPLE)
    set(ORBITAMP_SIGN_IDENTITY "Developer ID Application" CACHE STRING
        "codesign identity (prefix match) for the standalone; empty disables signing")

    if(ORBITAMP_SIGN_IDENTITY)
        execute_process(COMMAND security find-identity -v -p codesigning
                        OUTPUT_VARIABLE ORBITAMP_CODESIGN_IDS ERROR_QUIET)
        string(FIND "${ORBITAMP_CODESIGN_IDS}" "${ORBITAMP_SIGN_IDENTITY}" ORBITAMP_ID_FOUND)
    else()
        set(ORBITAMP_ID_FOUND -1)
    endif()

    if(ORBITAMP_ID_FOUND GREATER -1)
        message(STATUS "Standalone signing: '${ORBITAMP_SIGN_IDENTITY}' — the mic prompt is asked once, not per build")
        # --options runtime turns the hardened runtime on, which is what makes the entitlement
        # REQUIRED rather than advisory — the same posture a released build ships with.
        # --timestamp=none keeps the dev loop offline; notarization would need a real timestamp.
        # Every amp's standalone asks for the same microphone.
        foreach(amp IN LISTS AMPS)
            if(TARGET ${amp}_Standalone)
                add_custom_command(TARGET ${amp}_Standalone POST_BUILD
                    COMMAND codesign --force --options runtime --timestamp=none
                            --entitlements "${CMAKE_CURRENT_SOURCE_DIR}/installer/standalone.entitlements"
                            --sign "${ORBITAMP_SIGN_IDENTITY}"
                            "$<TARGET_BUNDLE_DIR:${amp}_Standalone>"
                    COMMENT "${amp}: signing the standalone so macOS remembers the mic grant"
                    VERBATIM)
            endif()
        endforeach()
    else()
        message(STATUS "Standalone signing: no '${ORBITAMP_SIGN_IDENTITY}' identity — staying ad-hoc (mic prompt every launch)")
    endif()

    # A REBUILD NEVER MOVES THE BUNDLE'S OWN DATE — only what is inside it changes — and that date is
    # what macOS keys its icon cache on. A bundle first built before it had an icon kept the blank
    # placeholder in the Dock and the app switcher for good, with the right icon sitting inside it.
    # So the bundle is touched after every build, and the system looks again.
    foreach(amp IN LISTS AMPS)
        if(TARGET ${amp}_Standalone)
            add_custom_command(TARGET ${amp}_Standalone POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E touch_nocreate "$<TARGET_BUNDLE_DIR:${amp}_Standalone>"
                VERBATIM)
        endif()
    endforeach()
endif()

# ----------------------------------------------------------------------------
# Gate for the waveform picture's MEMORY — what it recorded must not be rewritten later.
# ----------------------------------------------------------------------------
juce_add_console_app(orbitamp_ribbon_test PRODUCT_NAME "OrbitAmpRibbonTest")
target_sources(orbitamp_ribbon_test PRIVATE tests/WaveRibbonTests.cpp)
target_include_directories(orbitamp_ribbon_test PRIVATE src)
target_compile_definitions(orbitamp_ribbon_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(orbitamp_ribbon_test PRIVATE
    juce::juce_audio_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ----------------------------------------------------------------------------
# Gate for the ASSEMBLED plugin: move a control, hear the output change. Links the plugin's own
# sources rather than a stage of it, because what this checks is the wiring between them.
# ----------------------------------------------------------------------------
juce_add_console_app(orbitamp_chain_test PRODUCT_NAME "OrbitAmpChainTest")
target_sources(orbitamp_chain_test PRIVATE tests/ChainTests.cpp ${ORBITAMP_SOURCES})   # it plays OrbitAmp with no first start
target_include_directories(orbitamp_chain_test PRIVATE src)
target_include_directories(orbitamp_chain_test PRIVATE "${ORBITAMP_GENERATED_DIR}")   # the same faces, the same stamp
add_dependencies(orbitamp_chain_test OrbitAmpVersionGen)
target_compile_definitions(orbitamp_chain_test PRIVATE ORBITAMP_VERSION="${PROJECT_VERSION}"
                                                      JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(orbitamp_chain_test PRIVATE
    OrbitAmpData
    ${AMP_ENGINE_LIBRARIES}
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
gate_reads_factory(orbitamp_chain_test)   # OrbitAmp's whole shelf, and its packs: the gate plays every number of it

# ----------------------------------------------------------------------------
# Gate for the hum at the input, heard through the processor's own input path — the tuner's tap,
# which listens after the trim: AUTO takes a mains hum off; an INPUT trim moved mid-stream does not
# bring it back, because the stage sits ahead of the trim; OFF hands the input on bit for bit; and a
# demo loop with no hum in it passes untouched. Needs no pack: the input path is ahead of every block.
# ----------------------------------------------------------------------------
juce_add_console_app(orbitamp_hum_test PRODUCT_NAME "OrbitAmpHumTest")
target_sources(orbitamp_hum_test PRIVATE tests/HumTests.cpp ${ORBITAMP_SOURCES})       # ...and so does this one
target_include_directories(orbitamp_hum_test PRIVATE src)
target_include_directories(orbitamp_hum_test PRIVATE "${ORBITAMP_GENERATED_DIR}")
add_dependencies(orbitamp_hum_test OrbitAmpVersionGen)
target_compile_definitions(orbitamp_hum_test PRIVATE ORBITAMP_VERSION="${PROJECT_VERSION}"
                                                    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(orbitamp_hum_test PRIVATE
    OrbitAmpData
    ${AMP_ENGINE_LIBRARIES}
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
gate_reads_factory(orbitamp_hum_test)     # OrbitAmp's processor, its whole shelf with it

# ----------------------------------------------------------------------------
# Gate for reading an .orbitrig pack and playing it through the block the plugin plays it with —
# every installed pack sounds, the dial mixes without a step, a linked position is fed softer, a
# tone knob of either form reaches the audio, and OURS parks the device's own. Skips cleanly when
# no pack is installed.
# ----------------------------------------------------------------------------
juce_add_console_app(orbitamp_captured_test PRODUCT_NAME "OrbitAmpCapturedTest")
target_sources(orbitamp_captured_test PRIVATE tests/CapturedBlockTests.cpp)
target_include_directories(orbitamp_captured_test PRIVATE src)
target_compile_definitions(orbitamp_captured_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(orbitamp_captured_test PRIVATE
    felitronics::lineareq
    felitronics::nam
    felitronics::rigplayer
    felitronics::limiter
    felitronics::delay               # Parameters.h takes the delay's ladder and ranges from it...
    felitronics::reverb              # ...and the room's characters
    OrbitAmpCabinetCatalog           # ...and the cabinets, from the catalog's table
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_events
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ----------------------------------------------------------------------------
# Gate for the library manager's FILE WORK — imports, zip unpacking, renames, and the guards that
# keep every destructive operation inside the library. Runs entirely in a temp folder.
# ----------------------------------------------------------------------------
juce_add_console_app(orbitamp_library_test PRODUCT_NAME "OrbitAmpLibraryTest")
target_sources(orbitamp_library_test PRIVATE tests/LibraryTests.cpp)
target_include_directories(orbitamp_library_test PRIVATE src)
target_compile_definitions(orbitamp_library_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
                                                        ORBITAMP_SET_DIR="${ORBITAMP_SET_DIR}")   # the set's manifests, read in place
target_link_libraries(orbitamp_library_test PRIVATE
    felitronics::nam                 # DeviceLibrary.h reads pack manifests through namz
    juce::juce_events
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ----------------------------------------------------------------------------
# Gate for OrbitAmp's set as the build lays it out in the factory folder — every pack OrbitAmp plays, under
# a name the device list tells apart, none of what is put away, and the whole cabinet shelf in its folders;
# the list it reads is the build's own, <build>/Factory/sets/OrbitAmp.txt.
# ----------------------------------------------------------------------------
juce_add_console_app(orbitamp_set_test PRODUCT_NAME "OrbitAmpSetTest")
target_sources(orbitamp_set_test PRIVATE tests/OrbitAmpSetTests.cpp)
target_include_directories(orbitamp_set_test PRIVATE src)
target_compile_definitions(orbitamp_set_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(orbitamp_set_test PRIVATE
    OrbitAmpCabinetCatalog           # the family's cabinet shelf, by the catalog's table
    felitronics::nam                 # DeviceLibrary.h reads pack manifests through namz
    juce::juce_events
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
gate_reads_factory(orbitamp_set_test)

# ----------------------------------------------------------------------------
# Gate for the family's factory folder with more than one product reading it — two products of
# make-believe in one folder, each seeing what its own list names and nothing else: their packs, their
# cabinets, with a file missing or empty held as an empty place. Neither product's: built and run with
# OrbitAmp's gates. Runs entirely in a temp folder.
# ----------------------------------------------------------------------------
juce_add_console_app(orbitamp_factory_test PRODUCT_NAME "OrbitAmpFactoryTest")
target_sources(orbitamp_factory_test PRIVATE tests/FactorySetTests.cpp)
target_include_directories(orbitamp_factory_test PRIVATE src)
target_compile_definitions(orbitamp_factory_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
                                                        ORBITAMP_SET_DIR="${ORBITAMP_SET_DIR}")   # the set's packs, copied out of the tree
target_link_libraries(orbitamp_factory_test PRIVATE
    OrbitAmpCabinetCatalog           # the shelf's table
    felitronics::nam                 # DeviceLibrary.h reads pack manifests through namz
    juce::juce_events
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ----------------------------------------------------------------------------
# THE GATES — tests/gates.txt is the one list, read here the way tools/run-gates.sh reads it. Every target
# it names must be one (a gate nobody builds is a gate that never runs), and `--target Family_Gates` builds
# them all, the faces' too, which no other build asks for. What the runner needs to find each — its name,
# the binary as THIS build placed it, its environment — is written out per configuration, to
# <build>/gates/<config>.txt: no runner guesses where a generator put an executable. A gate the list skips
# on this system is left out of both.
# ----------------------------------------------------------------------------
set(GATES_TXT "${CMAKE_CURRENT_SOURCE_DIR}/tests/gates.txt")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${GATES_TXT}")
if(APPLE)
    set(_gate_system macos)
elseif(WIN32)
    set(_gate_system windows)
else()
    set(_gate_system linux)
endif()
file(STRINGS "${GATES_TXT}" _gate_lines ENCODING UTF-8 REGEX "^[^#]")
add_custom_target(Family_Gates)
set(_gate_manifest "")
foreach(line IN LISTS _gate_lines)
    string(REGEX MATCHALL "[^ \t\r]+" fields "${line}")
    list(LENGTH fields n)
    if(n EQUAL 0)
        continue()
    endif()
    if(n LESS 2)
        message(FATAL_ERROR "tests/gates.txt: \"${line}\" names no target")
    endif()
    list(GET fields 0 gate)
    list(GET fields 1 target)
    set(rest "")
    if(n GREATER 2)
        list(SUBLIST fields 2 -1 rest)
    endif()
    set(env "")
    set(skipped OFF)
    foreach(field IN LISTS rest)
        if(field MATCHES "^skip=(.*)$")
            string(REPLACE "," ";" systems "${CMAKE_MATCH_1}")
            if(_gate_system IN_LIST systems)
                set(skipped ON)
            endif()
        elseif(field MATCHES "^[A-Za-z_][A-Za-z0-9_]*=")
            list(APPEND env "${field}")
        else()
            message(FATAL_ERROR "tests/gates.txt: \"${field}\" in gate ${gate} is neither VAR=value nor skip=")
        endif()
    endforeach()
    if(NOT TARGET ${target})
        message(FATAL_ERROR "tests/gates.txt: gate ${gate} names ${target}, which is no target of this tree")
    endif()
    if(skipped)
        message(STATUS "Gate ${gate} is skipped on ${_gate_system} (tests/gates.txt)")
        continue()
    endif()
    add_dependencies(Family_Gates ${target})
    list(JOIN env " " env)
    string(APPEND _gate_manifest "${gate}\t$<TARGET_FILE:${target}>\t${env}\n")
endforeach()
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/gates/$<CONFIG>.txt" CONTENT "${_gate_manifest}")
