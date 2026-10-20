# ----------------------------------------------------------------------------
# A shipped binary's symbols kept beside the build, then the binary stripped as every release binary is — a
# POST_BUILD step of every format an amp ships in, when ORBITAMP_KEEP_SYMBOLS is on (CMakeLists.txt: A
# RELEASE KEEPS THE SYMBOLS OF WHAT IT SHIPPED):
#
#     cmake [-DCONFIG=<config>] -DBINARY=<file> -DSYMBOLS=<dir> [-DBUNDLE=<dir>] -DSTRIP=<strip>
#           -DDSYMUTIL=<dsymutil> -DDWARFDUMP=<dwarfdump> [-DCODESIGN=<codesign>]     (the Mac)
#           -DOBJCOPY=<objcopy> [-DREADELF=<readelf>]                                  (Linux)
#           -P KeepSymbols.cmake
#
# THE MAC. dsymutil makes <SYMBOLS>/<bundle>.dSYM from the binary as it was linked, every architecture of
# it, and `strip -S -x` then takes out of the binary its debug map and its local symbols — what `ld -x` left
# out at the link before. A bundle whose signature the strip broke is signed ad hoc again, as JUCE's own
# post-build step signs it; one that had none is left without. A dSYM is matched to a crash report by the
# UUID of each architecture, so the dSYM must carry the stripped binary's, every one of them: checked, and a
# mismatch stops the build. So does a dSYM that would carry no lines — a debug map with nothing in it, or an
# object file dsymutil could not open (the link-time-optimised object, kept by -object_path_lto, is the one
# that matters).
#
# LINUX. objcopy --only-keep-debug makes <SYMBOLS>/<file>.debug, `strip --strip-all` takes out what `-s` did
# at the link before, and --add-gnu-debuglink writes the debug file's name and checksum into the binary.
# Where the binary has a build ID, the debug file must carry the same one: checked.
#
# A configuration other than Release (-DCONFIG=$<CONFIG>) is left as it was linked.
# ----------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.22)

if(DEFINED CONFIG AND NOT CONFIG STREQUAL "Release")
    return()   # only a Release binary is stripped (CMakeLists.txt: strip_release), so only its symbols are kept
endif()

foreach(var BINARY SYMBOLS STRIP)
    if(NOT ${var})
        message(FATAL_ERROR "KeepSymbols: -D${var}=… is required")
    endif()
endforeach()
if(NOT EXISTS "${BINARY}")
    message(FATAL_ERROR "KeepSymbols: no binary at ${BINARY}")
endif()

function(run)   # run <command...> — runs it, and stops the build with what it said if it fails
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE failed OUTPUT_VARIABLE said ERROR_VARIABLE said)
    if(failed)
        message(FATAL_ERROR "KeepSymbols: `${ARGN}` failed:\n${said}")
    endif()
    set(said "${said}" PARENT_SCOPE)
endfunction()

file(MAKE_DIRECTORY "${SYMBOLS}")

if(DSYMUTIL)
    foreach(var DWARFDUMP)
        if(NOT ${var})
            message(FATAL_ERROR "KeepSymbols: -D${var}=… is required on the Mac")
        endif()
    endforeach()
    if(BUNDLE)
        get_filename_component(name "${BUNDLE}" NAME)
    else()
        get_filename_component(name "${BINARY}" NAME)
    endif()
    set(dsym "${SYMBOLS}/${name}.dSYM")
    file(REMOVE_RECURSE "${dsym}")   # a relink's dSYM is made whole, never merged into the last one's

    run("${DSYMUTIL}" "${BINARY}" -o "${dsym}")
    if(said MATCHES "no debug symbols in executable|unable to open object file")
        message(FATAL_ERROR "KeepSymbols: the dSYM of ${name} would carry no lines:\n${said}")
    elseif(said)
        message(STATUS "KeepSymbols: dsymutil on ${name}:\n${said}")
    endif()

    # Sealed before the strip (JUCE signs a plug-in's bundle ad hoc after its link), sealed after it; a bundle
    # that was not (an app, whose binary only the linker signed, which the strip keeps) is left as it was.
    set(sealed "${BINARY}")
    if(BUNDLE)
        set(sealed "${BUNDLE}")
    endif()
    if(NOT CODESIGN)
        find_program(CODESIGN codesign)
    endif()
    set(was_sealed FALSE)
    if(CODESIGN)
        execute_process(COMMAND "${CODESIGN}" --verify "${sealed}" RESULT_VARIABLE unsealed
                        OUTPUT_QUIET ERROR_QUIET)
        if(NOT unsealed)
            set(was_sealed TRUE)
        endif()
    endif()

    run("${STRIP}" -S -x "${BINARY}")

    if(was_sealed)
        execute_process(COMMAND "${CODESIGN}" --verify "${sealed}" RESULT_VARIABLE unsealed
                        OUTPUT_QUIET ERROR_QUIET)
        if(unsealed)
            run("${CODESIGN}" --force --sign - "${sealed}")
        endif()
    endif()

    # The UUID of every architecture, the binary's and the dSYM's: "UUID: <uuid> (<arch>)".
    set(uuid_regex "UUID: [0-9A-Fa-f-]+ \\([A-Za-z0-9_]+\\)")
    run("${DWARFDUMP}" --uuid "${BINARY}")
    string(REGEX MATCHALL "${uuid_regex}" shipped "${said}")
    run("${DWARFDUMP}" --uuid "${dsym}")
    string(REGEX MATCHALL "${uuid_regex}" kept "${said}")
    list(SORT shipped)
    list(SORT kept)
    if(NOT shipped OR NOT shipped STREQUAL kept)
        message(FATAL_ERROR "KeepSymbols: the dSYM of ${name} does not match the binary it was made from:\n"
                            "  binary: ${shipped}\n  dSYM:   ${kept}")
    endif()
    message(STATUS "KeepSymbols: ${dsym} — ${kept}")

elseif(OBJCOPY)
    get_filename_component(name "${BINARY}" NAME)
    set(debug "${SYMBOLS}/${name}.debug")
    file(REMOVE "${debug}")

    run("${OBJCOPY}" --only-keep-debug "${BINARY}" "${debug}")
    run("${STRIP}" --strip-all "${BINARY}")
    run("${OBJCOPY}" "--add-gnu-debuglink=${debug}" "${BINARY}")

    if(READELF)
        set(id_regex "Build ID: [0-9a-f]+")
        run("${READELF}" -n "${BINARY}")
        string(REGEX MATCH "${id_regex}" shipped "${said}")
        run("${READELF}" -n "${debug}")
        string(REGEX MATCH "${id_regex}" kept "${said}")
        if(NOT shipped STREQUAL kept)
            message(FATAL_ERROR "KeepSymbols: the debug file of ${name} does not match the binary:\n"
                                "  binary: ${shipped}\n  debug:  ${kept}")
        endif()
    endif()
    message(STATUS "KeepSymbols: ${debug} ${kept}")

else()
    message(FATAL_ERROR "KeepSymbols: say -DDSYMUTIL=… (the Mac) or -DOBJCOPY=… (Linux)")
endif()
