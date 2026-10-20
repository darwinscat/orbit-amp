# ----------------------------------------------------------------------------
# MULTIWRAP — the toy that proves the wrappers holding several plug-ins (cmake/PatchedWrappers.cmake) before
# an amp rides on them. A project of its own, never part of the amps' build and never installed anywhere:
# it is built, validated, and that is all.
#
#   cmake -S tests/multiwrap -B build-multiwrap -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-multiwrap
#   cd build-multiwrap
#   MultiwrapHost_artefacts/Release/MultiwrapHost "DarwinsCatAmps_artefacts/Release/VST3/Darwin's Cat Amps.vst3" \
#       "DarwinsCatAmps_artefacts/Release/CLAP/Darwin's Cat Amps.clap" list
#   MultiwrapHost_artefacts/Release/MultiwrapHost ToyOrbitAmp_artefacts/Release/VST3/OrbitAmp.vst3 \
#       ToyOrbitAmp_artefacts/Release/CLAP/OrbitAmp.clap single
#
# and then pluginval on the .vst3 and clap-validator on the .clap. It builds, from the amps' own pins (read
# off the top-level CMakeLists.txt, where they are said) and the same patches:
#
#   DarwinsCatAmps   ONE VST3 and ONE CLAP, "Darwin's Cat Amps", holding OrbitAmp, CatpitAmp and RedPawAmp
#                    (ToyPluginList.h) under the family's manufacturer code and each amp's real code and
#                    CLAP id — so its classes can be held against what each amp's own bundle shipped with;
#   ToyOrbitAmp      the toy's OrbitAmp built alone, with no list, as every amp is built today;
#   MultiwrapHost    the host that holds both to what they must be (MultiwrapHost.cpp).
#
# MULTIWRAP_UNPATCHED=ON takes the pins as they are, unpatched, and builds ToyOrbitAmp and the host alone:
# the row before the patch, which the patched ToyOrbitAmp must match.
# ----------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.22)
project(Multiwrap LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MULTIWRAP_UNPATCHED "Build ToyOrbitAmp on the unpatched pins (the row before the patch)" OFF)

# THE AMPS' PINS AND VERSION, where the amps say them — never a second copy that could drift.
set(root "${CMAKE_CURRENT_SOURCE_DIR}/../..")
file(STRINGS "${root}/CMakeLists.txt" pins REGEX "^set\\(ORBITAMP_(JUCE|CLAP_EXT)_TAG \"")
foreach(pin IN LISTS pins)
    string(REGEX MATCH "^set\\((ORBITAMP_[A-Z_]+) \"([^\"]+)\"" matched "${pin}")
    set(${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
endforeach()
file(STRINGS "${root}/CMakeLists.txt" version REGEX "^project\\(OrbitAmp VERSION ")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" version "${version}")
if(NOT ORBITAMP_JUCE_TAG OR NOT ORBITAMP_CLAP_EXT_TAG OR NOT version)
    message(FATAL_ERROR "multiwrap: the pins or the version were not found in ${root}/CMakeLists.txt")
endif()
message(STATUS "multiwrap: JUCE ${ORBITAMP_JUCE_TAG}, clap-juce-extensions ${ORBITAMP_CLAP_EXT_TAG}, version ${version}")

# No local JUCE tree unless one is named: the toy runs where the pin is fetched. Naming one tries the
# escape hatch — PatchedWrappers.cmake copies it and patches the copy.
set(ORBITAMP_JUCE_DIR "" CACHE PATH "A local JUCE tree of the pinned version (copied, then patched)")

if(MULTIWRAP_UNPATCHED)
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${ORBITAMP_JUCE_TAG}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(juce)
    FetchContent_Declare(clap-juce-extensions
        GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
        GIT_TAG        ${ORBITAMP_CLAP_EXT_TAG})
    FetchContent_MakeAvailable(clap-juce-extensions)
else()
    include("${root}/cmake/PatchedWrappers.cmake")
endif()

# A toy plug-in, made as add_amp makes an amp (the top-level CMakeLists.txt), as far as identity goes: the
# same company, manufacturer code, version, categories and CLAP features — and VST2 replacement off.
function(add_toy target product code clap_id)
    juce_add_plugin(${target}
        VERSION                     "${version}"
        PRODUCT_NAME                "${product}"
        COMPANY_NAME                "Darwin's Cat"
        BUNDLE_ID                   "com.darwinscat.multiwrap.${code}"
        PLUGIN_MANUFACTURER_CODE    Dcat
        PLUGIN_CODE                 ${code}
        FORMATS                     VST3
        VST3_CATEGORIES             "Fx" "Distortion"
        IS_SYNTH                    FALSE
        NEEDS_MIDI_INPUT            FALSE
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD     FALSE)

    clap_juce_extensions_plugin(TARGET ${target}
        CLAP_ID       "${clap_id}"
        CLAP_FEATURES audio-effect "guitar" "amplifier")

    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(${target} PRIVATE
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
endfunction()

# THE ONE BINARY. Its own code (Dcam) and CLAP id name no plug-in a host is shown: the list names those.
if(NOT MULTIWRAP_UNPATCHED)
    add_toy(DarwinsCatAmps "Darwin's Cat Amps" Dcam "com.darwinscat.amps")
    target_sources(DarwinsCatAmps PRIVATE ToyList.cpp)
    target_include_directories(DarwinsCatAmps PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(DarwinsCatAmps PUBLIC JUCE_PLUGIN_LIST_HEADER="ToyPluginList.h")
endif()

# OrbitAmp alone, as today.
add_toy(ToyOrbitAmp "OrbitAmp" Oamp "com.darwinscat.orbitamp")
target_sources(ToyOrbitAmp PRIVATE ToySingle.cpp)

# The host. It opens the CLAP with the CLAP headers the wrapper itself was built with.
juce_add_console_app(MultiwrapHost PRODUCT_NAME "MultiwrapHost" VERSION "${version}")
target_sources(MultiwrapHost PRIVATE MultiwrapHost.cpp)
target_include_directories(MultiwrapHost PRIVATE "${clap-juce-extensions_SOURCE_DIR}/clap-libs/clap/include")
target_compile_definitions(MultiwrapHost PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(MultiwrapHost PRIVATE
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_recommended_config_flags)
