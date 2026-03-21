# SPDX-FileCopyrightText: Copyright 2026 PkgXtractor Project
# SPDX-License-Identifier: GPL-2.0-or-later

set(BUILD_SHARED_LIBS OFF)
set(BUILD_TESTING OFF)
set_directory_properties(PROPERTIES
    EXCLUDE_FROM_ALL ON
    SYSTEM ON)

if (MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE -D_SCL_SECURE_NO_WARNINGS)
endif()

if (NOT TARGET fmt::fmt)
    set(FMT_TEST OFF CACHE BOOL "" FORCE)
    set(FMT_DOC OFF CACHE BOOL "" FORCE)
    add_subdirectory(fmt)
endif()

if (NOT TARGET cryptopp::cryptopp)
    set(CRYPTOPP_INSTALL OFF)
    set(CRYPTOPP_BUILD_TESTING OFF)
    set(CRYPTOPP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/cryptopp)
    set(CRYPTOPP_DISABLE_AESNI ON)
    set(CRYPTOPP_DISABLE_AVX2 ON)
    add_subdirectory(cryptopp-cmake)
    file(COPY cryptopp DESTINATION cryptopp FILES_MATCHING PATTERN "*.h")
    set_target_properties(cryptopp PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_BINARY_DIR}/cryptopp")
endif()

if (NOT TARGET ZLIB::ZLIB)
    set(ZLIB_ENABLE_TESTS OFF)
    set(WITH_GTEST OFF)
    set(WITH_NEW_STRATEGIES ON)
    set(WITH_NATIVE_INSTRUCTIONS ON)
    set(ZLIB_COMPAT ON CACHE BOOL "" FORCE)
    include(FetchContent)
    FetchContent_Declare(
        ZLIB
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/zlib-ng"
        OVERRIDE_FIND_PACKAGE)
    FetchContent_MakeAvailable(ZLIB)
    add_library(ZLIB::ZLIB ALIAS zlib)
endif()

if (NOT TARGET toml11::toml11)
    add_subdirectory(toml11)

    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        get_target_property(_toml11_compile_options toml11 INTERFACE_COMPILE_OPTIONS)
        if (_toml11_compile_options)
            list(REMOVE_ITEM _toml11_compile_options "/Zc:preprocessor")
            set_target_properties(toml11 PROPERTIES INTERFACE_COMPILE_OPTIONS "${_toml11_compile_options}")
        endif()
    endif()
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Release")
    option(TRACY_ENABLE "" OFF)
else()
    option(TRACY_ENABLE "" ON)
endif()
option(TRACY_NO_CRASH_HANDLER "" ON)
option(TRACY_ON_DEMAND "" ON)
option(TRACY_NO_FRAME_IMAGE "" ON)
option(TRACY_FIBERS "" OFF)
option(TRACY_NO_SYSTEM_TRACING "" ON)
option(TRACY_NO_CALLSTACK "" ON)
option(TRACY_NO_CODE_TRANSFER "" ON)
option(TRACY_NO_SAMPLING "" ON)
option(TRACY_ONLY_LOCALHOST "" ON)
option(TRACY_NO_CONTEXT_SWITCH "" ON)
add_subdirectory(tracy)
