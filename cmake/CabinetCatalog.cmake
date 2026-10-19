# THE CABINET CATALOG, read (resources/cabinets.txt says what is in it and how a line is written). One
# function, orbitamp_cabinet_catalog(<catalog> <resources dir> <header>): it writes the table the code
# plays the cabinets by into <header>, and hands back the files to build into the binaries, in the order
# of their numbers —
#
#   ORBITAMP_CABINETS_EVERY_PRODUCT   the folder every product carries (OrbitAmpData)
#   ORBITAMP_CABINETS_ORBITAMP        the folders OrbitAmp alone carries (OrbitAmpShelfOtherData)
#
# — and, beside each, the same files as paths inside the factory folder, IRs/<folder>/<file>, in the same
# order: ORBITAMP_CABINETS_EVERY_PRODUCT_AT and ORBITAMP_CABINETS_ORBITAMP_AT (THE FACTORY FOLDER).
#
# A catalog that breaks one of its rules stops the configure with the line that broke it, rather than
# building a shelf whose numbers would mean other cabinets than a session saved them on.

include_guard(GLOBAL)

# `text` as a C++ string literal. Every byte past ASCII — the catalog's `·` — and every quote, backslash
# and control byte is written as an \x escape, so the header is plain ASCII and every compiler reads the
# same bytes into the string, whatever it takes a source file's encoding to be. An escape runs on through
# every hex digit after it, so where one follows, the literal is closed and opened again.
function(_orbitamp_cabinet_literal out text)
    string(HEX "${text}" hex)
    string(LENGTH "${hex}" length)
    set(literal "")
    set(escaped FALSE)
    set(at 0)
    while(at LESS length)
        string(SUBSTRING "${hex}" ${at} 2 byte)
        math(EXPR code "0x${byte}")
        if(code GREATER_EQUAL 128 OR code LESS 32 OR code EQUAL 34 OR code EQUAL 92)
            string(APPEND literal "\\x${byte}")
            set(escaped TRUE)
        else()
            string(ASCII ${code} char)
            if(escaped AND char MATCHES "[0-9A-Fa-f]")
                string(APPEND literal "\" \"")
            endif()
            string(APPEND literal "${char}")
            set(escaped FALSE)
        endif()
        math(EXPR at "${at} + 2")
    endwhile()
    set(${out} "\"${literal}\"" PARENT_SCOPE)
endfunction()

function(orbitamp_cabinet_catalog catalog resources header)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${catalog}")
    file(STRINGS "${catalog}" lines ENCODING UTF-8)

    set(folders "")          # the folders, in the catalog's order
    set(ours "")             # ...and the one every product carries
    set(cabinets 0)
    set(irs 0)
    set(names "")
    set(files "")
    set(default "")

    foreach(line IN LISTS lines)
        if(line MATCHES "^[ \t]*(#|$)")
            continue()
        endif()
        string(REPLACE "|" ";" fields "${line}")
        list(TRANSFORM fields STRIP)
        list(POP_FRONT fields kind)
        list(LENGTH fields count)

        if(kind STREQUAL "folder" AND count EQUAL 2)
            list(GET fields 0 name)
            list(GET fields 1 carrier)
            if(name IN_LIST folders)
                message(FATAL_ERROR "${catalog}: the folder `${name}` is said twice")
            endif()
            if(carrier STREQUAL "every product")
                if(NOT ours STREQUAL "")
                    message(FATAL_ERROR "${catalog}: one folder is carried by every product — `${ours}`, and now `${name}` too")
                endif()
                set(ours "${name}")
            elseif(NOT carrier STREQUAL "OrbitAmp")
                message(FATAL_ERROR "${catalog}: a folder is carried by `every product` or by `OrbitAmp` — `${line}`")
            endif()
            list(APPEND folders "${name}")

        elseif(kind STREQUAL "cabinet" AND count EQUAL 2)
            list(GET fields 0 folder)
            list(GET fields 1 about)
            if(NOT folder IN_LIST folders)
                message(FATAL_ERROR "${catalog}: a cabinet in a folder no `folder` line has said — `${line}`")
            endif()
            set(cabinet ${cabinets})
            math(EXPR cabinets "${cabinets} + 1")
            set(_cabinet_${cabinet}_folder "${folder}")
            set(_cabinet_${cabinet}_about "${about}")
            set(_cabinet_${cabinet}_count 0)

        elseif(kind STREQUAL "ir" AND count EQUAL 3)
            list(GET fields 0 number)
            list(GET fields 1 name)
            list(GET fields 2 path)
            if(cabinets EQUAL 0)
                message(FATAL_ERROR "${catalog}: an IR before any cabinet — `${line}`")
            endif()
            if(NOT number MATCHES "^[0-9]+$")
                message(FATAL_ERROR "${catalog}: an IR's number is a whole number — `${line}`")
            endif()
            math(EXPR number "${number}")   # 07 is 7, the same IR
            if(DEFINED _ir_${number}_name)
                message(FATAL_ERROR "${catalog}: the number ${number} is given to `${_ir_${number}_name}` and to `${name}`")
            endif()
            if(name IN_LIST names)
                message(FATAL_ERROR "${catalog}: two IRs are named `${name}`")
            endif()
            get_filename_component(file "${path}" NAME)
            if(file IN_LIST files)
                message(FATAL_ERROR "${catalog}: two IRs are laid out as `${file}`")
            endif()
            if(NOT EXISTS "${resources}/${path}")
                message(FATAL_ERROR "${catalog}: `${name}` is resources/${path}, which is not there")
            endif()

            # The menu shows a cabinet as one run of numbers (cabShelves: its first and how many), in
            # the order the catalog has them.
            if(_cabinet_${cabinet}_count EQUAL 0)
                set(_cabinet_${cabinet}_first ${number})
            else()
                math(EXPR next "${_cabinet_${cabinet}_first} + ${_cabinet_${cabinet}_count}")
                if(NOT number EQUAL next)
                    message(FATAL_ERROR "${catalog}: a cabinet's IRs are one run of numbers, and `${name}` is ${number} where ${next} is next — an IR joins the cabinet that holds the last number, or comes with a cabinet of its own")
                endif()
            endif()
            math(EXPR _cabinet_${cabinet}_count "${_cabinet_${cabinet}_count} + 1")

            set(_ir_${number}_folder "${_cabinet_${cabinet}_folder}")
            set(_ir_${number}_name "${name}")
            set(_ir_${number}_file "${file}")
            set(_ir_${number}_path "${resources}/${path}")
            list(APPEND names "${name}")
            list(APPEND files "${file}")
            math(EXPR irs "${irs} + 1")

        elseif(kind STREQUAL "default" AND count EQUAL 1)
            list(GET fields 0 default)

        else()
            message(FATAL_ERROR "${catalog}: a line that is none of folder, cabinet, ir, default — `${line}`")
        endif()
    endforeach()

    if(irs EQUAL 0 OR ours STREQUAL "")
        message(FATAL_ERROR "${catalog}: a catalog holds IRs, and one folder that every product carries")
    endif()
    math(EXPR lastCabinet "${cabinets} - 1")
    foreach(cabinet RANGE ${lastCabinet})
        if(_cabinet_${cabinet}_count EQUAL 0)
            message(FATAL_ERROR "${catalog}: the cabinet `${_cabinet_${cabinet}_about}` holds no IR")
        endif()
    endforeach()

    # The table, in the order of the numbers — which run from 0 with none left out — and the files with it.
    set(every "")
    set(orbitamp "")
    set(everyAt "")
    set(orbitampAt "")
    set(table "")
    set(opens "")
    math(EXPR last "${irs} - 1")
    foreach(number RANGE ${last})
        if(NOT DEFINED _ir_${number}_name)
            message(FATAL_ERROR "${catalog}: the numbers run from 0 with none left out, and ${number} is not there")
        endif()
        set(laid "IRs/${_ir_${number}_folder}/${_ir_${number}_file}")
        if(_ir_${number}_folder STREQUAL ours)
            list(APPEND every "${_ir_${number}_path}")
            list(APPEND everyAt "${laid}")
        else()
            list(APPEND orbitamp "${_ir_${number}_path}")
            list(APPEND orbitampAt "${laid}")
        endif()
        if(_ir_${number}_name STREQUAL default)
            set(opens ${number})
        endif()
        _orbitamp_cabinet_literal(folder "${_ir_${number}_folder}")
        _orbitamp_cabinet_literal(name "${_ir_${number}_name}")
        _orbitamp_cabinet_literal(file "${_ir_${number}_file}")
        set(at "${number}")
        if(number LESS 10)
            set(at " ${number}")
        endif()
        string(APPEND table "    /* ${at} */ { ${folder}, ${name}, ${file} },\n")
    endforeach()
    if(opens STREQUAL "")
        message(FATAL_ERROR "${catalog}: the default is the name of an IR in it, and `${default}` is none")
    endif()

    set(shown "")
    foreach(cabinet RANGE ${lastCabinet})
        _orbitamp_cabinet_literal(about "${_cabinet_${cabinet}_about}")
        string(APPEND shown "    { ${about}, ${_cabinet_${cabinet}_first}, ${_cabinet_${cabinet}_count} },\n")
    endforeach()

    _orbitamp_cabinet_literal(everyProduct "${ours}")
    _orbitamp_cabinet_literal(defaultName "${default}")
    file(RELATIVE_PATH from "${CMAKE_CURRENT_SOURCE_DIR}" "${catalog}")
    set(text
"// Written by cmake/CabinetCatalog.cmake from ${from} — THE CABINET CATALOG, which says
// what each of these is and why. Change the catalog, never this: the next configure writes it again.
#pragma once

namespace orbitamp::catalog
{
/** An IR of the shelf: the folder it lies in, the name the player reads, and its file's name. */
struct Ir
{
    const char* folder;
    const char* name;
    const char* file;
};

/** A cabinet as the IR menu shows it: the line that heads its IRs (UTF-8), and the run of numbers they hold. */
struct Cabinet
{
    const char* about;
    int first, count;
};

/** The folder every product carries; the others are OrbitAmp's alone. */
inline constexpr const char* ours = ${everyProduct};

/** Every IR, at its number. */
inline constexpr Ir irs[]
{
${table}};

/** The number a fresh instance opens on: ${defaultName}. */
inline constexpr int defaultIr = ${opens};

/** The cabinets, in the order the menu shows them. */
inline constexpr Cabinet cabinets[]
{
${shown}};
} // namespace orbitamp::catalog
")

    # Written only when it changes, so a configure that changes nothing here rebuilds nothing.
    set(before "")
    if(EXISTS "${header}")
        file(READ "${header}" before)
    endif()
    if(NOT before STREQUAL text)
        file(WRITE "${header}" "${text}")
    endif()

    list(LENGTH every everyCount)
    list(LENGTH orbitamp orbitampCount)
    message(STATUS "Cabinets: ${irs} in the catalog — ${everyCount} every product carries, ${orbitampCount} OrbitAmp's alone")

    set(ORBITAMP_CABINETS_EVERY_PRODUCT "${every}" PARENT_SCOPE)
    set(ORBITAMP_CABINETS_ORBITAMP "${orbitamp}" PARENT_SCOPE)
    set(ORBITAMP_CABINETS_EVERY_PRODUCT_AT "${everyAt}" PARENT_SCOPE)
    set(ORBITAMP_CABINETS_ORBITAMP_AT "${orbitampAt}" PARENT_SCOPE)
endfunction()
