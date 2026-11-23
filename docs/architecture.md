# Stick Arcade architecture

Stick Arcade is a single firmware image containing a launcher and multiple games. The platform owns hardware, screens, settings, and multiplayer sessions. A game owns its rules, state, serialization, and rendering.

## Components

| Path | Responsibility |
|---|---|
| `src/main.cpp` | Initializes M5Unified and platform services; runs the launcher/settings/play screen loop. |
| `src/core/` | Shared types and services: `IGame`, fixed 60 Hz tick, input pads, gyro look control (`gyro_look.h`), the Joystick2 driver (`joystick.*`), wire reader/writer, per-game memory arena, display buffers, sensors, sound, and persistent settings. |
| `src/ui/` | Launcher, settings page, and the waiting/countdown/solo/online/exit flow. |
| `src/net/net_session.*` | Hardware-independent matchmaking/session state, host authority, snapshots, rematches, and link-loss handling. |
| `src/net/net_espnow.*` | ESP-NOW and Wi-Fi radio glue. |
| `src/games/registry.*` | Game list and launcher order. |
| `src/games/<game>/` | A game's simulation and wire format, plus its view/card/lobby drawing. |
| `test_host/` | C++ rule/core/network tests and optional serial-driven hardware checks. |

## Runtime flow

1. `main.cpp` initializes the display, settings, inputs, sound, and game registry.
2. The launcher selects a `GameEntry`; the play UI creates its `IGame` instance and presents the waiting screen.
3. Solo play starts a local simulation. In online play, the session pairs two devices that advertise compatible game and protocol identifiers.
4. The host steps the game at 60 Hz and serializes its full game state. The client sends its input pad and decodes each received state snapshot; it does not run authoritative simulation.
5. The game view draws to the platform canvas. On clients, `localInput()` can update local-only presentation such as immediate tilt aiming.
6. The platform destroys the game instance on exit and returns to the launcher.

```text
StickS3 hardware
      │
 main loop ── UI (launcher / settings / play)
                    │
              Game registry
                    │
       ┌────────────┴────────────┐
       │ Game rules + state      │ Game view + sound
       │ IGame, no hardware      │ canvas, events
       └────────────┬────────────┘
                    │ encode/decode
       net_session ─┴─ net_espnow
              ESP-NOW peer
```

## Game and network contract

`arcade::IGame` in `src/core/game_iface.h` defines lifecycle (`begin`, `step`, `phase`), input priming, full-state `encode`/`decode`, optional local presentation input and idle state, drawing, and sound events. `arcade::GameInfo` supplies the game ID, unique wire ID, protocol version, instance/arena memory sizes, factory, player count, and orientation.

The session layer is game-agnostic. HELLO pairing checks game wire ID and protocol version before matching. The host is authoritative; clients send `PadInput` and receive snapshots. Packets are state-based so a later snapshot replaces a lost one. Keep game state encoding compact: the packet limit is 250 bytes, including the session envelope. Increase a game's `protoVer` whenever its encoded state format changes incompatibly. Both boards need compatible firmware to play together.

## Build variants

`m5stack-sticks3` includes all registered games. `dev-throwhalla`, `dev-bumperduel`, `dev-samuraiduel`, and `dev-middiff` compile one game for faster iteration; `debug-all` enables serial debug controls and performance output. These are PlatformIO environments in `platformio.ini`.

MoonDuel (`samuraiduel`, wire ID 5, protocol 5, landscape) owns its platform physics, grounded sword/parry rules, horizontal bullets, round resets, and rematch weapon swaps. Its snapshots use the existing host-authoritative session. `test_host/preview_samuraiduel.py` renders the actual view on a host raster canvas for visual checks and requires Pillow and the installed M5GFX font dependency.
