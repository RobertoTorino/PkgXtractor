cmake_minimum_required(VERSION 3.20)
project(PkgXtractor-Qt CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Widgets Core)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Core PKG-related source files from shadPS4
file(GLOB PKGXTRACTOR_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/core/file_format/pkg.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/core/file_format/psf.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/core/pkgdb/*.cpp"
)

file(GLOB PKGXTRACTOR_COMMON_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/common/string_util.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/common/path_util.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/common/error.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/common/assert.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/common/logging/backend.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/common/logging/filter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/common/logging/text_formatter.cpp"
)

file(GLOB PKGXTRACTOR_LOADER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/core/loader/elf.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/core/loader.cpp"
)

# Qt GUI PkgXtractor executable using the stable gui.cpp
add_executable(PkgXtractor-qt
    "${CMAKE_CURRENT_SOURCE_DIR}/../pkgxtractor/gui.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources.qrc"
    ${PKGXTRACTOR_SOURCES}
    ${PKGXTRACTOR_COMMON_SOURCES}
    ${PKGXTRACTOR_LOADER_SOURCES}
)

# Find cryptopp and zlib (minimal dependencies)
find_package(cryptopp CONFIG QUIET)
if (NOT cryptopp_FOUND)
    message(WARNING "cryptopp library configuration not found - standalone build may fail. Try building the main shadPS4 project first.")
endif()

find_package(ZLIB QUIET)
if (NOT ZLIB_FOUND)
    message(WARNING "ZLIB not found - standalone build may fail.")
endif()

find_package(fmt CONFIG QUIET)
find_package(toml11 CONFIG QUIET)

# Link libraries (with graceful fallback for standalone)
target_link_libraries(PkgXtractor-qt PRIVATE
    Qt6::Widgets
    Qt6::Core
)

if(TARGET cryptopp::cryptopp)
    target_link_libraries(PkgXtractor-qt PRIVATE cryptopp::cryptopp)
endif()

if(ZLIB_FOUND)
    target_link_libraries(PkgXtractor-qt PRIVATE ZLIB::ZLIB)
endif()

if(TARGET fmt::fmt)
    target_link_libraries(PkgXtractor-qt PRIVATE fmt::fmt)
endif()

if(TARGET toml11::toml11)
    target_link_libraries(PkgXtractor-qt PRIVATE toml11::toml11)
endif()

# Include directories
target_include_directories(PkgXtractor-qt PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src"
    "${CMAKE_CURRENT_SOURCE_DIR}/../.."
)

# Compiler options
if (MSVC)
    target_compile_options(PkgXtractor-qt PRIVATE /W3)
    target_compile_definitions(PkgXtractor-qt PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(PkgXtractor-qt PRIVATE -Wall -Wextra)
endif()

message(STATUS "PkgXtractor-qt standalone GUI executable configured")
