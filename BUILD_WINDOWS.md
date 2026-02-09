# Building efgrabber on Windows

You have two main options for building on Windows: **Visual Studio (MSVC)** or **MSYS2 (MinGW/UCRT64)**.

## Option 1: MSYS2 (MinGW-w64 / UCRT64) - Recommended for GCC-like experience

This is often easier if you are used to Linux environments.

1.  **Install MSYS2**: Download and install from [msys2.org](https://www.msys2.org/).
2.  **Open the UCRT64 Terminal**.
3.  **Update the system**:
    ```bash
    pacman -Syu
    ```
    (You might need to restart the terminal and run it again).
4.  **Install Dependencies**:
    ```bash
    pacman -S mingw-w64-ucrt-x86_64-gcc \
              mingw-w64-ucrt-x86_64-cmake \
              mingw-w64-ucrt-x86_64-make \
              mingw-w64-ucrt-x86_64-curl \
              mingw-w64-ucrt-x86_64-sqlite3 \
              mingw-w64-ucrt-x86_64-qt5-base \
              mingw-w64-ucrt-x86_64-qt5-webengine
    ```
5.  **Build**:
    ```bash
    git clone https://github.com/segin/efgrabber.git
    cd efgrabber
    mkdir build && cd build
    cmake .. -G "MinGW Makefiles"
    mingw32-make -j$(nproc)
    ```
6.  **Run**:
    ```bash
    ./efgrabber.exe
    ```

## Option 2: Visual Studio 2022 (MSVC)

1.  **Prerequisites**:
    *   Visual Studio 2022 with "Desktop development with C++".
    *   **CMake**: Included in VS or [cmake.org](https://cmake.org/download/).
    *   **Qt 5.15 or Qt 6**: Install via [Qt Online Installer](https://www.qt.io/download-qt-installer). Select MSVC 2019/2022 64-bit and **Qt WebEngine**.
    *   **vcpkg** (for curl/sqlite):
        ```powershell
        git clone https://github.com/microsoft/vcpkg
        .\vcpkg\bootstrap-vcpkg.bat
        .\vcpkg\vcpkg install curl:x64-windows sqlite3:x64-windows
        ```

2.  **Configure & Build**:
    ```powershell
    cmake .. -DCMAKE_TOOLCHAIN_FILE="C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake" -DCMAKE_PREFIX_PATH="C:/Qt/5.15.2/msvc2019_64"
    cmake --build . --config Release
    ```

3.  **Deploy Qt DLLs**:
    ```powershell
    C:/Qt/5.15.2/msvc2019_64/bin/windeployqt.exe Release/efgrabber.exe
    ```
