# Stick Arcade

**Stick Arcade** is a collection of pixel-art mini-games for the M5Stack StickS3. Games share one firmware, launcher, settings system, and an ESP-NOW link for two-device play. No Wi-Fi router or internet connection is required during play.

[简体中文](README.zh-CN.md) · [Architecture](docs/architecture.md) · [Add a game](docs/adding-a-game.md)

## Games

- **Throwhalla** — aim by tilting the device, charge and throw weapons across a lava arena, and use powerups.
- **Big Bump** — a portrait sumo game where players steer inflatable fighters, use bursts, and collect items.

Both games support solo play and two StickS3 devices. On the launcher, B selects a game and A opens it. On the waiting screen, B starts solo; A returns to the launcher. Game controls are shown in each game. Shake the device twice quickly to open the exit prompt. On-screen text is currently English.

## Hardware

- M5Stack StickS3 with a USB-C data cable.
- For multiplayer, use two StickS3 devices with the same firmware/game protocol version.
- A Windows PC is the documented development setup. PlatformIO, the ESP32 Arduino platform, and M5Unified are used to build the firmware.

## Build and upload

Install [PlatformIO Core](https://platformio.org/install/cli) and Python. From PowerShell in this directory:

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install platformio
.\.venv\Scripts\pio.exe run -e m5stack-sticks3
```

Connect the StickS3 and upload (replace `COM9` with its current port):

```powershell
.\.venv\Scripts\pio.exe device list
.\.venv\Scripts\pio.exe run -e m5stack-sticks3 -t upload --upload-port COM9
```

The first build downloads the ESP32 toolchain, Arduino framework, and M5Unified. If upload cannot connect, put the StickS3 into its USB bootloader using its side reset button, then retry. You can also build one game at a time with `dev-throwhalla` or `dev-bumperduel`.

## Project structure

`src/core` contains the hardware-independent game contract, input types, wire encoding, settings, and display/input support. `src/net` pairs devices and transports host-authoritative snapshots over ESP-NOW. `src/ui` implements the launcher, settings, and play flow. Each folder in `src/games` contains a game's rules and view. See [the architecture guide](docs/architecture.md) for component responsibilities and data flow.

## Add a game

Implement the `arcade::IGame` contract, keep simulation and serialization testable without hardware, add a view, register the game, and add host-side rule/network tests. The [game development guide](docs/adding-a-game.md) explains the steps, protocol identifiers, controls, and validation workflow.

## Tests

Host tests require a C++17 compiler (`g++` or Visual Studio C++ on Windows):

```powershell
python test_host/run_tests.py
```

Build all firmware games with `pio run -e m5stack-sticks3`. Hardware serial test scripts under `test_host/` are development tools and require two connected devices where applicable.

## Version and compatibility

Firmware identifies itself as version 1.0. Online devices must use compatible firmware, game IDs, and game protocol versions; update both devices together when in doubt.

## License

This project is licensed under the [MIT License](LICENSE).
