# ----------------------------------------------------------------------------
# JUCE AND clap-juce-extensions, PATCHED SO THAT ONE BINARY HOLDS SEVERAL PLUG-INS.
#
# Both register exactly one plug-in per binary. The family ships one VST3, one CLAP and, on the Mac, one AU
# that hold every amp, each under the ids its own bundle has always had, so a session saved with an amp's
# own bundle opens it from the family's unchanged. The two several-plugins patches (cmake/patches, each
# explained at its head) add that and nothing else: a target that defines JUCE_PLUGIN_LIST_HEADER — the
# name of a header on its include path — holds the plug-ins that header lists (juce_PluginList.h, which the
# JUCE patch brings), and every other target compiles the very text it compiled before, each added line
# sitting behind that macro. So the standalones and a plug-in built alone build as they always did, from
# the patched trees. A third, small patch lets the
# family's VST3 carry its name at all: JUCE's VST3 post-build steps hand the bundle's path to the shell
# unquoted, and the apostrophe in "Darwin's Cat Amps" breaks them wherever that shell is /bin/sh.
#
# And one bug fix for every CLAP, whether it holds a list or not: the wrapper loads a state without asking
# the host to read the parameters' values again (clap-juce-extensions-state-rescan.patch, explained at its
# head), so a host showed a session's knobs where they were before it opened.
#
# Included once the pins are set — ORBITAMP_JUCE_TAG, ORBITAMP_JUCE_DIR, ORBITAMP_CLAP_EXT_TAG — it declares
# `juce` and `clap-juce-extensions`, patched, and makes them available.
#
# THE ESCAPE HATCH COPIES. ORBITAMP_JUCE_DIR names another project's JUCE tree, and a patch never touches a
# tree this build does not own. When that tree is the pinned version it is COPIED into this build (a
# FetchContent URL may name a directory, which it copies) and the copy is patched: the clone is still saved,
# and the other project's tree stays as it was. Any other tree is passed over and the pin fetched, as
# before. A FETCHCONTENT_SOURCE_DIR_<NAME> override, by contrast, is used as it is, unpatched — FetchContent
# runs no step on a tree it is handed — so it is no way to build a plug-in list.
# ----------------------------------------------------------------------------
include_guard(GLOBAL)
include(FetchContent)

set(ORBITAMP_JUCE_PATCH "${CMAKE_CURRENT_LIST_DIR}/patches/juce-several-plugins.patch")
set(ORBITAMP_JUCE_QUOTING_PATCH "${CMAKE_CURRENT_LIST_DIR}/patches/juce-vst3-post-build-quoting.patch")
set(ORBITAMP_CLAP_EXT_PATCH "${CMAKE_CURRENT_LIST_DIR}/patches/clap-juce-extensions-several-plugins.patch")
set(ORBITAMP_CLAP_EXT_RESCAN_PATCH "${CMAKE_CURRENT_LIST_DIR}/patches/clap-juce-extensions-state-rescan.patch")
set(_orbitamp_apply_patch "${CMAKE_CURRENT_LIST_DIR}/ApplyPatch.cmake")

# The patch step: ApplyPatch.cmake, run in the dependency's source tree. The patch's hash is one of its
# arguments, so an edited patch is a changed declaration and the step runs again.
function(_orbitamp_patch_step out patch)
    file(SHA256 "${patch}" hash)
    set(${out} "${CMAKE_COMMAND}" "-DPATCH=${patch}" "-DPATCH_SHA256=${hash}" -P "${_orbitamp_apply_patch}"
        PARENT_SCOPE)
endfunction()

# JUCE — the pinned tag, or the local tree when it is that version (copied, above).
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

_orbitamp_patch_step(_orbitamp_juce_patch "${ORBITAMP_JUCE_PATCH}")
_orbitamp_patch_step(_orbitamp_juce_quoting_patch "${ORBITAMP_JUCE_QUOTING_PATCH}")
if(_orbitamp_juce_have AND _orbitamp_juce_have STREQUAL _orbitamp_juce_want)
    message(STATUS "JUCE: a patched copy of the local source — ${ORBITAMP_JUCE_DIR}")
    FetchContent_Declare(juce
        URL            "${ORBITAMP_JUCE_DIR}"
        PATCH_COMMAND  ${_orbitamp_juce_patch} COMMAND ${_orbitamp_juce_quoting_patch})
else()
    message(STATUS "JUCE: fetching ${ORBITAMP_JUCE_TAG} from GitHub, patched")
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${ORBITAMP_JUCE_TAG}
        GIT_SHALLOW    TRUE
        PATCH_COMMAND  ${_orbitamp_juce_patch} COMMAND ${_orbitamp_juce_quoting_patch})
endif()
FetchContent_MakeAvailable(juce)

# clap-juce-extensions — the pinned commit, patched.
_orbitamp_patch_step(_orbitamp_clap_ext_patch "${ORBITAMP_CLAP_EXT_PATCH}")
_orbitamp_patch_step(_orbitamp_clap_ext_rescan_patch "${ORBITAMP_CLAP_EXT_RESCAN_PATCH}")
FetchContent_Declare(clap-juce-extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG        ${ORBITAMP_CLAP_EXT_TAG}
    PATCH_COMMAND  ${_orbitamp_clap_ext_patch} COMMAND ${_orbitamp_clap_ext_rescan_patch})
FetchContent_MakeAvailable(clap-juce-extensions)
