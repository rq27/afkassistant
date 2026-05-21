# afkassistant

**AFK auto-response plugin for SA-MP** — Automatically detects and replies to AFK verification codes sent by SA-MP on certain servers.

Built as an `.asi` file injected into the `gta_sa.exe` process via an ASI Loader.

## Features

- Monitors SA-MP chat in real-time by hooking `AddMessage`
- Detects AFK codes in the format `afk 3digits` using regex
- Automatically replies `/afk <code>` with a random delay (1–15 seconds) to appear natural
- Hotkey **`Numpad *`** — manually scans the last 19 messages from chat memory and sends a reply
- Hotkey **`-`** (minus/numpad) — toggles the plugin on/off
- Supports SA-MP versions **0.3.7-R1**, **R2**, **R3**, **R4**, **R5**, and **0.3.DL**

## Usage

1. Make sure SA-MP is installed along with an ASI Loader (e.g. [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) or [Silent's ASI Loader](https://github.com/GTAmodding/ASI-Loader))
2. Copy `aiassistant.asi` to your GTA San Andreas installation folder
3. Launch GTA:SA normally — the plugin will activate automatically

## Build Requirements

| Component           | Version                                                      |
| ------------------- | ------------------------------------------------------------ |
| CMake               | ≥ 3.20                                                       |
| MSVC (Build Tools)  | 14.44.35207 (VS 2022 Build Tools)                            |
| Windows SDK         | 10.0.19041.0                                                 |
| C Standard          | C17                                                          |
| C++ Standard        | C++17                                                        |
| Target Architecture | **x86 (32-bit)** — required since GTA:SA is a 32-bit process |

> **Important:** This project must be compiled as **x86 (32-bit)**. Using a 64-bit generator will cause the plugin to fail loading in GTA:SA.

## Building

### 1. Prerequisites

Install [Visual Studio 2022 Build Tools](https://visualstudio.microsoft.com/visual-cpp-build-tools/) with the following components:

- Workload: **Desktop development with C++**
- Individual component: **C++ CMake tools for Windows**
- Windows SDK version **10.0.19041.0** (recommended for compability with Windows 10 and older) or higher

### 2. Clone the repository

```sh
git clone --recurse-submodules https://github.com/username/afkassistant.git
cd afkassistant
```

> **Do not omit `--recurse-submodules`.** Without it, the `vendor/samp-api/` directory will be empty and the build will fail.

### 3. Generate build files (x86)

Open **Developer Command Prompt for VS 2022**, then run:

```sh
# Create Artifact build
cmake -S . -B build -A Win32

# Or alternative if using many VS Build Tools
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
```

### 4. Compile

```sh
cmake --build build --config Release
```

Output will be located at:

```
build/Release/aiassistant.asi
```

## Project Structure

```
afkassistant/
├── include/
│   └── MinHook.h            # MinHook public header
├── src/
│   ├── main.cpp             # Main plugin logic
│   └── minhook/             # MinHook source (manually copied)
│       ├── buffer.c/h
│       ├── hook.c
│       ├── trampoline.c/h
│       └── hde/             # Hde32/Hde64 disassembler
├── vendor/
│   └── samp-api/            # Git submodule — BlastHackNet/SAMP-API
├── CMakeLists.txt
└── LICENSE
```

## Dependencies

| Project                                              | Author       | License      | Inclusion                                     |
| ---------------------------------------------------- | ------------ | ------------ | --------------------------------------------- |
| [MinHook](https://github.com/TsudaKageyu/minhook)    | TsudaKageyu  | BSD 2-Clause | Manually copied to `src/minhook/` (no `.git`) |
| [SAMP-API](https://github.com/BlastHackNet/SAMP-API) | BlastHackNet | MIT          | Git submodule at `vendor/samp-api/`           |

## License

Copyright (C) 2026 Raziq Revano Ramadani

This project is licensed under the **GNU General Public License v3.0** — see the [LICENSE](LICENSE) file for full details.

In short: you are free to use, modify, and redistribute this plugin, **but any distribution (including modified versions) must include the source code and use the same license (GPLv3)**. It may not be made closed-source or claimed under another community or company's name.
