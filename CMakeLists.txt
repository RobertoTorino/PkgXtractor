# SPDX-FileCopyrightText: Copyright 2026 PkgXtractor Project
# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.24)

if (WIN32 AND NOT DEFINED CMAKE_ASM_MASM_COMPILER)
    set(CMAKE_ASM_MASM_COMPILER
        "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/ml64.exe"
        CACHE FILEPATH "MASM compiler")
endif()

if (WIN32)
    set(CMAKE_ASM_MASM_FLAGS
        "${CMAKE_ASM_MASM_FLAGS} /I\"C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/shared\""
        CACHE STRING "MASM flags" FORCE)
endif()

project(PkgXtractor LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(ENABLE_QT_GUI "Build the Qt GUI variant of PkgXtractor" OFF)

if (ENABLE_QT_GUI)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)
endif()

function(pkgxtractor_git_or_default output_variable)
    set(options)
    set(oneValueArgs COMMAND DEFAULT)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "" ${ARGN})

    execute_process(
        COMMAND ${ARG_COMMAND}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE)

    if (result EQUAL 0 AND NOT output STREQUAL "")
        set(${output_variable} "${output}" PARENT_SCOPE)
    else()
        set(${output_variable} "${ARG_DEFAULT}" PARENT_SCOPE)
    endif()
endfunction()

pkgxtractor_git_or_default(GIT_REV COMMAND git rev-parse --short=12 HEAD DEFAULT unknown)
pkgxtractor_git_or_default(GIT_DESC COMMAND git describe --always --long --dirty DEFAULT unknown)
pkgxtractor_git_or_default(GIT_BRANCH COMMAND git rev-parse --abbrev-ref HEAD DEFAULT detached-head)
pkgxtractor_git_or_default(GIT_REMOTE_NAME COMMAND git remote DEFAULT origin)
pkgxtractor_git_or_default(GIT_REMOTE_URL COMMAND git config --get remote.origin.url DEFAULT unknown)

string(TIMESTAMP BUILD_DATE "%Y-%m-%d %H:%M:%S")
set(APP_VERSION "0.12.0")
set(APP_IS_RELEASE true)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/common/scm_rev.cpp.in"
               "${CMAKE_CURRENT_BINARY_DIR}/src/common/scm_rev.cpp" @ONLY)

add_subdirectory(externals)

if (ENABLE_QT_GUI)
    find_package(Qt6 REQUIRED COMPONENTS Widgets Core)
endif()

set(PKGXTRACTOR_SOURCES
    src/core/crypto/crypto.cpp
    src/core/crypto/crypto.h
    src/core/file_format/pfs.h
    src/core/file_format/pkg.cpp
    src/core/file_format/pkg.h
    src/core/file_format/pkg_type.cpp
    src/core/file_format/pkg_type.h
    src/core/file_format/playgo_chunk.cpp
    src/core/file_format/playgo_chunk.h
    src/core/file_format/psf.cpp
    src/core/file_format/psf.h
    src/core/file_format/trp.cpp
    src/core/file_format/trp.h
    src/core/libraries/playgo/playgo_types.h
    src/core/loader.cpp
    src/core/loader.h
    src/core/loader/dwarf.cpp
    src/core/loader/dwarf.h
    src/core/loader/elf.cpp
    src/core/loader/elf.h)

set(PKGXTRACTOR_COMMON_SOURCES
    src/common/alignment.h
    src/common/arch.h
    src/common/assert.cpp
    src/common/assert.h
    src/common/bounded_threadsafe_queue.h
    src/common/concepts.h
    src/common/config.cpp
    src/common/config.h
    src/common/debug.h
    src/common/endian.h
    src/common/error.cpp
    src/common/error.h
    src/common/io_file.cpp
    src/common/io_file.h
    src/common/logging/backend.cpp
    src/common/logging/backend.h
    src/common/logging/filter.cpp
    src/common/logging/filter.h
    src/common/logging/formatter.h
    src/common/logging/log.h
    src/common/logging/log_entry.h
    src/common/logging/text_formatter.cpp
    src/common/logging/text_formatter.h
    src/common/logging/types.h
    src/common/ntapi.cpp
    src/common/ntapi.h
    src/common/path_util.cpp
    src/common/path_util.h
    src/common/polyfill_thread.h
    src/common/recursive_lock.cpp
    src/common/recursive_lock.h
    ${CMAKE_CURRENT_BINARY_DIR}/src/common/scm_rev.cpp
    src/common/scm_rev.h
    src/common/scope_exit.h
    src/common/spin_lock.cpp
    src/common/spin_lock.h
    src/common/string_util.cpp
    src/common/string_util.h
    src/common/thread.cpp
    src/common/thread.h
    src/common/types.h
    src/common/uint128.h)

if (ENABLE_QT_GUI)
    qt_add_executable(PkgXtractor
        tools/pkgxtractor/gui.cpp
        tools/pkgxtractor_qt/resources.qrc
        ${PKGXTRACTOR_SOURCES}
        ${PKGXTRACTOR_COMMON_SOURCES})
    set_target_properties(PkgXtractor PROPERTIES
        OUTPUT_NAME pkgxtractor-qt
        WIN32_EXECUTABLE TRUE)
    target_compile_definitions(PkgXtractor PRIVATE ENABLE_QT_GUI)
else()
    add_executable(PkgXtractor
        tools/pkgxtractor/main.cpp
        ${PKGXTRACTOR_SOURCES}
        ${PKGXTRACTOR_COMMON_SOURCES})
    set_target_properties(PkgXtractor PROPERTIES OUTPUT_NAME pkgxtractor-cli)
endif()

target_include_directories(PkgXtractor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(PkgXtractor PRIVATE
    cryptopp::cryptopp
    ZLIB::ZLIB
    fmt::fmt
    toml11::toml11
    Tracy::TracyClient)

if (WIN32)
    target_link_libraries(PkgXtractor PRIVATE bcrypt)
endif()

if (ENABLE_QT_GUI)
    target_link_libraries(PkgXtractor PRIVATE Qt6::Widgets Qt6::Core)
endif()

if (WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/pkgxtractor.rc")
    target_sources(PkgXtractor PRIVATE src/pkgxtractor.rc)
endif()

if (MSVC)
    target_compile_options(PkgXtractor PRIVATE /W3)
    target_compile_definitions(PkgXtractor PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(PkgXtractor PRIVATE -Wall -Wextra)
endif()

if (ENABLE_QT_GUI)
    set(PKGXTRACTOR_VARIANT "Qt GUI (pkgxtractor-qt)")
else()
    set(PKGXTRACTOR_VARIANT "CLI (pkgxtractor-cli)")
endif()

add_custom_command(TARGET PkgXtractor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "PkgXtractor Build Information" > "$<TARGET_FILE_DIR:PkgXtractor>/VERSION.txt"
    COMMAND ${CMAKE_COMMAND} -E echo "================================" >> "$<TARGET_FILE_DIR:PkgXtractor>/VERSION.txt"
    COMMAND ${CMAKE_COMMAND} -E echo "Build Date: $<TARGET_PROPERTY:PkgXtractor,PKGXTRACTOR_BUILD_TIME>" >> "$<TARGET_FILE_DIR:PkgXtractor>/VERSION.txt"
    COMMAND ${CMAKE_COMMAND} -E echo "Build Type: $<CONFIG>" >> "$<TARGET_FILE_DIR:PkgXtractor>/VERSION.txt"
    COMMAND ${CMAKE_COMMAND} -E echo "Variant: ${PKGXTRACTOR_VARIANT}" >> "$<TARGET_FILE_DIR:PkgXtractor>/VERSION.txt"
    COMMAND ${CMAKE_COMMAND} -E echo "================================" >> "$<TARGET_FILE_DIR:PkgXtractor>/VERSION.txt"
    COMMENT "Generating VERSION.txt for PkgXtractor")

string(TIMESTAMP PKGXTRACTOR_BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S")
set_target_properties(PkgXtractor PROPERTIES PKGXTRACTOR_BUILD_TIME "${PKGXTRACTOR_BUILD_TIMESTAMP}")

install(TARGETS PkgXtractor RUNTIME DESTINATION .)

if (ENABLE_QT_GUI)
    message(STATUS "PkgXtractor Qt GUI executable configured (pkgxtractor-qt)")
else()
    message(STATUS "PkgXtractor CLI executable configured (pkgxtractor-cli)")
endif()
