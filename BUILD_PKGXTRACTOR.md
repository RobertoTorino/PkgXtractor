# Building PkgXtractor

PkgXtractor is a PS4 PKG file extraction utility built as part of the shadPS4 project.

## Quick Start

```powershell
# From the root project directory
cd c:\_repositories\PkgXtractor

# Create build directory
mkdir build -Force
cd build

# Configure
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build . --config Release --parallel 4
```

## Find the Executable

After building, `PkgXtractor.exe` will be in:
- `build\Release\` (typical)
- `build\x64-Release\` (Visual Studio)

## Usage

```powershell
# Extract PKG file
.\PkgXtractor.exe game.pkg

# Extract to specific location
.\PkgXtractor.exe game.pkg "C:\Games"

# Or drag & drop a .pkg onto the exe
```

## Prerequisites

- CMake 3.20+
- Visual Studio 2022 or Build Tools
- ~15GB disk space for first build

## Notes

- First build takes 10-20 minutes
- All dependencies built automatically
- Executable builds alongside shadps4 emulator main binary
