# Building PkgXtractor and shadPS4 (Windows)

These are the current valid Windows build instructions for both `PkgXtractor` and `shadps4`.

Main docs:
- Project overview and general setup: [shadPS4-Windows_0.12.0/README.md](shadPS4-Windows_0.12.0/README.md)
- Contribution/build workflow details: [shadPS4-Windows_0.12.0/CONTRIBUTING.md](shadPS4-Windows_0.12.0/CONTRIBUTING.md)
- CMake preset definitions: [CMakePresets.json](CMakePresets.json) and [CMakeWindowsPresets.json](CMakeWindowsPresets.json)

## Prerequisites

- CMake 3.20+
- Visual Studio 2022 (or Build Tools) with C++ workload
- For Qt builds: Qt 6 (example: `C:\Qt\6.10.2\msvc2022_64`)

## 1) Build PkgXtractor (CLI)

```powershell
cd c:\_repositories\PkgXtractor
cmake -S . -B build\x64-Release-CLI -G "Visual Studio 17 2022" -A x64 -DENABLE_QT_GUI=OFF
cmake --build build\x64-Release-CLI --config Release --target PkgXtractor --parallel 8
```

Output:
- `build\x64-Release-CLI\Release\pkgxtractor.exe`

## 2) Build PkgXtractor (Qt GUI)

```powershell
cd c:\_repositories\PkgXtractor
cmake -S . -B build\x64-Release-Qt -G "Visual Studio 17 2022" -A x64 -DENABLE_QT_GUI=ON -DCMAKE_PREFIX_PATH="C:/Qt/6.10.2/msvc2022_64"
cmake --build build\x64-Release-Qt --config Release --target PkgXtractor --parallel 8
```

Output:
- `build\x64-Release-Qt\Release\pkgxtractor-qt.exe`

Optional Qt runtime deployment:

```powershell
& "C:\Qt\6.10.2\msvc2022_64\bin\windeployqt.exe" --release --compiler-runtime "build\x64-Release-Qt\Release\pkgxtractor-qt.exe"
```

## 3) Build shadps4 (CLI)

```powershell
cd c:\_repositories\PkgXtractor
cmake -S . -B build\x64-Release-CLI -G "Visual Studio 17 2022" -A x64 -DENABLE_QT_GUI=OFF
cmake --build build\x64-Release-CLI --config Release --target shadps4 --parallel 8
```

Output:
- `build\x64-Release-CLI\Release\shadps4-cli.exe`

## 4) Build shadps4 (Qt GUI)

```powershell
cd c:\_repositories\PkgXtractor
cmake -S . -B build\x64-Release-Qt -G "Visual Studio 17 2022" -A x64 -DENABLE_QT_GUI=ON -DCMAKE_PREFIX_PATH="C:/Qt/6.10.2/msvc2022_64"
cmake --build build\x64-Release-Qt --config Release --target shadps4 --parallel 8
```

```powershell
cd c:\_repositories\PkgXtractor
cmake --build build\x64-Release-Qt --config Release --target PkgXtractor --parallel 8
```

```powershell
Copy-Item "build\x64-Release-Qt\Release\pkgxtractor-qt.exe" -Destination "D:\EMULATORS\SHADPS4_0.12.0\pkgxtractor-qt.exe" -Force
```

Output:
- `build\x64-Release-Qt\Release\shadps4-qt.exe`

Optional Qt runtime deployment:

```powershell
& "C:\Qt\6.10.2\msvc2022_64\bin\windeployqt.exe" --release --compiler-runtime "build\x64-Release-Qt\Release\shadps4-qt.exe"
```

## Notes

- `ENABLE_QT_GUI=OFF` builds CLI variants (`pkgxtractor.exe`, `shadps4-cli.exe`).
- `ENABLE_QT_GUI=ON` builds Qt variants (`pkgxtractor-qt.exe`, `shadps4-qt.exe`).
- Use separate build directories for CLI and Qt to avoid cache/generator conflicts.
