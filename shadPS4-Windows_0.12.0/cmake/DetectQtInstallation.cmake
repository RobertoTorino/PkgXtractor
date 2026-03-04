# SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

set(QT_CANDIDATE_ROOTS
    "C:/Qt"
    "D:/dev/qt"
)

set(QT_MSVC_KITS)
set(QT_MINGW_KITS)

foreach(QT_ROOT IN LISTS QT_CANDIDATE_ROOTS)
    if(EXISTS "${QT_ROOT}")
        file(GLOB ROOT_MSVC_KITS LIST_DIRECTORIES true "${QT_ROOT}/*/msvc*_64")
        file(GLOB ROOT_MINGW_KITS LIST_DIRECTORIES true "${QT_ROOT}/*/mingw_*")
        list(APPEND QT_MSVC_KITS ${ROOT_MSVC_KITS})
        list(APPEND QT_MINGW_KITS ${ROOT_MINGW_KITS})
    endif()
endforeach()

list(SORT QT_MSVC_KITS COMPARE NATURAL)
list(REVERSE QT_MSVC_KITS)

if(QT_MSVC_KITS)
    list(GET QT_MSVC_KITS 0 QT_PREFIX)
    set(CMAKE_PREFIX_PATH "${QT_PREFIX}" CACHE PATH "Qt prefix auto-detected" FORCE)
    message(STATUS "Auto-detected Qt MSVC kit: ${QT_PREFIX}")
else()
    if(QT_MINGW_KITS)
        list(SORT QT_MINGW_KITS COMPARE NATURAL)
        list(REVERSE QT_MINGW_KITS)
        list(GET QT_MINGW_KITS 0 QT_MINGW_PREFIX)
        message(FATAL_ERROR
            "Qt was found, but only MinGW kits are installed (example: ${QT_MINGW_PREFIX}).\n"
            "This project is being built with MSVC/clang-cl and requires an MSVC Qt kit (for example: C:/Qt/<version>/msvc2022_64).\n"
            "Install the MSVC Qt kit in Qt Maintenance Tool, then reconfigure.\n"
            "Alternatively set CMAKE_PREFIX_PATH or Qt6_DIR to that MSVC kit.")
    else()
        message(FATAL_ERROR
            "No Qt MSVC kit was found under C:/Qt or D:/dev/qt.\n"
            "Install Qt with the msvc2022_64 kit and set CMAKE_PREFIX_PATH or Qt6_DIR accordingly.")
    endif()
endif()

