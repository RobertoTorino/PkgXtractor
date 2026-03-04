# PkgXtractor Qt - Standalone Deployment

## Overview
This is a **fully standalone** version of PkgXtractor Qt that includes all required Qt6 dependencies.

## Quick Start
Simply run `pkgxtractor-qt.exe` from this folder. No installation required.

## Contents
- **pkgxtractor-qt.exe** - Main application executable
- **Qt6 DLLs** - All required Qt6 libraries (Core, Gui, Widgets, Network, Svg)
- **Support DLLs** - Graphics, networking, and system dependencies
- **Plugin folders** - Qt plugins for platforms, styles, image formats, etc.
- **Translations** - Qt translations (optional, for multi-language support)
- **compare_extractions.py** - Comparison script (for Compare Extractions feature)

## Features
✅ Extract PS4 PKG files to folder structure
✅ Preview SFO metadata and icon0.png  
✅ Compare extraction results with SHA-256 hashing
✅ Salvage mode for corrupted PKGs (off by default)
✅ Fullscreen image viewer for pic0.png (click icon preview)

## System Requirements
- Windows 10/11 (64-bit)
- No additional installations needed - everything is included

## Troubleshooting
If the app doesn't run:
1. Keep all DLLs in the same folder as the executable
2. Don't rename or move plugin subdirectories
3. Ensure you have write permissions to the folder

## Notes
- Size: ~50 MB (includes all Qt6 libraries and plugins)
- No need for Qt installation or PATH configuration
- Completely portable - can be moved to any Windows machine with no dependencies

---
Built with PkgXtractor (2026-03-04)
