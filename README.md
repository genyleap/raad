## Raad ⚡️

<u><small><i>Pronunciation: /rɑːd/ — Persian (رعد)</i></small></u>

**Next-Gen, Ultra-Fast, and Reliable Download Manager**

Raad is a modern, high-performance download manager built with modern **C++20** and **Qt 6**, designed for speed, reliability, and clean architecture — with future integration for **Geny token and blockchain-powered features**.

![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)
![C++](https://img.shields.io/badge/C%2B%2B%20Version-20-blue.svg)
![Build Status](https://img.shields.io/badge/Build-Passing-brightgreen.svg)

![5d3802f2-d94e-4b09-ba74-94910af8ff8c-d1](https://github.com/user-attachments/assets/1cb69065-2441-49e3-ad98-dceca633f97f)

## 🚧 Project Status
> Raad is currently **under active development**.  
> The core download engine is in progress, and the **graphical UI has not completed yet**.


## Highlights
- Multi-segment downloads with resume, retry, mirror failover, and per-segment progress
- Adaptive segment controller based on runtime/network and main resources like CPU, RAM and HDD/SSD Storage R/W speed conditions
- Queue policies: concurrency, bandwidth caps, schedules, quotas, and domain routing
- Per-task/global speed limits, proxy/auth/header/cookie/User-Agent controls, and optional insecure SSL
- Checksum verification, session persistence, import/export, and post-download actions
- Power-aware behavior, runtime telemetry, URL probe, and integrated update client

## Highlights (Not Actually Implemented Yet)

- Speed and network tester module
- $Geny token and NFTs blockchain integration
- Application store for better maintenance (To keep requirements and installed files up to date)
- Auto platform and file detector
- Torrent and magnet backend support
- Advanced remote terminal repo updater
- Support YouTube and social multimedia features

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
* `ui/` — QML UI *(work in progress)*
* `packaging/` — packaging assets and helpers

## License

MIT License — see `LICENSE`.

## Status

🚧 **Active development**
Core features are being implemented.
UI and user-facing components are planned for upcoming milestones.
