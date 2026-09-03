# SPDX-License-Identifier: AGPL-3.0-or-later
# ----------------------------------------------------------------------------
# OrbitAmpVersion.h build-time stamp generator — invoked via `cmake -P` by the OrbitAmpVersionGen
# custom target on EVERY build (that target is always out-of-date on purpose), so:
#   • kBuildNumber is a FRESH 14-digit UTC timestamp each build (later build => bigger number;
#     unique across machines with no coordination), and
#   • the git describe / hash / dirty flag reflect the working tree AT BUILD TIME — never the
#     stale value a configure-time snapshot would freeze in.
# End users have no repo, so the header is baked into the binary; this script is the only place git
# is ever consulted. It must NEVER fail the build: any missing git / failing command degrades
# gracefully to a sane literal ("unknown", "0.0.0-dev").
#
# The same shape as OrbitCab's generator — it is a candidate for felitronics-appkit once a third
# product wants it; until then it stays here, per the house rule (build it here, move it later).
#
# Expected -D inputs (all optional; each has a graceful default):
#   GIT_EXECUTABLE   path to git (empty => no git available)          SRC_DIR   this repo's root
#   OUT_FILE         absolute path of the header to (re)write         ARCH      build arch label
#   OS               build OS label
#   FCORE_LOCAL      ON when the sibling felitronics-core checkout is used, OFF when the pin is fetched
#   FCORE_DIR        the sibling checkout path (only read when FCORE_LOCAL)
#   FCORE_TAG        the pinned felitronics-core tag (used when fetched, or as a local fallback)
#   APPKIT_LOCAL / APPKIT_DIR / APPKIT_TAG   the same three for felitronics-appkit
#   NAMZ_LOCAL / NAMZ_DIR / NAMZ_TAG         the same three for namz, the pack codec
# ----------------------------------------------------------------------------

# --- kBuildNumber: 14-digit UTC YYYYMMDDHHMMSS -----------------------------------------------
string(TIMESTAMP _build_number "%Y%m%d%H%M%S" UTC)

# --- kBuilder: env ORBITAMP_BUILDER (CI sets =ci) else the local username --------------------
if(DEFINED ENV{ORBITAMP_BUILDER} AND NOT "$ENV{ORBITAMP_BUILDER}" STREQUAL "")
    set(_builder "$ENV{ORBITAMP_BUILDER}")
elseif(DEFINED ENV{USER} AND NOT "$ENV{USER}" STREQUAL "")
    set(_builder "$ENV{USER}")           # POSIX
elseif(DEFINED ENV{USERNAME} AND NOT "$ENV{USERNAME}" STREQUAL "")
    set(_builder "$ENV{USERNAME}")       # Windows
else()
    set(_builder "unknown")
endif()

# --- kDescribe / kGitHash / kGitDirty / kBuildCount (build-time; graceful without git) -------
set(_describe   "0.0.0-dev")   # default when there is no git or no repo
set(_hash       "unknown")
set(_dirty      "false")
set(_buildcount "-1")          # commits since the last tag; -1 = unknown (no git / no tag)
if(GIT_EXECUTABLE AND SRC_DIR)
    # Does the repo carry ANY tag? (describe --always silently falls back to a bare hash, which
    # is indistinguishable from a real describe — so we probe for a tag explicitly.)
    execute_process(COMMAND "${GIT_EXECUTABLE}" describe --tags --abbrev=0
        WORKING_DIRECTORY "${SRC_DIR}"
        RESULT_VARIABLE _tag_rc OUTPUT_QUIET ERROR_QUIET)

    execute_process(COMMAND "${GIT_EXECUTABLE}" describe --tags --always --dirty
        WORKING_DIRECTORY "${SRC_DIR}"
        RESULT_VARIABLE _desc_rc OUTPUT_VARIABLE _desc_out
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)

    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${SRC_DIR}"
        RESULT_VARIABLE _hash_rc OUTPUT_VARIABLE _hash_out
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)

    if(_hash_rc EQUAL 0 AND NOT _hash_out STREQUAL "")
        set(_hash "${_hash_out}")
    endif()

    if(_desc_rc EQUAL 0 AND NOT _desc_out STREQUAL "")
        # dirty flag: derived from describe's own "-dirty" suffix (tracked-file changes), so it
        # stays consistent with what kDescribe shows.
        if(_desc_out MATCHES "-dirty$")
            set(_dirty "true")
        endif()
        if(_tag_rc EQUAL 0)
            set(_describe "${_desc_out}")     # real tag-based describe, e.g. v0.2.0-18-g6550266[-dirty]
            # kBuildCount = the "-N-" in the describe (commits past the tag); 0 when exactly on a tag.
            if(_desc_out MATCHES "-([0-9]+)-g[0-9a-fA-F]+")
                set(_buildcount "${CMAKE_MATCH_1}")
            else()
                set(_buildcount "0")
            endif()
        else()
            set(_describe "0.0.0-dev")         # no tags yet: describe only had a bare hash to give
        endif()
    endif()
endif()

# --- kArch / kOS: the build architecture + OS labels (passed in by the parent CMake) ---------
set(_arch "unknown")
if(ARCH)
    set(_arch "${ARCH}")
endif()
set(_os "unknown")
if(OS)
    set(_os "${OS}")
endif()

# --- the dependency rows: felitronics-core and felitronics-appkit --------------------------------
# The parent CMake knows which FetchContent path it took (sibling checkout vs pinned fetch) and says
# so. A sibling is described LIVE (it may have advanced past the pin); a fetched one is the pin
# itself. Three columns come out of it — version, where it came from, and the commit — because a
# version string only hints at the last two.
function(_orbitamp_resolve_dep is_local dir tag out_version out_commit out_state)
    set(_v "${tag}")
    set(_c "")
    set(_s "pin")

    if(is_local AND dir AND EXISTS "${dir}/.git")
        set(_s "local")
        if(GIT_EXECUTABLE)
            # --match: namz carries per-language tags (js-v1.1.0, py-v…) beside the product's own
            # vX.Y.Z, and "the nearest tag" would happily name a port's release as the codec version.
            execute_process(COMMAND "${GIT_EXECUTABLE}" describe --tags --abbrev=0 --match "v[0-9]*"
                WORKING_DIRECTORY "${dir}"
                RESULT_VARIABLE _t_rc OUTPUT_VARIABLE _t_out
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(_t_rc EQUAL 0 AND NOT _t_out STREQUAL "")
                set(_v "${_t_out}")
            endif()

            execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
                WORKING_DIRECTORY "${dir}"
                RESULT_VARIABLE _h_rc OUTPUT_VARIABLE _h_out
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(_h_rc EQUAL 0 AND NOT _h_out STREQUAL "")
                set(_c "g${_h_out}")
            endif()
        endif()
    elseif(is_local AND dir)
        set(_s "local")            # a sibling that is not a git checkout: the pin is all we can say
    endif()

    if(_v STREQUAL "")
        set(_v "unknown")
    endif()

    set(${out_version} "${_v}" PARENT_SCOPE)
    set(${out_commit}  "${_c}" PARENT_SCOPE)
    set(${out_state}   "${_s}" PARENT_SCOPE)
endfunction()

_orbitamp_resolve_dep("${FCORE_LOCAL}"  "${FCORE_DIR}"  "${FCORE_TAG}"  _core   _core_commit   _core_state)
_orbitamp_resolve_dep("${APPKIT_LOCAL}" "${APPKIT_DIR}" "${APPKIT_TAG}" _appkit _appkit_commit _appkit_state)
_orbitamp_resolve_dep("${NAMZ_LOCAL}"   "${NAMZ_DIR}"   "${NAMZ_TAG}"   _namz   _namz_commit   _namz_state)

# --- escape the collected values for C++ double-quoted string literals -----------------------
# git tags may legally carry '"' or '\' and ORBITAMP_BUILDER / USER are arbitrary env text —
# unescaped they would break the emitted header's compilation. Backslash and quote are escaped;
# tab/CR/LF collapse to a space; UTF-8 bytes above ASCII pass through untouched.
function(_orbitamp_cxx_escape var)
    set(_v "${${var}}")
    string(REPLACE "\\" "\\\\" _v "${_v}")
    string(REPLACE "\"" "\\\"" _v "${_v}")
    string(REGEX REPLACE "[\t\r\n]" " " _v "${_v}")
    set(${var} "${_v}" PARENT_SCOPE)
endfunction()

_orbitamp_cxx_escape(_describe)
_orbitamp_cxx_escape(_hash)
_orbitamp_cxx_escape(_builder)
_orbitamp_cxx_escape(_core)
_orbitamp_cxx_escape(_core_commit)
_orbitamp_cxx_escape(_core_state)
_orbitamp_cxx_escape(_appkit)
_orbitamp_cxx_escape(_appkit_commit)
_orbitamp_cxx_escape(_appkit_state)
_orbitamp_cxx_escape(_namz)
_orbitamp_cxx_escape(_namz_commit)
_orbitamp_cxx_escape(_namz_state)
_orbitamp_cxx_escape(_arch)
_orbitamp_cxx_escape(_os)

# --- emit the header ------------------------------------------------------------------------
set(_content "// SPDX-License-Identifier: AGPL-3.0-or-later
// GENERATED at build time by cmake/GenerateOrbitAmpVersion.cmake — DO NOT EDIT, DO NOT COMMIT.
// Baked into the binary so end users (who have no git repo) still get a precise build stamp.
#pragma once

namespace orbitamp::version
{
    // `git describe --tags --always --dirty`, or \"0.0.0-dev\" when the repo carries no tag yet,
    // or \"unknown\" when git is unavailable entirely.
    inline constexpr const char* kDescribe    = \"${_describe}\";

    // UTC build timestamp as YYYYMMDDHHMMSS (14 digits) — later build => bigger number.
    inline constexpr long long   kBuildNumber = ${_build_number}LL;

    // Commits since the last tag (the \"-N-\" in kDescribe); 0 when exactly on a tag, -1 if unknown.
    inline constexpr int         kBuildCount  = ${_buildcount};

    inline constexpr const char* kGitHash     = \"${_hash}\";   // short HEAD hash (or \"unknown\")
    inline constexpr bool        kGitDirty    = ${_dirty};       // uncommitted tracked changes present
    inline constexpr const char* kBuilder     = \"${_builder}\"; // env ORBITAMP_BUILDER, else username
    // The dependency rows behind the version badge: the tag each library resolved to, whether it
    // came from a sibling checkout or the pin, and the commit when there is a checkout to ask.
    inline constexpr const char* kCoreVersion   = \"${_core}\";
    inline constexpr const char* kCoreCommit    = \"${_core_commit}\";
    inline constexpr const char* kCoreState     = \"${_core_state}\";
    inline constexpr const char* kAppkitVersion = \"${_appkit}\";
    inline constexpr const char* kAppkitCommit  = \"${_appkit_commit}\";
    inline constexpr const char* kAppkitState   = \"${_appkit_state}\";
    inline constexpr const char* kNamzVersion   = \"${_namz}\";
    inline constexpr const char* kNamzCommit    = \"${_namz_commit}\";
    inline constexpr const char* kNamzState     = \"${_namz_state}\";
    inline constexpr const char* kArch        = \"${_arch}\";    // build arch (arm64 / x86_64 / Universal)
    inline constexpr const char* kOS          = \"${_os}\";      // build OS   (macOS / Windows / Linux)
}
")

# Write only when the content actually changed, to avoid a needless recompile when two resolutions
# land in the same UTC second (the build number is then identical).
if(EXISTS "${OUT_FILE}")
    file(READ "${OUT_FILE}" _existing)
    if(_existing STREQUAL "${_content}")
        return()
    endif()
endif()
file(WRITE "${OUT_FILE}" "${_content}")
