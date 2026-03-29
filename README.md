[![CI Build and Release](https://github.com/RobertoTorino/PkgXtractor/actions/workflows/release.yml/badge.svg)](https://github.com/RobertoTorino/PkgXtractor/actions/workflows/release.yml) [![Nightly / Beta Build](https://github.com/RobertoTorino/PkgXtractor/actions/workflows/nightly.yml/badge.svg)](https://github.com/RobertoTorino/PkgXtractor/actions/workflows/nightly.yml)

# pkgxtractor
## Based on a Fork of shadPS4 0.12.0

This repository contains the source code for `PkgXtractor`, a PKG extraction tool. It has been streamlined to focus solely on the components necessary for building `PkgXtractor`, removing any unrelated files and dependencies.

![icon](resources/pkgxtractor-icon.png)

---

## What remains

- `src/common`: shared runtime and logging utilities used by the extractor
- `src/core/crypto`: PKG decryption helpers
- `src/core/file_format`: PKG, PSF, TRP, and PlayGo parsing
- `src/core/loader`: file type detection and ELF helpers used by the tool
- `src/core/libraries/playgo`: PlayGo type definitions
- `tools/pkgxtractor`: CLI entrypoint and Qt GUI implementation
- `tools/pkgxtractor_qt/resources.qrc`: Qt resource file
- `externals/cryptopp`, `externals/cryptopp-cmake`, `externals/fmt`, `externals/toml11`, `externals/tracy`, `externals/zlib-ng`

Everything related only to `shadPS4`, duplicated release snapshots, and generated build output is removed.

## Windows build only


### CLI Build (no Qt required)


```powershell
cmake --preset x64-Clang-Release
cmake --build build/x64-Clang-Release --target PkgXtractor --parallel 8
```

Output:
- `build/x64-Clang-Release/pkgxtractor-cli.exe`

### Qt GUI Build

You must enable the Qt GUI option and provide your Qt installation path. You can do this by setting the environment variable or passing it as a CMake argument. Example:

```powershell
# Option 1: Set CMAKE_PREFIX_PATH as an environment variable
$env:CMAKE_PREFIX_PATH = "C:/Qt/6.10.2/msvc2022_64/lib/cmake/Qt6"
cmake --preset x64-Clang-Release-Qt -DENABLE_QT_GUI=ON

# Option 2: Pass as a CMake argument
cmake --preset x64-Clang-Release-Qt -DENABLE_QT_GUI=ON -DCMAKE_PREFIX_PATH="C:/Qt/6.10.2/msvc2022_64/lib/cmake/Qt6"

cmake --build build/x64-Clang-Release-Qt --target PkgXtractor --parallel 8
```


Output:
- `build/x64-Clang-Release-Qt/pkgxtractor-qt.exe`


Optional runtime deployment:
---

## Automated Version Bumping (Release Workflow)

When you merge to the main branch, the release workflow will automatically bump the version in `CMakeLists.txt` based on your latest commit message:

- **Major version bump**: If your commit message contains `BREAKING CHANGE` or `!` (exclamation mark after the type), the major version is incremented and minor/patch are reset to 0.
- **Minor version bump**: If your commit message contains `feat:`, the minor version is incremented and patch is reset to 0.
- **Patch version bump**: For all other commit messages, only the patch version is incremented.

### Examples

**Major bump:**
```
refactor!: change PKG extraction logic

BREAKING CHANGE: The extraction API has changed.
```

**Minor bump:**
```
feat: add support for new PKG metadata field
```

**Patch bump:**
```
fix: correct typo in log output
```

The new version is committed and pushed automatically after each successful release build on main.

```powershell
& "C:/Qt/6.10.2/msvc2022_64/bin/windeployqt.exe" --release --compiler-runtime "build/x64-Clang-Release-Qt/pkgxtractor-qt.exe"
```

