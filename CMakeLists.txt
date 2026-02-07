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
find_package(Qt5 COMPONENTS Widgets QUIET)
if(Qt5_FOUND)
    set(QT_FOUND TRUE)
    set(QT_LIBRARIES Qt5::Widgets)
    message(STATUS "Qt5 found - GUI will be built")
else()
    set(QT_FOUND FALSE)
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

    install(TARGETS efgrabber
        RUNTIME DESTINATION bin
    )
endif()

# Install CLI target (always)
install(TARGETS efgrabber-cli
    RUNTIME DESTINATION bin
)
