## Raad ⚡️

<u><small><i>Pronunciation: /rɑːd/ — Persian (رعد)</i></small></u>

**Next-Gen, Ultra-Fast, and Reliable Download Manager**

Raad is a modern, high-performance download manager built with modern **C++20** and **Qt 6**, designed for speed, reliability, and clean architecture — with future integration for **Geny token and blockchain-powered features**.


![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)
![C++ Version](https://img.shields.io/badge/C%2B%2B-C%2B%2B20-blue.svg)
![Build Status](https://img.shields.io/badge/Build-Passing-brightgreen.svg)


## 🚧 Project Status
> Raad is currently **under active development**.  
> The core download engine is in progress, and the **graphical UI has not been implemented yet**.


## Highlights
- High-performance multi-segment downloads with resume support  
- File integrity verification (hash-based)  
- Clean and modular C++20 codebase  
- Integrated with Geny token & blockchain for future features
- Designed for a modern QML-based UI *(coming soon)*


## Tech Stack
- C++20 / CMake
- Qt 6 (Core, Network, Concurrent, Quick, QuickControls2)
- QML UI layer *(planned)*


## Requirements
- CMake 3.28+
- Qt 6.8+ *(tested with 6.10)*
- A C++20-capable compiler
- For C++20 modules:
  - Clang + `clang-scan-deps` (LLVM toolchain)

## Build (All Platforms)
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
````


## Modules and LLVM

Raad uses **C++20 modules** by default.
If your compiler does not fully support modules or `clang-scan-deps`, disable them:

```bash
cmake -S . -B build -DRAAD_USE_MODULES=OFF
```

## Qt Discovery

If CMake cannot find Qt, set one of the following and reconfigure:

```bash
# Preferred
- DQt6_DIR=/path/to/Qt/6.x.x/<platform>/lib/cmake/Qt6

# Or
- DCMAKE_PREFIX_PATH=/path/to/Qt/6.x.x/<platform>
```

## Run

```bash
./build/appraad
```

## Project Layout

* `src/` — C++ core, download engine, and services
* `qml/` — QML UI *(work in progress)*
* `packaging/` — packaging assets and helpers

## License

MIT License — see `LICENSE`.

## Status

🚧 **Active development**
Core features are being implemented.
UI and user-facing components are planned for upcoming milestones.
