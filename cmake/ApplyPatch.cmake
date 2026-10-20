# ----------------------------------------------------------------------------
# One patch onto the dependency tree this runs in — a FetchContent PATCH_COMMAND (PatchedWrappers.cmake):
#
#     cmake -DPATCH=<file> -P ApplyPatch.cmake          (working directory: the dependency's source tree)
#
# ONCE, AND ALWAYS ONTO THE PRISTINE TREE. A copy of what was applied is kept in the tree beside it, so a
# step that runs again — FetchContent re-populates whenever a declared detail changes, the patch's hash
# among them — finds the tree already patched and leaves it, or patched with an older copy and reverses that
# first. `git apply` only writes the working files, inside a clone or outside any repository alike: nothing
# here commits, resets or checks anything out.
# ----------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.22)

if(NOT PATCH OR NOT EXISTS "${PATCH}")
    message(FATAL_ERROR "ApplyPatch: no patch file given (-DPATCH=<file>)")
endif()

find_program(ORBITAMP_GIT git REQUIRED)
get_filename_component(name "${PATCH}" NAME)
set(applied "${CMAKE_CURRENT_SOURCE_DIR}/.orbitamp-applied-${name}")

if(EXISTS "${applied}")
    file(SHA256 "${applied}" was)
    file(SHA256 "${PATCH}" is)
    if(was STREQUAL is)
        message(STATUS "ApplyPatch: ${name} is already applied")
        return()
    endif()

    execute_process(COMMAND "${ORBITAMP_GIT}" apply --reverse --whitespace=nowarn "${applied}"
                    RESULT_VARIABLE failed ERROR_VARIABLE error)
    if(failed)
        message(FATAL_ERROR "ApplyPatch: the tree no longer takes back its older ${name}:\n${error}"
                            "Delete this dependency's source directory and configure again.")
    endif()
    file(REMOVE "${applied}")
endif()

execute_process(COMMAND "${ORBITAMP_GIT}" apply --whitespace=nowarn "${PATCH}"
                RESULT_VARIABLE failed ERROR_VARIABLE error)
if(failed)
    message(FATAL_ERROR "ApplyPatch: ${name} does not apply to ${CMAKE_CURRENT_SOURCE_DIR}:\n${error}"
                        "Was the pin moved without the patch? A patch is written against one version.")
endif()

file(COPY_FILE "${PATCH}" "${applied}")
message(STATUS "ApplyPatch: applied ${name}")
