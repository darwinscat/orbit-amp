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

project(OrbitAmp VERSION 0.7.0 LANGUAGES C CXX)

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
# for that repository in it (see below). v0.60.0 is the freshest, and the first with felitronics::dehum,
# the HumRemover the input's HUM stage is built on, so nothing older builds here. Between v0.31.0 and
# v0.53.0, nothing this plugin calls plays differently INSIDE a running stream —
# the probe loops render bit-identically on both — and what moved is at a stream's edges: a re-prepared
# capture no longer answers with the previous stream's tail, its warm-up is counted at the new rate, a
# trim switched off stays off through the prepare. The cabinet hands CabConvolver taps already at the
# host's rate, so the v0.34.0 rate factor and the v0.32.0 edge fix have nothing to act on here, and the
# v0.53.0 glides are in stages this plugin does not link. Past v0.53.0 two things reach this graph, and
# neither plays here: WaveShaper gains four shapes while Asym — the only one the power amp's TubeStage
# sets — keeps its expressions, and eq's Tilt at slope 6 turns first-order, a band no block here builds.
set(ORBITAMP_FCORE_TAG "v0.60.0" CACHE STRING "felitronics-core release tag (fetched when no local checkout)")

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
# second clone of trees this machine already has, and fall back to the pins when they are gone.
set(ORBITAMP_GUITAR_TAG "v0.1.0" CACHE STRING "felitronics-guitar-core release tag (fetched when no local checkout)")
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
# The plugin target.
#
# 4-char codes — LOCK these before the first release (changing them afterwards makes hosts
# treat it as a different plugin):
#   PLUGIN_MANUFACTURER_CODE = Dcat  (Darwin's Cat — >=1 uppercase, AU rule; same as OrbitCab)
#   PLUGIN_CODE              = Oamp  (OrbitAmp — unique per plugin; OrbitCab holds `Orbt`)
#
# CLAP rides via clap-juce-extensions below — one extra format, no source changes.
# ----------------------------------------------------------------------------
# OrbitAmp's icon. macOS 26 sets any icon that is not its own rounded square on a grey plate — the
# family's disc came out as a small disc on a light square — so the Mac wears the orbit on the cockpit's
# rounded square (824 of the 1024 canvas, margins and all), the violet round its edge. Windows and Linux
# keep the disc.
if(APPLE)
    set(ORBITAMP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/OrbitAmp-macOS-1024.png")
else()
    set(ORBITAMP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/OrbitAmp-1024.png")
endif()

juce_add_plugin(OrbitAmp
    PRODUCT_NAME                "OrbitAmp"
    COMPANY_NAME                "Darwin's Cat"
    BUNDLE_ID                   "com.darwinscat.orbitamp"
    PLUGIN_MANUFACTURER_CODE    Dcat
    PLUGIN_CODE                 Oamp
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
    MICROPHONE_PERMISSION_TEXT  "OrbitAmp needs audio input to process your signal."
    # Bundle / executable icon — the orbit; see ORBITAMP_ICON.
    # One 1024-square PNG is enough: juceaide builds the macOS .icns for every bundle and the
    # Windows .ico from it. No ICON_SMALL on purpose — see OrbitCab: juceaide reads the pair
    # small-then-big while JUCEUtils appends big-then-small, and passing both can swap the slots.
    ICON_BIG                    "${ORBITAMP_ICON}"
    COPY_PLUGIN_AFTER_BUILD     ${ORBITAMP_COPY_PLUGIN}
)

target_sources(OrbitAmp PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/OrbitAmpSet.cpp
    src/device/FactorySet.cpp
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
    src/ui/library/IrTreeView.cpp
)

# Wrap the same JUCE plugin as a CLAP (.clap) — one extra format, no source changes.
clap_juce_extensions_plugin(TARGET OrbitAmp
    CLAP_ID       "com.darwinscat.orbitamp"
    CLAP_FEATURES audio-effect "guitar" "amplifier")

# The CLAP wrapper ships its own default clap.icns (the generic CLAP logo) and JUCE's ICON_BIG
# does not reach it. Overwrite it in place with the AppIcon.icns juceaide already generated for
# the JUCE formats, keeping the wrapper's Info.plist CFBundleIconFile=clap.icns reference valid.
# macOS only — the CLAP on Windows/Linux is a plain shared library, no icon resource.
if(APPLE)
    add_custom_command(TARGET OrbitAmp_CLAP POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy
                "$<TARGET_PROPERTY:OrbitAmp,JUCE_ICON_FILE>"
                "$<TARGET_BUNDLE_DIR:OrbitAmp_CLAP>/Contents/Resources/clap.icns"
        VERBATIM)
endif()

# ----------------------------------------------------------------------------
# THE SET: every pack and every cabinet a Darwin's Cat amp carries in its binary, ONE copy of each in
# the tree — resources/set/Devices for the packs, under their own names, and resources/set/IRs for the
# cabinets (resources/set/IRs.md says what they are). Each product says which of them it carries, and
# no product keeps a folder of its own.
# ----------------------------------------------------------------------------
set(ORBITAMP_SET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources/set")

# A PACK IS KEPT AS A FOLDER and carried as a zip. In the tree each pack is a folder,
# resources/set/Devices/<name>.orbitrig — its manifest, its models and its picture side by side, so a
# change to a pack is a change to its rig.json that git can show, not a new blob. Every build zips each
# folder into <name>.orbitrig.zip in the build tree — the file the products carry and lay out, under
# the name it always had — and that is what the set data below takes in. .gitattributes keeps git's
# hands off the folders' line endings: what is zipped is the files byte for byte, on every system.
#
# The zip is made by a tool of our own (tools/PackZip.cpp, JUCE's zip writer), not by `cmake -E tar`:
# CMake's zip writer puts into every entry the access and change times the file has on disk, its
# owner, its permissions and a date in the machine's own time zone, whatever --mtime says — so the
# same folder made other bytes on another checkout, another day or another machine, and a product
# built later would lay its copy of an unchanged pack over the one another product had laid out. Ours
# puts in the files' names and bytes and nothing else, in the order of their names, so the same folder
# makes the same zip on every build and every system.
#
# Each folder's files are listed by a glob, CONFIGURE_DEPENDS, so a file added to a pack, or a pack
# added to Devices, is zipped from the next build on; the list is also written out, and rewritten only
# when it changes, so a file taken out of a pack makes the zip again too (as JUCE's binary data does).
# A name with a leading dot is the Finder's (.DS_Store), not the pack's. Every zip is made by one
# target, OrbitAmpSetPacks, which each product's set data waits for: a rule that several targets ran
# would race itself in a parallel build.
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
foreach(folder IN LISTS ORBITAMP_SET_FOLDERS)
    if(NOT IS_DIRECTORY "${folder}")
        continue()
    endif()
    get_filename_component(pack "${folder}" NAME)
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

# Factory devices: OrbitAmp's own set, INSIDE THE BINARY like the cockpit's — every format, every
# system — and laid out in the family's Factory folder when it starts (src/device/FactorySet.h says
# why a bundle will not do). OrbitAmp is the player of the whole library: it carries EVERY pack in
# resources/set/Devices, each under its own name — every folder there, zipped. A pack added there is
# carried from the next build on, with nobody to remember to list it.
list(LENGTH ORBITAMP_SET_PACKS ORBITAMP_SET_COUNT)
message(STATUS "OrbitAmp Devices: ${ORBITAMP_SET_COUNT} in the binary")
juce_add_binary_data(OrbitAmpSetData
    NAMESPACE   OrbitAmpSetData
    HEADER_NAME OrbitAmpSetData.h
    SOURCES     ${ORBITAMP_SET_PACKS})
add_dependencies(OrbitAmpSetData OrbitAmpSetPacks)

# ----------------------------------------------------------------------------
# Embedded assets. The display FACE is not among them: appkit carries Michroma itself, whole, as
# code, so a product that embedded it too was putting a second copy of the same font in every
# binary and parsing both at startup. The mark still comes from appkit's assets/ — referenced
# there, NOT copied here, so there is one copy of the file in the ecosystem.
# felitronics_appkit_SOURCE_DIR is set by FetchContent above and points at whichever source won
# (local checkout or fetched release).
# ----------------------------------------------------------------------------
juce_add_binary_data(OrbitAmpData
    SOURCES
        "${felitronics_appkit_SOURCE_DIR}/assets/catlogo.svg"

        # The trademark notice, as text rather than as a string literal: it is a legal sentence that
        # the README carries word for word, and a file can be diffed against that one.
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/notice.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/disclaimer.txt"   # ...and its long form, with the list

        # The cabinet IRs — orbitcab's, referenced there like everything shared: one copy in the
        # ecosystem.
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/ir/01-cookie-monster.wav"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/ir/02-darth-genocider.wav"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/ir/03-kitten-slayer.wav"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/ir/04-kaiju-tamer.wav"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/ir/05-iceburn-suicide.wav"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/ir/06-vertical-lip-stabber.wav"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/ir/07-manslaughter-joe.wav"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/ir/08-big-bubba.wav"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/ir/09-devils-cunnilingus.wav"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/ir/10-october-32th.wav"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/ir/11-wumbo.wav"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/ir/12-world-collider.wav"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/ir/13-cannibal-choir.wav"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/ir/14-cathode-ray-fleshburn.wav"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/ir/15-impaler-jim.wav"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/ir/16-nacho-guacamole.wav"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/ir/17-pickle-punisher.wav"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/ir/18-wasabi-warrior.wav"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/ir/19-pesto-paladin.wav"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/ir/20-don-spinacio.wav"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/ir/21-kill-dill.wav"

        # ...and the Orange PPC212V's ten, the cabinets CatpitAmp plays, referenced in the set's
        # folder rather than copied: one copy in the tree. resources/set/IRs.md says what they are and
        # how they were shot.
        "${ORBITAMP_SET_DIR}/IRs/02-dark-matter.wav"
        "${ORBITAMP_SET_DIR}/IRs/03-ion-cavity.wav"
        "${ORBITAMP_SET_DIR}/IRs/04-event-horizon.wav"
        "${ORBITAMP_SET_DIR}/IRs/06-coronal-flare.wav"
        "${ORBITAMP_SET_DIR}/IRs/07-brown-dwarf.wav"
        "${ORBITAMP_SET_DIR}/IRs/08-accretion-gap.wav"
        "${ORBITAMP_SET_DIR}/IRs/09-dust-cloud.wav"
        "${ORBITAMP_SET_DIR}/IRs/10-blue-shift.wav"
        "${ORBITAMP_SET_DIR}/IRs/11-red-giant.wav"
        "${ORBITAMP_SET_DIR}/IRs/12-pulsar-beam.wav"
)

# THE PRODUCT'S version, not the plugin wrapper's. `JucePlugin_VersionString` exists only inside a
# target juce_add_plugin built, and the headless gates compile the very same UI sources without it —
# so a face that read it built as a plugin and failed as a test. This one is the project's, defined
# for everything that compiles a face.
target_compile_definitions(OrbitAmp PUBLIC
    ORBITAMP_VERSION="${PROJECT_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0   # JUCE 8 has no splash requirement on any tier, incl. AGPLv3
    $<$<PLATFORM_ID:Windows>:JUCE_ASIO=1>   # the standalone offers ASIO beside WASAPI; JUCE carries the SDK
)

target_include_directories(OrbitAmp PUBLIC "${ORBITAMP_GENERATED_DIR}")   # OrbitAmpVersion.h
add_dependencies(OrbitAmp OrbitAmpVersionGen)                            # ...rebaked before every build

target_link_libraries(OrbitAmp PRIVATE
    OrbitAmpData                     # the mark, the notice, the demo loops — BinaryData::*
    OrbitAmpSetData                  # the factory packs, laid out at start — OrbitAmpSetData::*
    felitronics::analysis            # spectrum taps + panes behind the EQ curves
    felitronics::appkit              # shared app-side kit (header-only; JUCE comes from us)
    felitronics::appkit_webp         # juce::Image <- WebP, for the picture a pack ships
    felitronics::core                # core::BypassWire reads the resampler's delay geometry
    felitronics::dehum               # the hum off the input, ahead of the trim
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
    juce::juce_data_structures       # ValueTree behind the workspace / presets
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# ----------------------------------------------------------------------------
# CatpitAmp — the cockpit: a second face over the same processor (src/catpit), one processor and two
# faces, each target compiling exactly one `makeEditor`. Standalone only, for now. Its renders are in
# resources/catpit and are built into the binary under a namespace of their own, beside OrbitAmp's data.
# ----------------------------------------------------------------------------
juce_add_binary_data(CatpitAmpData
    NAMESPACE   CatpitData
    HEADER_NAME CatpitData.h
    SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/catpit/cockpit.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/catpit/midi.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/catpit/knob-drive.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/catpit/knob-tone.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/catpit/throttle-grip.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/catpit/guard-closed.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/catpit/guard-open.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/catpit/guard-on.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/catpit/fader-cap.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/catpit/sky-clean.jpg"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/catpit/sky-edge.jpg"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/catpit/sky-crunch.jpg"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/catpit/sky-hi-gain.jpg"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/catpit/mark.svg")

# RELEASED APART FROM ORBITAMP, from the same tree: a version of its own, and tags of its own —
# `catpit-vX.Y.Z` beside OrbitAmp's `vX.Y.Z` — so neither waits for the other and a fix in the engine
# reaches each at its own next release. This is the number the cockpit shows, stamps its bundle with
# and compares against the releases that wear its prefix (src/Product.h); it moves when the COCKPIT
# is released. .github/workflows/release-catpit.yml refuses a tag that disagrees with it.
set(CATPITAMP_VERSION 0.2.0)

# The cockpit's icon. macOS 26 sets any icon that is not its own rounded square on a grey plate —
# the family's disc came out as a small disc on a light square — so the Mac wears the mark on the
# rounded square the system expects (824 of the 1024 canvas, margins and all), and is shown as drawn.
# Windows and Linux keep the disc.
if(APPLE)
    set(CATPITAMP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/CatpitAmp-macOS-1024.png")
else()
    set(CATPITAMP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/CatpitAmp-1024.png")
endif()

juce_add_plugin(CatpitAmp
    VERSION                     "${CATPITAMP_VERSION}"
    PRODUCT_NAME                "CatpitAmp"
    COMPANY_NAME                "Darwin's Cat"
    BUNDLE_ID                   "com.darwinscat.catpitamp"
    PLUGIN_MANUFACTURER_CODE    Dcat
    PLUGIN_CODE                 Ctpt
    FORMATS                     VST3 AU Standalone
    VST3_CATEGORIES             "Fx" "Distortion"
    AU_MAIN_TYPE                "kAudioUnitType_Effect"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT  "CatpitAmp needs audio input to process your signal."
    # Bundle / executable icon — the cockpit's mark; see CATPITAMP_ICON. No ICON_SMALL, as OrbitAmp.
    ICON_BIG                    "${CATPITAMP_ICON}"
    COPY_PLUGIN_AFTER_BUILD     ${ORBITAMP_COPY_PLUGIN})

# ...and as a CLAP, wrapped the way OrbitAmp's is, wearing the cockpit's icon in place of the
# wrapper's own.
clap_juce_extensions_plugin(TARGET CatpitAmp
    CLAP_ID       "com.darwinscat.catpitamp"
    CLAP_FEATURES audio-effect "guitar" "amplifier")
if(APPLE)
    add_custom_command(TARGET CatpitAmp_CLAP POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy
                "$<TARGET_PROPERTY:CatpitAmp,JUCE_ICON_FILE>"
                "$<TARGET_BUNDLE_DIR:CatpitAmp_CLAP>/Contents/Resources/clap.icns"
        VERBATIM)
endif()

# What the cockpit is made of — said once, for the plug-in and for the two programs further down that
# are the cockpit compiled again under a main() of their own.
set(CATPITAMP_SOURCES
    src/PluginProcessor.cpp
    src/Parameters.cpp
    src/ui/Theme.cpp
    src/core/ProcessMemory.cpp
    src/device/FactorySet.cpp
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
    src/face/Standalone.cpp
    src/catpit/CatpitArt.cpp
    src/catpit/CatpitControls.cpp
    src/catpit/CatpitLineup.cpp
    src/catpit/CatpitSpace.cpp
    src/catpit/CatpitGlSky.cpp
    src/catpit/CatpitEditor.cpp)
set(CATPITAMP_LIBRARIES
    OrbitAmpData
    CatpitAmpData
    felitronics::analysis
    felitronics::appkit
    felitronics::appkit_webp
    felitronics::core
    felitronics::dehum
    felitronics::dynamics
    felitronics::eq
    felitronics::lineareq
    felitronics::convolution
    felitronics::measurement
    felitronics::nam
    felitronics::rigplayer
    felitronics::limiter
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_data_structures
    juce::juce_opengl                # the sky, on the GPU
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

target_sources(CatpitAmp PRIVATE ${CATPITAMP_SOURCES})

target_compile_definitions(CatpitAmp PUBLIC
    ORBITAMP_VERSION="${CATPITAMP_VERSION}"
    ORBITAMP_PRODUCT_NAME="CatpitAmp"
    ORBITAMP_RELEASE_TAG_PREFIX="catpit-"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    $<$<PLATFORM_ID:Windows>:JUCE_ASIO=1>)   # the standalone offers ASIO beside WASAPI; JUCE carries the SDK
target_include_directories(CatpitAmp PUBLIC "${ORBITAMP_GENERATED_DIR}")
add_dependencies(CatpitAmp OrbitAmpVersionGen)

target_link_libraries(CatpitAmp PRIVATE ${CATPITAMP_LIBRARIES})

# Every format the cockpit is built as on this platform — AU on the Mac alone.
set(CATPITAMP_FORMATS "")
foreach(fmt IN ITEMS VST3 AU Standalone CLAP)
    if(TARGET CatpitAmp_${fmt})
        list(APPEND CATPITAMP_FORMATS CatpitAmp_${fmt})
    endif()
endforeach()

# THE COCKPIT'S OWN SET IS INSIDE ITS BINARY — every format's, on every system — and the amp lays it
# out as files when it first starts (src/channels/Set.h says why). Out of the tree's set
# (resources/set), the cockpit carries what its four channels play and nothing else: the X88-IR's four
# packs — a channel finds its own by the capture it holds, not by the file's name — and the ten
# cabinets CAB steps through, in the order of their names. Without a set, the cockpit would play only
# what the player puts in its own Devices and IRs folders.
set(CATPITAMP_SET
    "${ORBITAMP_SET_ZIPPED}/X88-IR Normal.orbitrig.zip"
    "${ORBITAMP_SET_ZIPPED}/X88-IR Altered.orbitrig.zip"
    "${ORBITAMP_SET_ZIPPED}/X88-IR Crunch.orbitrig.zip"
    "${ORBITAMP_SET_ZIPPED}/X88-IR Overdrive.orbitrig.zip"
    "${ORBITAMP_SET_DIR}/IRs/02-dark-matter.wav"
    "${ORBITAMP_SET_DIR}/IRs/03-ion-cavity.wav"
    "${ORBITAMP_SET_DIR}/IRs/04-event-horizon.wav"
    "${ORBITAMP_SET_DIR}/IRs/06-coronal-flare.wav"
    "${ORBITAMP_SET_DIR}/IRs/07-brown-dwarf.wav"
    "${ORBITAMP_SET_DIR}/IRs/08-accretion-gap.wav"
    "${ORBITAMP_SET_DIR}/IRs/09-dust-cloud.wav"
    "${ORBITAMP_SET_DIR}/IRs/10-blue-shift.wav"
    "${ORBITAMP_SET_DIR}/IRs/11-red-giant.wav"
    "${ORBITAMP_SET_DIR}/IRs/12-pulsar-beam.wav")

# What reads the set says whether there is one: with none, there is no data to link and nothing to ask.
# Every product's set data goes by the one name, ProductSetData, which channels/Set.cpp reads: a target
# links exactly one product's.
add_library(CatpitAmpSet INTERFACE)
if(CATPITAMP_SET)
    juce_add_binary_data(CatpitAmpSetData
        NAMESPACE   ProductSetData
        HEADER_NAME ProductSetData.h
        SOURCES     ${CATPITAMP_SET})
    add_dependencies(CatpitAmpSetData OrbitAmpSetPacks)   # the packs are zipped by the build (THE SET)
    set_target_properties(CatpitAmpSetData PROPERTIES EXCLUDE_FROM_ALL TRUE)
    target_link_libraries(CatpitAmpSet INTERFACE CatpitAmpSetData)
    target_compile_definitions(CatpitAmpSet INTERFACE PRODUCT_SET_EMBEDDED=1)
else()
    target_compile_definitions(CatpitAmpSet INTERFACE PRODUCT_SET_EMBEDDED=0)
endif()
target_link_libraries(CatpitAmp PRIVATE CatpitAmpSet)

# Not part of the default build. The two products are released apart, and OrbitAmp's own build — a
# contributor's, a pull request's, a release's — must never fail on the cockpit. Whoever wants the
# cockpit asks for it by name: one format (`cmake --build build --target CatpitAmp_Standalone`), or
# all of them — `--target CatpitAmp_All`, which the CLAP joins here, since its wrapper does not add
# itself.
set_target_properties(CatpitAmp ${CATPITAMP_FORMATS} CatpitAmpData PROPERTIES EXCLUDE_FROM_ALL TRUE)
add_dependencies(CatpitAmp_All CatpitAmp_CLAP)

# The cockpit's memory probe — not a gate, a measuring instrument: what the cockpit holds with its
# window built, and with it SHUT, which is the figure that matters in a host and the one the
# standalone cannot give. It is the cockpit itself, compiled again under a main() of its own, and
# like the cockpit it is built only when asked for by name.
function(catpitamp_console target main product)
    juce_add_console_app(${target} PRODUCT_NAME "${product}")
    target_sources(${target} PRIVATE ${main} ${CATPITAMP_SOURCES})
    target_include_directories(${target} PRIVATE src "${ORBITAMP_GENERATED_DIR}")
    add_dependencies(${target} OrbitAmpVersionGen)
    target_compile_definitions(${target} PRIVATE
        ORBITAMP_VERSION="${CATPITAMP_VERSION}"
        ORBITAMP_PRODUCT_NAME="CatpitAmp"
        ORBITAMP_RELEASE_TAG_PREFIX="catpit-"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(${target} PRIVATE CatpitAmpSet ${CATPITAMP_LIBRARIES})
    set_target_properties(${target} PROPERTIES EXCLUDE_FROM_ALL TRUE)
endfunction()
catpitamp_console(catpitamp_memory_probe tests/MemoryProbe.cpp "CatpitAmpMemoryProbe")

# Gate for the cockpit's channels with no window at all: that the processor is a four-channel amp by
# itself, and CHANNEL — the parameter a host reaches it by — moves it. The cockpit under a main() of
# its own again, and the cockpit's, so built only by name.
catpitamp_console(catpitamp_channel_test tests/CatpitChannelTests.cpp "CatpitAmpChannelTest")

# Gate for the cockpit's set as it leaves the binary — laid out whole, once, and nothing else in the
# folder touched. Runs entirely in a folder of its own. The cockpit's, so built only by name — and under
# the cockpit's name, which is the name of its list in the folder.
juce_add_console_app(catpitamp_set_test PRODUCT_NAME "CatpitAmpSetTest")
target_sources(catpitamp_set_test PRIVATE
    tests/CatpitSetTests.cpp
    src/channels/Set.cpp
    src/device/FactorySet.cpp)
target_include_directories(catpitamp_set_test PRIVATE src)
target_compile_definitions(catpitamp_set_test PRIVATE ORBITAMP_PRODUCT_NAME="CatpitAmp" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(catpitamp_set_test PRIVATE
    CatpitAmpSet
    juce::juce_core
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
set_target_properties(catpitamp_set_test PROPERTIES EXCLUDE_FROM_ALL TRUE)

# ----------------------------------------------------------------------------
# RedPawAmp — the windscreen: the drive across Mars (src/redpaw/view), a view that takes values and
# no processor, so it stands on its own. The sources are listed here for the third face to take up;
# the demo is a bare window with the view in it, for looking at it and measuring it. Built only when
# asked for by name. No audio.
# ----------------------------------------------------------------------------
set(REDPAW_VIEW_SOURCES
    src/redpaw/view/Mars1993.cpp
    src/redpaw/view/PlainsWorld.cpp
    src/redpaw/view/PlainsGl.cpp
    src/redpaw/view/Windscreen.cpp)

juce_add_gui_app(redpaw_view_demo PRODUCT_NAME "RedPawViewDemo")
target_sources(redpaw_view_demo PRIVATE src/redpaw/view/WindscreenDemo.cpp ${REDPAW_VIEW_SOURCES})
target_include_directories(redpaw_view_demo PRIVATE src)
target_compile_definitions(redpaw_view_demo PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(redpaw_view_demo PRIVATE
    juce::juce_gui_extra
    juce::juce_opengl
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
set_target_properties(redpaw_view_demo PROPERTIES EXCLUDE_FROM_ALL TRUE)

# ----------------------------------------------------------------------------
# FrostPawAmp — the ice: the same windscreen looking out on an ice moon (src/frostpaw/view), the drive
# handed to it as an IceDrive. The sources are listed here for the product to take up, with the
# windscreen's own; the demo is the same bare window over the ice. Built only when asked for by name.
# ----------------------------------------------------------------------------
set(FROSTPAW_VIEW_SOURCES
    src/frostpaw/view/IceWorld.cpp
    src/frostpaw/view/IceGl.cpp)

juce_add_gui_app(frostpaw_view_demo PRODUCT_NAME "FrostPawViewDemo")
target_sources(frostpaw_view_demo PRIVATE src/redpaw/view/WindscreenDemo.cpp ${REDPAW_VIEW_SOURCES} ${FROSTPAW_VIEW_SOURCES})
target_include_directories(frostpaw_view_demo PRIVATE src)
target_compile_definitions(frostpaw_view_demo PRIVATE ORBITAMP_VIEW_DEMO_ICE=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(frostpaw_view_demo PRIVATE
    juce::juce_gui_extra
    juce::juce_opengl
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
set_target_properties(frostpaw_view_demo PROPERTIES EXCLUDE_FROM_ALL TRUE)

# ----------------------------------------------------------------------------
# Titan — the methane sea: the same windscreen as a helicopter's canopy over Titan (src/amberpaw/view), the
# drive handed to it as a TitanDrive. The sources are listed here for the product to take up, with the
# windscreen's own; the demo is the bare window over the sea, in the canopy's shape. Built only when asked
# for by name.
# ----------------------------------------------------------------------------
set(AMBERPAW_VIEW_SOURCES
    src/amberpaw/view/TitanWorld.cpp
    src/amberpaw/view/TitanGl.cpp)

juce_add_gui_app(amberpaw_view_demo PRODUCT_NAME "AmberPawViewDemo")
target_sources(amberpaw_view_demo PRIVATE src/redpaw/view/WindscreenDemo.cpp ${REDPAW_VIEW_SOURCES} ${AMBERPAW_VIEW_SOURCES})
target_include_directories(amberpaw_view_demo PRIVATE src)
target_compile_definitions(amberpaw_view_demo PRIVATE ORBITAMP_VIEW_DEMO_TITAN=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(amberpaw_view_demo PRIVATE
    juce::juce_gui_extra
    juce::juce_opengl
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
set_target_properties(amberpaw_view_demo PROPERTIES EXCLUDE_FROM_ALL TRUE)

# The sea's CPU half against the prototype it was ported from: the start, five scripted flights, the
# gears, Saturn, the wipers' panes. JUCE-free, links only the world.
add_executable(titan_world_test tests/TitanWorldTests.cpp src/amberpaw/view/TitanWorld.cpp)
target_include_directories(titan_world_test PRIVATE src)
target_compile_features(titan_world_test PRIVATE cxx_std_20)

# ----------------------------------------------------------------------------
# RedPawAmp — the rover: a third face over the same processor (src/redpaw), the dashboard of a Mars rover
# with the drive across Mars in its windscreen. Four channels — the Friedman IR-X's two, each with its
# Boost off and on — picked as two gears and TURBO. Built like the cockpit: the family's console (src/face)
# over the family's channels (src/channels), its own renders in resources/redpaw, its own set, released
# apart under tags of its own (`redpaw-vX.Y.Z`) — no release workflow yet. Built only when asked for by
# name.
# ----------------------------------------------------------------------------

# THE ROVER'S FACE IS WORN BY MORE THAN ONE PRODUCT (src/redpaw/RedPawSkin.h): what every rover is made
# of is said once here — the renders by the file names the face asks for, the sources, the libraries —
# and each product adds its own skin's file and its own renders.
set(ROVER_RENDERS
    dashboard.png midi.txt mark.svg knob-drive.png knob-tone.png lever.png slider-cap.png fader-cap.png
    toggle-up.png toggle-down.png key-up.png key-down.png turbo-up.png turbo-down.png)

set(REDPAWAMP_RENDERS ${ROVER_RENDERS} turbo-lid.png)     # ...and TURBO's hatch shut, its own
list(TRANSFORM REDPAWAMP_RENDERS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/resources/redpaw/")
# ...and the STILL it ships, one frame of its world, where there is one: never another rover's.
file(GLOB REDPAWAMP_STILL CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/redpaw/still.jpg")
list(APPEND REDPAWAMP_RENDERS ${REDPAWAMP_STILL})
juce_add_binary_data(RedPawAmpData
    NAMESPACE   RedPawData
    HEADER_NAME RedPawData.h
    SOURCES     ${REDPAWAMP_RENDERS})

# The rover's own version, moved when the ROVER is released (src/Product.h).
set(REDPAWAMP_VERSION 0.1.0)

# Its icon: its vehicle seen from the front, drawn as the cockpit's mark is — in the family's lilac with
# the orange dot — on the cockpit's rounded square with the violet round its edge. On the Mac the
# square macOS 26 expects (824 of the 1024 canvas, margins and all); elsewhere the same on the whole canvas.
if(APPLE)
    set(REDPAWAMP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/RedPawAmp-macOS-1024.png")
else()
    set(REDPAWAMP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/RedPawAmp-1024.png")
endif()

juce_add_plugin(RedPawAmp
    VERSION                     "${REDPAWAMP_VERSION}"
    PRODUCT_NAME                "RedPawAmp"
    COMPANY_NAME                "Darwin's Cat"
    BUNDLE_ID                   "com.darwinscat.redpawamp"
    PLUGIN_MANUFACTURER_CODE    Dcat
    PLUGIN_CODE                 Rdpw
    FORMATS                     VST3 AU Standalone
    VST3_CATEGORIES             "Fx" "Distortion"
    AU_MAIN_TYPE                "kAudioUnitType_Effect"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT  "RedPawAmp needs audio input to process your signal."
    # Bundle / executable icon — its vehicle on the square; see REDPAWAMP_ICON.
    ICON_BIG                    "${REDPAWAMP_ICON}"
    COPY_PLUGIN_AFTER_BUILD     ${ORBITAMP_COPY_PLUGIN})

clap_juce_extensions_plugin(TARGET RedPawAmp
    CLAP_ID       "com.darwinscat.redpawamp"
    CLAP_FEATURES audio-effect "guitar" "amplifier")
if(APPLE)
    add_custom_command(TARGET RedPawAmp_CLAP POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy
                "$<TARGET_PROPERTY:RedPawAmp,JUCE_ICON_FILE>"
                "$<TARGET_BUNDLE_DIR:RedPawAmp_CLAP>/Contents/Resources/clap.icns"
        VERBATIM)
endif()

# What a rover is made of — for the plug-in and for its gates, which are the rover compiled again
# under a main() of their own — and what RedPawAmp adds: its skin.
set(ROVER_SOURCES
    src/PluginProcessor.cpp
    src/Parameters.cpp
    src/ui/Theme.cpp
    src/core/ProcessMemory.cpp
    src/device/FactorySet.cpp
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
    src/face/Standalone.cpp
    src/redpaw/RedPawArt.cpp
    src/redpaw/RedPawControls.cpp
    src/redpaw/RedPawLineup.cpp
    src/redpaw/RedPawEditor.cpp
    ${REDPAW_VIEW_SOURCES})
set(REDPAWAMP_SOURCES ${ROVER_SOURCES} src/redpaw/RedPawSkin.cpp)
set(ROVER_LIBRARIES
    OrbitAmpData
    felitronics::analysis
    felitronics::appkit
    felitronics::appkit_webp
    felitronics::core
    felitronics::dehum
    felitronics::dynamics
    felitronics::eq
    felitronics::lineareq
    felitronics::convolution
    felitronics::measurement
    felitronics::nam
    felitronics::rigplayer
    felitronics::limiter
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_data_structures
    juce::juce_opengl                # the windscreen, on the GPU
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
set(REDPAWAMP_LIBRARIES RedPawAmpData ${ROVER_LIBRARIES})

target_sources(RedPawAmp PRIVATE ${REDPAWAMP_SOURCES})
target_compile_definitions(RedPawAmp PUBLIC
    ORBITAMP_VERSION="${REDPAWAMP_VERSION}"
    ORBITAMP_PRODUCT_NAME="RedPawAmp"
    ORBITAMP_RELEASE_TAG_PREFIX="redpaw-"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    $<$<PLATFORM_ID:Windows>:JUCE_ASIO=1>)
target_include_directories(RedPawAmp PUBLIC "${ORBITAMP_GENERATED_DIR}")
add_dependencies(RedPawAmp OrbitAmpVersionGen)
target_link_libraries(RedPawAmp PRIVATE ${REDPAWAMP_LIBRARIES})

set(REDPAWAMP_FORMATS "")
foreach(fmt IN ITEMS VST3 AU Standalone CLAP)
    if(TARGET RedPawAmp_${fmt})
        list(APPEND REDPAWAMP_FORMATS RedPawAmp_${fmt})
    endif()
endforeach()

# THE ROVER'S OWN SET IS INSIDE ITS BINARY, laid out as files when it first starts (src/channels/Set.h):
# out of the tree's set, the packs its channels play — the Friedman IR-X's two, each found by the capture
# it holds — and, until the rovers have cabinets of their own, the ten the cockpit carries.
# Every rover lists its own packs; the cabinets are every rover's.
set(ROVER_CABINETS
    "${ORBITAMP_SET_DIR}/IRs/02-dark-matter.wav"
    "${ORBITAMP_SET_DIR}/IRs/03-ion-cavity.wav"
    "${ORBITAMP_SET_DIR}/IRs/04-event-horizon.wav"
    "${ORBITAMP_SET_DIR}/IRs/06-coronal-flare.wav"
    "${ORBITAMP_SET_DIR}/IRs/07-brown-dwarf.wav"
    "${ORBITAMP_SET_DIR}/IRs/08-accretion-gap.wav"
    "${ORBITAMP_SET_DIR}/IRs/09-dust-cloud.wav"
    "${ORBITAMP_SET_DIR}/IRs/10-blue-shift.wav"
    "${ORBITAMP_SET_DIR}/IRs/11-red-giant.wav"
    "${ORBITAMP_SET_DIR}/IRs/12-pulsar-beam.wav")
set(REDPAWAMP_SET
    "${ORBITAMP_SET_ZIPPED}/IR-X Ch1 PowerAmp.orbitrig.zip"
    "${ORBITAMP_SET_ZIPPED}/IR-X Ch2 PowerAmp.orbitrig.zip"
    ${ROVER_CABINETS})

add_library(RedPawAmpSet INTERFACE)
juce_add_binary_data(RedPawAmpSetData
    NAMESPACE   ProductSetData
    HEADER_NAME ProductSetData.h
    SOURCES     ${REDPAWAMP_SET})
add_dependencies(RedPawAmpSetData OrbitAmpSetPacks)   # the packs are zipped by the build (THE SET)
set_target_properties(RedPawAmpSetData PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_link_libraries(RedPawAmpSet INTERFACE RedPawAmpSetData)
target_compile_definitions(RedPawAmpSet INTERFACE PRODUCT_SET_EMBEDDED=1)
target_link_libraries(RedPawAmp PRIVATE RedPawAmpSet)

# Not part of the default build, like the cockpit: `--target RedPawAmp_All` builds every format.
set_target_properties(RedPawAmp ${REDPAWAMP_FORMATS} RedPawAmpData PROPERTIES EXCLUDE_FROM_ALL TRUE)
add_dependencies(RedPawAmp_All RedPawAmp_CLAP)

# A ROVER'S GATES, the same for every rover: `amp` says whose — RedPawAmp, FrostPawAmp, AmberPawAmp — and the gate is
# that product compiled again under a main() of its own, with its skin, its set and its name; `channels`
# is how many channels the gate holds it to (ROVER_CHANNELS), said here apart from the product's lineup.
function(rover_console target main product amp tagPrefix channels)
    string(TOUPPER "${amp}" AMP)
    juce_add_console_app(${target} PRODUCT_NAME "${product}")
    target_sources(${target} PRIVATE ${main} ${${AMP}_SOURCES})
    target_include_directories(${target} PRIVATE src "${ORBITAMP_GENERATED_DIR}")
    add_dependencies(${target} OrbitAmpVersionGen)
    target_compile_definitions(${target} PRIVATE
        ORBITAMP_VERSION="${${AMP}_VERSION}"
        ORBITAMP_PRODUCT_NAME="${amp}"
        ORBITAMP_RELEASE_TAG_PREFIX="${tagPrefix}"
        ROVER_CHANNELS=${channels}
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(${target} PRIVATE ${amp}Set ${${AMP}_LIBRARIES})
    set_target_properties(${target} PROPERTIES EXCLUDE_FROM_ALL TRUE)
endfunction()

# ...and its set as it leaves the binary, the set's own reading of it and nothing more.
function(rover_set_test target product amp channels)
    juce_add_console_app(${target} PRODUCT_NAME "${product}")
    target_sources(${target} PRIVATE
        tests/RedPawSetTests.cpp
        src/channels/Set.cpp
        src/device/FactorySet.cpp)
    target_include_directories(${target} PRIVATE src)
    target_compile_definitions(${target} PRIVATE ORBITAMP_PRODUCT_NAME="${amp}" ROVER_CHANNELS=${channels} JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(${target} PRIVATE
        ${amp}Set
        felitronics::nam                 # DeviceLibrary.h reads pack manifests through namz
        juce::juce_core
        juce::juce_events
        juce::juce_gui_basics
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    set_target_properties(${target} PROPERTIES EXCLUDE_FROM_ALL TRUE)
endfunction()

# Gate for the rover's channels with no window at all: four channels on the IR-X's two, the Boost each
# holds, moved by CHANNEL the way a host moves it, each with its own memory and what it opens on.
rover_console(redpawamp_channel_test tests/RedPawChannelTests.cpp "RedPawAmpChannelTest" RedPawAmp "redpaw-" 4)

# Gate for the rover's set as it leaves the binary — laid out whole, once, under the rover's own list,
# the two packs its four channels play. Runs entirely in a folder of its own.
rover_set_test(redpawamp_set_test "RedPawAmpSetTest" RedPawAmp 4)

# ----------------------------------------------------------------------------
# FrostPawAmp — the ice moon's rover: a fourth face over the same processor, RedPawAmp's dashboard worn
# over a picture of its own (src/frostpaw/FrostPawSkin.cpp) — frosted stainless steel, amber lamps,
# ice-blue rings. The same controls, TURBO and PARK: the rover's, shared, not copied — over channels of
# its own, six on the V4's six packs in three gears, and the rovers' cabinets. Released apart under tags
# of its own (`frostpaw-vX.Y.Z`) — no release workflow yet. Built only when asked for by name.
# ----------------------------------------------------------------------------

# Its renders are in resources/frostpaw, under the rover's names; one that is not there yet is
# RedPawAmp's. So a render of its own is a file dropped in there and nothing more: the folder is looked
# at again on every build.
file(GLOB FROSTPAW_OWN_RENDERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/frostpaw/*")
set(FROSTPAWAMP_RENDERS "")
foreach(render IN LISTS ROVER_RENDERS)
    if("${CMAKE_CURRENT_SOURCE_DIR}/resources/frostpaw/${render}" IN_LIST FROSTPAW_OWN_RENDERS)
        list(APPEND FROSTPAWAMP_RENDERS "${CMAKE_CURRENT_SOURCE_DIR}/resources/frostpaw/${render}")
    else()
        list(APPEND FROSTPAWAMP_RENDERS "${CMAKE_CURRENT_SOURCE_DIR}/resources/redpaw/${render}")
    endif()
endforeach()
# ...and renders a rover may carry or not — the maker's mark raised in its own metal, instead of engraved;
# TURBO's hatch shut, instead of open on PARK too: there when the file is, and nothing in its place when it
# is not. Never RedPawAmp's: a rusted hatch on frosted steel would be a patch.
foreach(render IN ITEMS emblem.png turbo-lid.png still.jpg)
    if("${CMAKE_CURRENT_SOURCE_DIR}/resources/frostpaw/${render}" IN_LIST FROSTPAW_OWN_RENDERS)
        list(APPEND FROSTPAWAMP_RENDERS "${CMAKE_CURRENT_SOURCE_DIR}/resources/frostpaw/${render}")
    endif()
endforeach()
juce_add_binary_data(FrostPawAmpData
    NAMESPACE   FrostPawData
    HEADER_NAME FrostPawData.h
    SOURCES     ${FROSTPAWAMP_RENDERS})

# Its own version, moved when FROSTPAWAMP is released (src/Product.h).
set(FROSTPAWAMP_VERSION 0.1.0)

# Its icon: its vehicle seen from the front, drawn as the cockpit's mark is — in the family's lilac with
# the orange dot — on the cockpit's rounded square with the violet round its edge. On the Mac the
# square macOS 26 expects (824 of the 1024 canvas, margins and all); elsewhere the same on the whole canvas.
if(APPLE)
    set(FROSTPAWAMP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/FrostPawAmp-macOS-1024.png")
else()
    set(FROSTPAWAMP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/FrostPawAmp-1024.png")
endif()

juce_add_plugin(FrostPawAmp
    VERSION                     "${FROSTPAWAMP_VERSION}"
    PRODUCT_NAME                "FrostPawAmp"
    COMPANY_NAME                "Darwin's Cat"
    BUNDLE_ID                   "com.darwinscat.frostpawamp"
    PLUGIN_MANUFACTURER_CODE    Dcat
    PLUGIN_CODE                 Frpw
    FORMATS                     VST3 AU Standalone
    VST3_CATEGORIES             "Fx" "Distortion"
    AU_MAIN_TYPE                "kAudioUnitType_Effect"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT  "FrostPawAmp needs audio input to process your signal."
    # Bundle / executable icon — its vehicle on the square; see FROSTPAWAMP_ICON.
    ICON_BIG                    "${FROSTPAWAMP_ICON}"
    COPY_PLUGIN_AFTER_BUILD     ${ORBITAMP_COPY_PLUGIN})

clap_juce_extensions_plugin(TARGET FrostPawAmp
    CLAP_ID       "com.darwinscat.frostpawamp"
    CLAP_FEATURES audio-effect "guitar" "amplifier")
if(APPLE)
    add_custom_command(TARGET FrostPawAmp_CLAP POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy
                "$<TARGET_PROPERTY:FrostPawAmp,JUCE_ICON_FILE>"
                "$<TARGET_BUNDLE_DIR:FrostPawAmp_CLAP>/Contents/Resources/clap.icns"
        VERBATIM)
endif()

# A rover, and its skin.
set(FROSTPAWAMP_SOURCES ${ROVER_SOURCES} ${FROSTPAW_VIEW_SOURCES} src/frostpaw/FrostPawSkin.cpp)
set(FROSTPAWAMP_LIBRARIES FrostPawAmpData ${ROVER_LIBRARIES})

target_sources(FrostPawAmp PRIVATE ${FROSTPAWAMP_SOURCES})
target_compile_definitions(FrostPawAmp PUBLIC
    ORBITAMP_VERSION="${FROSTPAWAMP_VERSION}"
    ORBITAMP_PRODUCT_NAME="FrostPawAmp"
    ORBITAMP_RELEASE_TAG_PREFIX="frostpaw-"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    $<$<PLATFORM_ID:Windows>:JUCE_ASIO=1>)
target_include_directories(FrostPawAmp PUBLIC "${ORBITAMP_GENERATED_DIR}")
add_dependencies(FrostPawAmp OrbitAmpVersionGen)
target_link_libraries(FrostPawAmp PRIVATE ${FROSTPAWAMP_LIBRARIES})

set(FROSTPAWAMP_FORMATS "")
foreach(fmt IN ITEMS VST3 AU Standalone CLAP)
    if(TARGET FrostPawAmp_${fmt})
        list(APPEND FROSTPAWAMP_FORMATS FrostPawAmp_${fmt})
    endif()
endforeach()

# ITS SET IS INSIDE ITS BINARY TOO, a list of its own — the V4's six packs its channels play, and the
# rovers' ten cabinets — laid out under its own list in the family's folder (src/channels/Set.h), where a
# file another product carries too is one file.
set(FROSTPAWAMP_SET
    "${ORBITAMP_SET_ZIPPED}/V4 Copper.orbitrig.zip"
    "${ORBITAMP_SET_ZIPPED}/V4 Copper TB.orbitrig.zip"
    "${ORBITAMP_SET_ZIPPED}/V4 Sheriff Ch1.orbitrig.zip"
    "${ORBITAMP_SET_ZIPPED}/V4 Sheriff Ch2.orbitrig.zip"
    "${ORBITAMP_SET_ZIPPED}/V4 Kraken Ch1.orbitrig.zip"
    "${ORBITAMP_SET_ZIPPED}/V4 Kraken Ch2.orbitrig.zip"
    ${ROVER_CABINETS})

add_library(FrostPawAmpSet INTERFACE)
juce_add_binary_data(FrostPawAmpSetData
    NAMESPACE   ProductSetData
    HEADER_NAME ProductSetData.h
    SOURCES     ${FROSTPAWAMP_SET})
add_dependencies(FrostPawAmpSetData OrbitAmpSetPacks)   # the packs are zipped by the build (THE SET)
set_target_properties(FrostPawAmpSetData PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_link_libraries(FrostPawAmpSet INTERFACE FrostPawAmpSetData)
target_compile_definitions(FrostPawAmpSet INTERFACE PRODUCT_SET_EMBEDDED=1)
target_link_libraries(FrostPawAmp PRIVATE FrostPawAmpSet)

# Not part of the default build, like the other faces: `--target FrostPawAmp_All` builds every format.
set_target_properties(FrostPawAmp ${FROSTPAWAMP_FORMATS} FrostPawAmpData PROPERTIES EXCLUDE_FROM_ALL TRUE)
add_dependencies(FrostPawAmp_All FrostPawAmp_CLAP)

# Its gates are the rover's, run as FrostPawAmp: its six channels on its packs, and its set laid out
# under its own list.
rover_console(frostpawamp_channel_test tests/RedPawChannelTests.cpp "FrostPawAmpChannelTest" FrostPawAmp "frostpaw-" 6)
rover_set_test(frostpawamp_set_test "FrostPawAmpSetTest" FrostPawAmp 6)

# ----------------------------------------------------------------------------
# AmberPawAmp — a helicopter over Titan's methane sea: a fifth face over the same processor, the rovers'
# dashboard worn over a cockpit of its own (src/amberpaw/AmberPawSkin.cpp) — dark gunmetal under a canopy of
# three panes, its tuner a gauge, its meters dots. The same controls, TURBO and PARK: the rovers', shared,
# not copied — over the V4's six channels in three gears, as FrostPawAmp has them until it has captures of
# its own, and the rovers' cabinets. Released apart under tags of its own (`amberpaw-vX.Y.Z`) — no release
# workflow yet. Built only when asked for by name.
# ----------------------------------------------------------------------------

# Its renders are in resources/amberpaw, under the rovers' names; one that is not there yet is RedPawAmp's,
# the way FrostPawAmp's are.
file(GLOB AMBERPAW_OWN_RENDERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/amberpaw/*")
set(AMBERPAWAMP_RENDERS "")
foreach(render IN LISTS ROVER_RENDERS)
    if("${CMAKE_CURRENT_SOURCE_DIR}/resources/amberpaw/${render}" IN_LIST AMBERPAW_OWN_RENDERS)
        list(APPEND AMBERPAWAMP_RENDERS "${CMAKE_CURRENT_SOURCE_DIR}/resources/amberpaw/${render}")
    else()
        list(APPEND AMBERPAWAMP_RENDERS "${CMAKE_CURRENT_SOURCE_DIR}/resources/redpaw/${render}")
    endif()
endforeach()
# ...and the renders a rover may carry or not: there when the file is, nothing in its place when it is not.
foreach(render IN ITEMS emblem.png turbo-lid.png still.jpg)
    if("${CMAKE_CURRENT_SOURCE_DIR}/resources/amberpaw/${render}" IN_LIST AMBERPAW_OWN_RENDERS)
        list(APPEND AMBERPAWAMP_RENDERS "${CMAKE_CURRENT_SOURCE_DIR}/resources/amberpaw/${render}")
    endif()
endforeach()
juce_add_binary_data(AmberPawAmpData
    NAMESPACE   AmberPawData
    HEADER_NAME AmberPawData.h
    SOURCES     ${AMBERPAWAMP_RENDERS})

# Its own version, moved when AMBERPAWAMP is released (src/Product.h).
set(AMBERPAWAMP_VERSION 0.1.0)

# Its icon: its vehicle seen from the front, drawn as the cockpit's mark is — in the family's lilac with
# the orange dot — on the cockpit's rounded square with the violet round its edge. On the Mac the
# square macOS 26 expects (824 of the 1024 canvas, margins and all); elsewhere the same on the whole canvas.
if(APPLE)
    set(AMBERPAWAMP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/AmberPawAmp-macOS-1024.png")
else()
    set(AMBERPAWAMP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/AmberPawAmp-1024.png")
endif()

juce_add_plugin(AmberPawAmp
    VERSION                     "${AMBERPAWAMP_VERSION}"
    PRODUCT_NAME                "AmberPawAmp"
    COMPANY_NAME                "Darwin's Cat"
    BUNDLE_ID                   "com.darwinscat.amberpawamp"
    PLUGIN_MANUFACTURER_CODE    Dcat
    PLUGIN_CODE                 Ambp
    FORMATS                     VST3 AU Standalone
    VST3_CATEGORIES             "Fx" "Distortion"
    AU_MAIN_TYPE                "kAudioUnitType_Effect"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT  "AmberPawAmp needs audio input to process your signal."
    # Bundle / executable icon — its vehicle on the square; see AMBERPAWAMP_ICON.
    ICON_BIG                    "${AMBERPAWAMP_ICON}"
    COPY_PLUGIN_AFTER_BUILD     ${ORBITAMP_COPY_PLUGIN})

clap_juce_extensions_plugin(TARGET AmberPawAmp
    CLAP_ID       "com.darwinscat.amberpawamp"
    CLAP_FEATURES audio-effect "guitar" "amplifier")
if(APPLE)
    add_custom_command(TARGET AmberPawAmp_CLAP POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy
                "$<TARGET_PROPERTY:AmberPawAmp,JUCE_ICON_FILE>"
                "$<TARGET_BUNDLE_DIR:AmberPawAmp_CLAP>/Contents/Resources/clap.icns"
        VERBATIM)
endif()

# A rover, and its skin. Out of its canopy, the flight over Titan's methane sea (src/amberpaw/view).
set(AMBERPAWAMP_SOURCES ${ROVER_SOURCES} ${AMBERPAW_VIEW_SOURCES} src/amberpaw/AmberPawSkin.cpp)
set(AMBERPAWAMP_LIBRARIES AmberPawAmpData ${ROVER_LIBRARIES})

target_sources(AmberPawAmp PRIVATE ${AMBERPAWAMP_SOURCES})
target_compile_definitions(AmberPawAmp PUBLIC
    ORBITAMP_VERSION="${AMBERPAWAMP_VERSION}"
    ORBITAMP_PRODUCT_NAME="AmberPawAmp"
    ORBITAMP_RELEASE_TAG_PREFIX="amberpaw-"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    $<$<PLATFORM_ID:Windows>:JUCE_ASIO=1>)
target_include_directories(AmberPawAmp PUBLIC "${ORBITAMP_GENERATED_DIR}")
add_dependencies(AmberPawAmp OrbitAmpVersionGen)
target_link_libraries(AmberPawAmp PRIVATE ${AMBERPAWAMP_LIBRARIES})

set(AMBERPAWAMP_FORMATS "")
foreach(fmt IN ITEMS VST3 AU Standalone CLAP)
    if(TARGET AmberPawAmp_${fmt})
        list(APPEND AMBERPAWAMP_FORMATS AmberPawAmp_${fmt})
    endif()
endforeach()

# ITS SET IS INSIDE ITS BINARY TOO, a list of its own — for now FrostPawAmp's: the V4's six packs its
# channels play, and the rovers' ten cabinets — laid out under its own list in the family's folder
# (src/channels/Set.h), where a file another product carries too is one file.
set(AMBERPAWAMP_SET
    "${ORBITAMP_SET_ZIPPED}/V4 Copper.orbitrig.zip"
    "${ORBITAMP_SET_ZIPPED}/V4 Copper TB.orbitrig.zip"
    "${ORBITAMP_SET_ZIPPED}/V4 Sheriff Ch1.orbitrig.zip"
    "${ORBITAMP_SET_ZIPPED}/V4 Sheriff Ch2.orbitrig.zip"
    "${ORBITAMP_SET_ZIPPED}/V4 Kraken Ch1.orbitrig.zip"
    "${ORBITAMP_SET_ZIPPED}/V4 Kraken Ch2.orbitrig.zip"
    ${ROVER_CABINETS})

add_library(AmberPawAmpSet INTERFACE)
juce_add_binary_data(AmberPawAmpSetData
    NAMESPACE   ProductSetData
    HEADER_NAME ProductSetData.h
    SOURCES     ${AMBERPAWAMP_SET})
add_dependencies(AmberPawAmpSetData OrbitAmpSetPacks)   # the packs are zipped by the build (THE SET)
set_target_properties(AmberPawAmpSetData PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_link_libraries(AmberPawAmpSet INTERFACE AmberPawAmpSetData)
target_compile_definitions(AmberPawAmpSet INTERFACE PRODUCT_SET_EMBEDDED=1)
target_link_libraries(AmberPawAmp PRIVATE AmberPawAmpSet)

# Not part of the default build, like the other faces: `--target AmberPawAmp_All` builds every format.
set_target_properties(AmberPawAmp ${AMBERPAWAMP_FORMATS} AmberPawAmpData PROPERTIES EXCLUDE_FROM_ALL TRUE)
add_dependencies(AmberPawAmp_All AmberPawAmp_CLAP)

# Its gates are the rover's, run as AmberPawAmp: its six channels on its packs, and its set laid out
# under its own list.
rover_console(amberpawamp_channel_test tests/RedPawChannelTests.cpp "AmberPawAmpChannelTest" AmberPawAmp "amberpaw-" 6)
rover_set_test(amberpawamp_set_test "AmberPawAmpSetTest" AmberPawAmp 6)

# ----------------------------------------------------------------------------
# Headless acceptance gate for the EQ link. JUCE-free — it links only felitronics::eq, which is
# the point of keeping core::EqLink free of JUCE: the DSP is testable without a host, a GUI, or
# an audio device. Non-zero exit on any failure.
# ----------------------------------------------------------------------------
# Diagnostics, not gates: what the stage sees in packs, how hot the chain runs, and offline
# listening renders — the instruments that caught the fuzz.
juce_add_console_app(orbitamp_pack_probe PRODUCT_NAME "OrbitAmpPackProbe")
target_sources(orbitamp_pack_probe PRIVATE tests/PackProbe.cpp)
target_include_directories(orbitamp_pack_probe PRIVATE src)
target_compile_definitions(orbitamp_pack_probe PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(orbitamp_pack_probe PRIVATE
    felitronics::lineareq felitronics::nam felitronics::rigplayer felitronics::limiter
    juce::juce_dsp juce::juce_events juce::juce_gui_basics juce::juce_audio_formats juce::juce_audio_processors
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

juce_add_console_app(orbitamp_render_probe PRODUCT_NAME "OrbitAmpRenderProbe")
target_sources(orbitamp_render_probe PRIVATE tests/RenderProbe.cpp)
target_include_directories(orbitamp_render_probe PRIVATE src)
target_compile_definitions(orbitamp_render_probe PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(orbitamp_render_probe PRIVATE
    felitronics::lineareq felitronics::nam felitronics::rigplayer felitronics::limiter
    juce::juce_dsp juce::juce_events juce::juce_gui_basics juce::juce_audio_formats juce::juce_audio_processors
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

juce_add_console_app(orbitamp_level_probe PRODUCT_NAME "OrbitAmpLevelProbe")
target_sources(orbitamp_level_probe PRIVATE tests/LevelProbe.cpp)
target_include_directories(orbitamp_level_probe PRIVATE src)
target_compile_definitions(orbitamp_level_probe PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(orbitamp_level_probe PRIVATE
    felitronics::lineareq felitronics::nam felitronics::rigplayer felitronics::limiter
    juce::juce_dsp juce::juce_events juce::juce_gui_basics juce::juce_audio_formats juce::juce_audio_processors
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

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

# Same gate for the reverb. It links juce_audio_basics because that stage is JUCE-bound by
# construction (Freeverb) — which is exactly why it is a SEPARATE target: the tone-stack gate above
# stays JUCE-free, and the split makes the dependency visible instead of quietly universal.
juce_add_console_app(orbitamp_reverb_test PRODUCT_NAME "OrbitAmpReverbTest")
target_sources(orbitamp_reverb_test PRIVATE tests/ReverbStageTests.cpp)
target_include_directories(orbitamp_reverb_test PRIVATE src)
target_compile_definitions(orbitamp_reverb_test PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(orbitamp_reverb_test PRIVATE
    felitronics::eq
    juce::juce_audio_basics
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
        add_custom_command(TARGET OrbitAmp_Standalone POST_BUILD
            COMMAND codesign --force --options runtime --timestamp=none
                    --entitlements "${CMAKE_CURRENT_SOURCE_DIR}/installer/standalone.entitlements"
                    --sign "${ORBITAMP_SIGN_IDENTITY}"
                    "$<TARGET_BUNDLE_DIR:OrbitAmp_Standalone>"
            COMMENT "OrbitAmp: signing the standalone so macOS remembers the mic grant"
            VERBATIM)

        # The cockpit is a standalone too, and asks for the same microphone.
        if(TARGET CatpitAmp_Standalone)
            add_custom_command(TARGET CatpitAmp_Standalone POST_BUILD
                COMMAND codesign --force --options runtime --timestamp=none
                        --entitlements "${CMAKE_CURRENT_SOURCE_DIR}/installer/standalone.entitlements"
                        --sign "${ORBITAMP_SIGN_IDENTITY}"
                        "$<TARGET_BUNDLE_DIR:CatpitAmp_Standalone>"
                COMMENT "CatpitAmp: signing the standalone so macOS remembers the mic grant"
                VERBATIM)
        endif()

        # ...and so is the rover.
        if(TARGET RedPawAmp_Standalone)
            add_custom_command(TARGET RedPawAmp_Standalone POST_BUILD
                COMMAND codesign --force --options runtime --timestamp=none
                        --entitlements "${CMAKE_CURRENT_SOURCE_DIR}/installer/standalone.entitlements"
                        --sign "${ORBITAMP_SIGN_IDENTITY}"
                        "$<TARGET_BUNDLE_DIR:RedPawAmp_Standalone>"
                COMMENT "RedPawAmp: signing the standalone so macOS remembers the mic grant"
                VERBATIM)
        endif()

        # ...and the ice moon's.
        if(TARGET FrostPawAmp_Standalone)
            add_custom_command(TARGET FrostPawAmp_Standalone POST_BUILD
                COMMAND codesign --force --options runtime --timestamp=none
                        --entitlements "${CMAKE_CURRENT_SOURCE_DIR}/installer/standalone.entitlements"
                        --sign "${ORBITAMP_SIGN_IDENTITY}"
                        "$<TARGET_BUNDLE_DIR:FrostPawAmp_Standalone>"
                COMMENT "FrostPawAmp: signing the standalone so macOS remembers the mic grant"
                VERBATIM)
        endif()

        # ...and the helicopter's.
        if(TARGET AmberPawAmp_Standalone)
            add_custom_command(TARGET AmberPawAmp_Standalone POST_BUILD
                COMMAND codesign --force --options runtime --timestamp=none
                        --entitlements "${CMAKE_CURRENT_SOURCE_DIR}/installer/standalone.entitlements"
                        --sign "${ORBITAMP_SIGN_IDENTITY}"
                        "$<TARGET_BUNDLE_DIR:AmberPawAmp_Standalone>"
                COMMENT "AmberPawAmp: signing the standalone so macOS remembers the mic grant"
                VERBATIM)
        endif()
    else()
        message(STATUS "Standalone signing: no '${ORBITAMP_SIGN_IDENTITY}' identity — staying ad-hoc (mic prompt every launch)")
    endif()

    # A REBUILD NEVER MOVES THE BUNDLE'S OWN DATE — only what is inside it changes — and that date is
    # what macOS keys its icon cache on. A bundle first built before it had an icon kept the blank
    # placeholder in the Dock and the app switcher for good, with the right icon sitting inside it.
    # So the bundle is touched after every build, and the system looks again.
    foreach(standalone OrbitAmp_Standalone CatpitAmp_Standalone RedPawAmp_Standalone FrostPawAmp_Standalone)
        if(TARGET ${standalone})
            add_custom_command(TARGET ${standalone} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E touch_nocreate "$<TARGET_BUNDLE_DIR:${standalone}>"
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
target_sources(orbitamp_chain_test PRIVATE
    tests/ChainTests.cpp
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
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
target_include_directories(orbitamp_chain_test PRIVATE src)
target_include_directories(orbitamp_chain_test PRIVATE "${ORBITAMP_GENERATED_DIR}")   # the same faces, the same stamp
add_dependencies(orbitamp_chain_test OrbitAmpVersionGen)
target_compile_definitions(orbitamp_chain_test PRIVATE ORBITAMP_VERSION="${PROJECT_VERSION}"
                                                      JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(orbitamp_chain_test PRIVATE
    OrbitAmpData
    felitronics::analysis
    felitronics::appkit
    felitronics::appkit_webp         # the panel decodes a pack's photograph — same half the plugin links
    felitronics::core                # core::BypassWire reads the resampler's delay geometry
    felitronics::dehum
    felitronics::dynamics
    felitronics::eq
    felitronics::lineareq
    felitronics::convolution         # the cabinet convolves through CabConvolver
    felitronics::measurement
    felitronics::nam
    felitronics::rigplayer
    felitronics::limiter
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_data_structures
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ----------------------------------------------------------------------------
# Gate for the hum at the input, heard through the processor's own input path — the tuner's tap,
# which listens after the trim: AUTO takes a mains hum off; an INPUT trim moved mid-stream does not
# bring it back, because the stage sits ahead of the trim; OFF hands the input on bit for bit; and a
# demo loop with no hum in it passes untouched. Needs no pack: the input path is ahead of every block.
# ----------------------------------------------------------------------------
juce_add_console_app(orbitamp_hum_test PRODUCT_NAME "OrbitAmpHumTest")
target_sources(orbitamp_hum_test PRIVATE
    tests/HumTests.cpp
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
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
target_include_directories(orbitamp_hum_test PRIVATE src)
target_include_directories(orbitamp_hum_test PRIVATE "${ORBITAMP_GENERATED_DIR}")
add_dependencies(orbitamp_hum_test OrbitAmpVersionGen)
target_compile_definitions(orbitamp_hum_test PRIVATE ORBITAMP_VERSION="${PROJECT_VERSION}"
                                                    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(orbitamp_hum_test PRIVATE
    OrbitAmpData
    felitronics::analysis
    felitronics::appkit
    felitronics::appkit_webp
    felitronics::core
    felitronics::dehum
    felitronics::dynamics
    felitronics::eq
    felitronics::lineareq
    felitronics::convolution
    felitronics::measurement
    felitronics::nam
    felitronics::rigplayer
    felitronics::limiter
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_data_structures
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

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
target_compile_definitions(orbitamp_library_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(orbitamp_library_test PRIVATE
    felitronics::nam                 # DeviceLibrary.h reads pack manifests through namz
    juce::juce_events
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ----------------------------------------------------------------------------
# Gate for OrbitAmp's set as it leaves the binary — every pack of the set laid out under a name the
# device list tells apart, once, byte for byte. Runs entirely in a temp folder: the machine's real
# Factory folder is never written.
# ----------------------------------------------------------------------------
juce_add_console_app(orbitamp_set_test PRODUCT_NAME "OrbitAmpSetTest")
target_sources(orbitamp_set_test PRIVATE
    tests/OrbitAmpSetTests.cpp
    src/device/FactorySet.cpp)
target_include_directories(orbitamp_set_test PRIVATE src)
target_compile_definitions(orbitamp_set_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(orbitamp_set_test PRIVATE
    OrbitAmpSetData
    felitronics::nam                 # DeviceLibrary.h reads pack manifests through namz
    juce::juce_events
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ----------------------------------------------------------------------------
# Gate for the family's Factory folder with more than one product laying out in it — two products of
# make-believe in one folder: what each keeps, what one leaves to the other when it drops a file, and
# the folder a product kept before the family shared one, emptied of exactly what its list named.
# Neither product's: built and run with OrbitAmp's gates, and the cockpit's release runs it too. Runs
# entirely in a temp folder.
# ----------------------------------------------------------------------------
juce_add_console_app(orbitamp_factory_test PRODUCT_NAME "OrbitAmpFactoryTest")
target_sources(orbitamp_factory_test PRIVATE
    tests/FactorySetTests.cpp
    src/device/FactorySet.cpp)
target_include_directories(orbitamp_factory_test PRIVATE src)
target_compile_definitions(orbitamp_factory_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(orbitamp_factory_test PRIVATE
    juce::juce_core
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
