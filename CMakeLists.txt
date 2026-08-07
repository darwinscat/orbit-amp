cmake_minimum_required(VERSION 3.22)

# ----------------------------------------------------------------------------
# orbit-amp — the device / voicing PLAYER plugin (C++/JUCE). Felitronics line, by Darwin's Cat.
#
# The preamp/voicing side of the chain:  boost -> preamp (voicing) -> EQ -> reverb.
# Power amp + cabinet live downstream in OrbitCab.
#
# One CMake config builds VST3 + AU + Standalone. JUCE 8 and the felitronics libraries are
# fetched by CMake (FetchContent) — NOT vendored, NOT system-installed. Each dependency is
# HYBRID: a local sibling checkout is used when present (co-development), otherwise a pinned
# release is fetched, so a clean clone still configures.
# ----------------------------------------------------------------------------

project(OrbitAmp VERSION 0.1.0 LANGUAGES C CXX)

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
# felitronics-core — shared, JUCE-free, header-only DSP core (eq / dynamics / saturation /
# neural / ...). Nothing is linked yet: the chain stages land one at a time and each adds its
# module to target_link_libraries below. Wired now so that's a one-line change, not a rebuild
# of the dependency graph. Its self-tests do NOT build from here (guarded by PROJECT_IS_TOP_LEVEL).
# ----------------------------------------------------------------------------
set(ORBITAMP_FCORE_TAG "v0.9.0" CACHE STRING "felitronics-core release tag (fetched when no local checkout)")
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
FetchContent_MakeAvailable(felitronics_core)

# ----------------------------------------------------------------------------
# felitronics-appkit — the family's shared app-side JUCE infrastructure (brand kit, update
# check, device glyphs). Header-only INTERFACE target: it never links JUCE itself, we supply
# the modules transitively. This is also where the faceplate render kit MOVES once it settles
# here (see CLAUDE.md — build it here first, move it later).
# ----------------------------------------------------------------------------
set(ORBITAMP_APPKIT_TAG "v0.8.0" CACHE STRING "felitronics-appkit release tag (fetched when no local checkout)")
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
FetchContent_MakeAvailable(felitronics_appkit)

# ----------------------------------------------------------------------------
# The plugin target.
#
# 4-char codes — LOCK these before the first release (changing them afterwards makes hosts
# treat it as a different plugin):
#   PLUGIN_MANUFACTURER_CODE = Dcat  (Darwin's Cat — >=1 uppercase, AU rule; same as OrbitCab)
#   PLUGIN_CODE              = Oamp  (OrbitAmp — unique per plugin; OrbitCab holds `Orbt`)
#
# CLAP comes later via clap-juce-extensions (one extra format, no source changes) — not wired
# yet so the scaffold stays small.
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
    COPY_PLUGIN_AFTER_BUILD     ${ORBITAMP_COPY_PLUGIN}
)

target_sources(OrbitAmp PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/Parameters.cpp
    src/ui/Theme.cpp
    src/ui/FaceplateView.cpp
    src/ui/PreampBlock.cpp
    src/ui/EqSection.cpp
    src/ui/ReverbBlock.cpp
    src/ui/Chrome.cpp
)

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
)

target_compile_definitions(OrbitAmp PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0   # JUCE 8 has no splash requirement on any tier, incl. AGPLv3
)

target_link_libraries(OrbitAmp PRIVATE
    OrbitAmpData                     # embedded Michroma — BinaryData::* symbols
    felitronics::appkit              # shared app-side kit (header-only; JUCE comes from us)
    felitronics::eq                  # matched biquads behind the tone stack (core::ToneStack)
    juce::juce_audio_utils           # pulls audio_processors / audio_basics / gui_basics ...
    juce::juce_dsp                   # the chain stages will build on this
    juce::juce_data_structures       # ValueTree behind the workspace / presets
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# ----------------------------------------------------------------------------
# Headless acceptance gate for the tone stack. JUCE-free — it links only felitronics::eq, which is
# the point of keeping core::ToneStack free of JUCE: the DSP is testable without a host, a GUI, or
# an audio device. Non-zero exit on any failure.
# ----------------------------------------------------------------------------
add_executable(orbitamp_tonestack_test tests/ToneStackTests.cpp)
target_include_directories(orbitamp_tonestack_test PRIVATE src)
target_link_libraries(orbitamp_tonestack_test PRIVATE felitronics::eq)
target_compile_features(orbitamp_tonestack_test PRIVATE cxx_std_20)

# Same gate for the reverb. It links juce_audio_basics because that stage is JUCE-bound by
# construction (Freeverb) — which is exactly why it is a SEPARATE target: the tone-stack gate above
# stays JUCE-free, and the split makes the dependency visible instead of quietly universal.
juce_add_console_app(orbitamp_reverb_test PRODUCT_NAME "OrbitAmpReverbTest")
target_sources(orbitamp_reverb_test PRIVATE tests/ReverbStageTests.cpp)
target_include_directories(orbitamp_reverb_test PRIVATE src)
target_compile_definitions(orbitamp_reverb_test PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(orbitamp_reverb_test PRIVATE
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
