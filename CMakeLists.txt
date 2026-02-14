# CMakeLists.txt - CMake build configuration for efgrabber
# Copyright © 2026 Kirn Gill II <segin2005@gmail.com>
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

cmake_minimum_required(VERSION 3.20)
project(efgrabber VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable Qt automoc/autouic/autorcc
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED libcurl)
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

# Try Qt5
find_package(Qt5 COMPONENTS Widgets Concurrent QUIET)
find_package(Qt5 COMPONENTS WebEngineWidgets QUIET)
if(Qt5_FOUND)
    set(QT_FOUND TRUE)
    set(QT_LIBRARIES Qt5::Widgets Qt5::Concurrent)
    message(STATUS "Qt5 found - GUI will be built")
    if(Qt5WebEngineWidgets_FOUND)
        set(QT_WEBENGINE_FOUND TRUE)
        list(APPEND QT_LIBRARIES Qt5::WebEngineWidgets)
        message(STATUS "Qt5 WebEngine found - browser integration enabled")
    else()
        set(QT_WEBENGINE_FOUND FALSE)
        message(STATUS "Qt5 WebEngine not found - browser integration disabled")
    endif()
else()
    set(QT_FOUND FALSE)
    set(QT_WEBENGINE_FOUND FALSE)
    message(STATUS "Qt5 not found - only CLI will be built")
    message(STATUS "  Install Qt5: sudo apt install qtbase5-dev")
endif()

option(BUILD_GUI "Build the Qt GUI application" ${QT_FOUND})

if(BUILD_GUI AND NOT QT_FOUND)
    message(WARNING "Qt5 not found, disabling GUI build")
    set(BUILD_GUI OFF)
endif()

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CURL_INCLUDE_DIRS}
    ${SQLite3_INCLUDE_DIRS}
)

# Link directories
link_directories(
    ${CURL_LIBRARY_DIRS}
)

# Source files
set(CORE_SOURCES
    src/database.cpp
    src/downloader.cpp
    src/scraper.cpp
    src/thread_pool.cpp
    src/download_manager.cpp
    src/cookie.cpp
)

# CLI-only version (no GUI dependencies) - always build
add_executable(efgrabber-cli
    src/main_cli.cpp
    ${CORE_SOURCES}
)

target_link_libraries(efgrabber-cli
    ${CURL_LIBRARIES}
    SQLite::SQLite3
    Threads::Threads
)

target_compile_options(efgrabber-cli PRIVATE
    -Wall -Wextra -Wpedantic
)

# GUI version (optional)
if(BUILD_GUI)
    set(GUI_SOURCES
        src/gui/main_window_qt.cpp
        src/gui/main_window_qt.h
        src/gui/browser_widget.cpp
        src/gui/browser_widget.h
        src/gui/scraper_pool.cpp
        src/gui/scraper_pool.h
    )

    add_executable(efgrabber
        src/main_qt.cpp
        ${CORE_SOURCES}
        ${GUI_SOURCES}
    )

    target_link_libraries(efgrabber
        ${QT_LIBRARIES}
        ${CURL_LIBRARIES}
        SQLite::SQLite3
        Threads::Threads
    )

    target_compile_options(efgrabber PRIVATE
        -Wall -Wextra -Wpedantic
    )

    if(QT_WEBENGINE_FOUND)
        target_compile_definitions(efgrabber PRIVATE HAVE_WEBENGINE=1)
    endif()

    install(TARGETS efgrabber
        RUNTIME DESTINATION bin
    )
endif()

# Install CLI target (always)
install(TARGETS efgrabber-cli
    RUNTIME DESTINATION bin
)

# Testing
enable_testing()

add_executable(test_scraper
    tests/test_scraper.cpp
    src/scraper.cpp
)

target_link_libraries(test_scraper
    ${CURL_LIBRARIES}
    SQLite::SQLite3
    Threads::Threads
)

target_include_directories(test_scraper PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

add_test(NAME ScraperTest COMMAND test_scraper)
