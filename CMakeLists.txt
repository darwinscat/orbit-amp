cmake_minimum_required(VERSION 3.22)

# ----------------------------------------------------------------------------
# orbit-amp — the device / voicing PLAYER plugin (C++/JUCE). Felitronics line, by Darwin's Cat.
#
# The chain:  boost -> EQ -> preamp (voicing) -> EQ -> delay -> reverb -> power amp -> cabinet.
#
# One CMake config builds VST3 + AU + Standalone. JUCE 8 and the felitronics libraries are
# fetched by CMake (FetchContent) — NOT vendored, NOT system-installed. Each dependency is
# HYBRID: a local sibling checkout is used when present (co-development), otherwise a pinned
# release is fetched, so a clean clone still configures.
# ----------------------------------------------------------------------------

project(OrbitAmp VERSION 0.2.0 LANGUAGES C CXX)

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
# JUCE 8.0.14 tree another Felitronics project already fetched, so a first build here costs no
# second full clone; if that path is gone, the pin is fetched instead. Bump the tag deliberately.
# ----------------------------------------------------------------------------
set(ORBITAMP_JUCE_TAG "8.0.14" CACHE STRING "JUCE git tag to build against")
set(ORBITAMP_JUCE_DIR "$ENV{HOME}/IdeaProjects/orbit-capture/app/tests/build/_deps/juce-src"
    CACHE PATH "Local JUCE source tree (used if present); else the pinned tag is fetched")
if(ORBITAMP_JUCE_DIR AND EXISTS "${ORBITAMP_JUCE_DIR}/CMakeLists.txt")
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
set(ORBITAMP_FCORE_TAG "v0.24.0" CACHE STRING "felitronics-core release tag (fetched when no local checkout)")

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
# The neural runtime. felitronics::nam is OFF by default so a consumer that does not need it stays
# header-only; orbit-amp plays captured devices, so it needs it. The module fetches
# NeuralAmpModelerCore and the namz codec itself — the overrides below only spare a second clone of
# trees this machine already has, and fall back to the pins when they are gone.
set(FELITRONICS_WITH_NAM ON CACHE BOOL "orbit-amp plays .namz captures" FORCE)

set(ORBITAMP_NAM_CORE_SRC "$ENV{HOME}/IdeaProjects/orbit-nam-capture/app/build-hard/_deps/nam_core_src-src"
    CACHE PATH "Local NeuralAmpModelerCore checkout; empty fetches the pin")
set(ORBITAMP_NAMZ_SRC "$ENV{HOME}/IdeaProjects/namz"
    CACHE PATH "Local namz checkout; empty fetches the pin")

if(EXISTS "${ORBITAMP_NAM_CORE_SRC}")
    set(FELITRONICS_NAM_CORE_SRC "${ORBITAMP_NAM_CORE_SRC}" CACHE PATH "" FORCE)
endif()
if(EXISTS "${ORBITAMP_NAMZ_SRC}/include/namz.h")
    set(FELITRONICS_NAMZ_SRC "${ORBITAMP_NAMZ_SRC}" CACHE PATH "" FORCE)
endif()

# ...and when there is NO local checkout — CI — the tag core would otherwise pin. The device page
# reads a pack's photograph and the box's own history (`Rig::picture`, `gear.year`, `serial_number`,
# `designed_in`, `made_in`), all of which are newer than the commit felitronics-core carries. Pinned
# HERE rather than waited for: a core release exists to move core's own code, and holding this
# behind one would leave the plugin unbuildable on CI for no reason of its own.
set(ORBITAMP_NAMZ_TAG "v3.3.0" CACHE STRING "namz release tag CI builds against (no local checkout)")
set(FELITRONICS_NAMZ_TAG "${ORBITAMP_NAMZ_TAG}" CACHE STRING "" FORCE)

FetchContent_MakeAvailable(felitronics_core)

# ----------------------------------------------------------------------------
# felitronics-appkit — the family's shared app-side JUCE infrastructure (brand kit, update
# check, device glyphs). Header-only INTERFACE target: it never links JUCE itself, we supply
# the modules transitively. This is also where the faceplate render kit MOVES once it settles
# here (see CLAUDE.md — build it here first, move it later).
# ----------------------------------------------------------------------------
set(ORBITAMP_APPKIT_TAG "v0.12.0" CACHE STRING "felitronics-appkit release tag (fetched when no local checkout)")
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

# ...and the same for felitronics-appkit: the badge's dependency rows name what was actually built
# against, sibling or pin, never the other one.
if(ORBITAMP_APPKIT_DIR AND EXISTS "${ORBITAMP_APPKIT_DIR}/CMakeLists.txt")
    set(_oa_appkit_local ON)
else()
    set(_oa_appkit_local OFF)
endif()

# The inference engine is nobody's checkout: felitronics-core pins a COMMIT of upstream's
# NeuralAmpModelerCore, and we only point the fetch at a copy that is already unpacked next door.
# So the row is always "pin", and the directory is whichever of the two the build actually used.
if(ORBITAMP_NAM_CORE_SRC AND EXISTS "${ORBITAMP_NAM_CORE_SRC}/.git")
    set(_oa_namcore_dir "${ORBITAMP_NAM_CORE_SRC}")
else()
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
    # Bundle / executable icon — the cat on the family ground (resources/icon/OrbitAmp-1024.png).
    # One 1024-square PNG is enough: juceaide builds the macOS .icns for every bundle and the
    # Windows .ico from it. No ICON_SMALL on purpose — see OrbitCab: juceaide reads the pair
    # small-then-big while JUCEUtils appends big-then-small, and passing both can swap the slots.
    ICON_BIG                    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/OrbitAmp-1024.png"
    COPY_PLUGIN_AFTER_BUILD     ${ORBITAMP_COPY_PLUGIN}
)

target_sources(OrbitAmp PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/Parameters.cpp
    src/ui/Theme.cpp
    src/ui/FaceplateView.cpp
    src/ui/CapturedBlockPanel.cpp
    src/ui/CabinetBlock.cpp
    src/ui/PowerAmpBlock.cpp
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
# Factory devices. DeviceLibrary::bundledDirectory() looks inside the bundle first
# (Contents/Resources/Devices) and falls back to the machine's Factory folder only when the build
# ships nothing. The packs are curated by hand and arrive late, so the rule is a folder, not a
# list: whatever sits in resources/Devices/ at CONFIGURE time rides into every bundle — drop the
# packs in and re-run cmake. Absent or empty, the build ships an empty factory. The resources land
# during bundle assembly, so they are inside every seal that is applied afterwards (dev signing,
# the copy-plugin step, the release signature).
# ----------------------------------------------------------------------------
file(GLOB ORBITAMP_FACTORY_PACKS "${CMAKE_CURRENT_SOURCE_DIR}/resources/Devices/*")
if(ORBITAMP_FACTORY_PACKS)
    foreach(fmt IN ITEMS VST3 AU Standalone CLAP)
        if(TARGET OrbitAmp_${fmt})
            juce_add_bundle_resources_directory(OrbitAmp_${fmt}
                "${CMAKE_CURRENT_SOURCE_DIR}/resources/Devices")
        endif()
    endforeach()
    list(LENGTH ORBITAMP_FACTORY_PACKS ORBITAMP_FACTORY_COUNT)
    message(STATUS "Factory devices: ${ORBITAMP_FACTORY_COUNT} pack(s) ride in the bundles")
else()
    message(STATUS "Factory devices: resources/Devices/ is empty or absent — the factory ships empty")
endif()

# ----------------------------------------------------------------------------
# Embedded assets. Michroma (OFL) is the family's display face and it lives in felitronics-appkit's
# assets/ — referenced there, NOT copied here, so there is one copy of the file in the ecosystem.
# felitronics_appkit_SOURCE_DIR is set by FetchContent above and points at whichever source won
# (local checkout or fetched release).
# ----------------------------------------------------------------------------
juce_add_binary_data(OrbitAmpData
    SOURCES
        "${felitronics_appkit_SOURCE_DIR}/assets/Michroma-Regular.ttf"
        "${felitronics_appkit_SOURCE_DIR}/assets/catlogo.svg"

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
)

target_include_directories(OrbitAmp PUBLIC "${ORBITAMP_GENERATED_DIR}")   # OrbitAmpVersion.h
add_dependencies(OrbitAmp OrbitAmpVersionGen)                            # ...rebaked before every build

target_link_libraries(OrbitAmp PRIVATE
    OrbitAmpData                     # embedded Michroma — BinaryData::* symbols
    felitronics::analysis            # spectrum taps + panes behind the EQ curves
    felitronics::appkit              # shared app-side kit (header-only; JUCE comes from us)
    felitronics::appkit_webp         # juce::Image <- WebP, for the picture a pack ships
    felitronics::dynamics            # the noise gate (felitronics::dynamics::NoiseGate)
    felitronics::eq                  # matched biquads behind the tone stack (core::ToneStack)
    felitronics::lineareq            # the tone curve read off the player's grid (core::CapturedBlock)
    felitronics::measurement         # reference-unity IR normalization (core::CabinetIr)
    felitronics::nam                 # NAM inference — the captured devices actually play
    felitronics::rigplayer           # the pack player: captures, their crossfade, tone, alignment
    juce::juce_audio_utils           # pulls audio_processors / audio_basics / gui_basics ...
    juce::juce_dsp                   # the chain stages will build on this
    juce::juce_data_structures       # ValueTree behind the workspace / presets
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

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
    felitronics::lineareq felitronics::nam felitronics::rigplayer
    juce::juce_dsp juce::juce_events juce::juce_gui_basics juce::juce_audio_formats juce::juce_audio_processors
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

juce_add_console_app(orbitamp_render_probe PRODUCT_NAME "OrbitAmpRenderProbe")
target_sources(orbitamp_render_probe PRIVATE tests/RenderProbe.cpp)
target_include_directories(orbitamp_render_probe PRIVATE src)
target_compile_definitions(orbitamp_render_probe PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(orbitamp_render_probe PRIVATE
    felitronics::lineareq felitronics::nam felitronics::rigplayer
    juce::juce_dsp juce::juce_events juce::juce_gui_basics juce::juce_audio_formats juce::juce_audio_processors
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

juce_add_console_app(orbitamp_level_probe PRODUCT_NAME "OrbitAmpLevelProbe")
target_sources(orbitamp_level_probe PRIVATE tests/LevelProbe.cpp)
target_include_directories(orbitamp_level_probe PRIVATE src)
target_compile_definitions(orbitamp_level_probe PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(orbitamp_level_probe PRIVATE
    felitronics::lineareq felitronics::nam felitronics::rigplayer
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
    else()
        message(STATUS "Standalone signing: no '${ORBITAMP_SIGN_IDENTITY}' identity — staying ad-hoc (mic prompt every launch)")
    endif()
endif()

# ----------------------------------------------------------------------------
# Acceptance gate for the power amp's CALIBRATION — felitronics tests the tube stage itself; what is
# ours is the knob mapping. Links juce_dsp for the bypass delay line.

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
    src/ui/PowerAmpBlock.cpp
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
    felitronics::dynamics
    felitronics::eq
    felitronics::lineareq
    felitronics::measurement
    felitronics::nam
    felitronics::rigplayer
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
