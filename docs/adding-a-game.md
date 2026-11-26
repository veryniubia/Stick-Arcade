# Adding a game

Each game lives in `src/games/<name>/`. Keep rules and network state independent of M5 hardware so they can be built and tested on a PC. Follow `src/games/throwhalla/` or `src/games/bumperduel/` as examples.

## 1. Implement the game contract

Create a class derived from `arcade::IGame` (`src/core/game_iface.h`). Implement:

- `begin(mode, seed)` to initialize a new match. Modes are `Solo`, `Host`, and `Client`.
- `step(dt, in[2])` for one fixed 1/60 second simulation step. Solo and host simulate; client state comes from snapshots. `in[0]` is the host/blue player and `in[1]` is the client/red player.
- `prime(in)` if needed to prevent button presses held during the countdown from counting as gameplay presses.
- `phase()` returning `Playing`, `RoundEnd`, or `MatchEnd` for platform-level countdown/rematch flow.
- `encode(writer)` and `decode(reader)` for the complete client-visible state. Decode transactionally: on truncated or invalid input, return false and leave the game state unchanged.
- `draw(local, ctx)` and `sfx(events)` for presentation.

Use `PadInput` press counters (`aCount` and `bCount`) for edge-triggered actions; held booleans represent held controls. `prime()` synchronizes the counters at match start. Player 1 has no human input in Solo mode. Optional `localInput()` is for immediate local presentation; it must not alter authoritative state.

A game that needs the Unit Joystick2 sets `GameInfo::needsJoystick`. The launcher then shows it in the separate joystick group, refuses to open it while the unit is missing, and the platform fills `PadInput::stickX/stickY` and, instead of the tilt, `aim` (view yaw) and `pitch` (view pitch, limited to `pad::kLookYawLimit`/`kLookPitchLimit`). If the unit disappears during play the platform shows an error, leaves an online match and returns to the launcher. Set `GameInfo::gyroScope` for games that use lobby gyro calibration and B to scope/recenter. Stick-only games leave this flag clear. List such games after all others in `src/games/registry.cpp`. This input changes the session's input packet, so both devices need the same firmware.

## 2. Add a view and launcher artwork

Keep hardware drawing and sound in `<name>_view.cpp` and `view.h`. Draw through the platform canvas; don't access M5 hardware directly. Implement the game draw, event sound, 120×78 launcher card, and waiting-screen lobby background. Keep the launcher name short (up to 10 characters; English and digits only) and on-screen text concise. Artwork can be embedded from PNG using `tools/png2c.py` (16-color palette, optional RLE).

## 3. Describe and register the game

Define a `const arcade::GameInfo` with a unique `wireId`, a `protoVer`, `instanceSize`, `arenaBytes`, factory, `maxPlayers`, and orientation. `maxPlayers` is currently advertised metadata; the platform session pairs two devices. Raise `protoVer` whenever the wire state format changes incompatibly. Add the game to `src/games/registry.cpp` in the desired launcher order.

## 4. Keep controls consistent

The platform uses A as the primary action and B as the secondary action. Avoid assigning both buttons to exit; the platform owns the double-shake exit gesture and confirmation. Settings and waiting-screen prompts also use A and B, so document game-specific controls in the view/UI and README.

## 5. Write host-side tests

Add a C++ test in `test_host/` for gameplay rules, important edge cases, and deterministic outcomes. Verify state encode/decode round trips and that invalid/truncated data is rejected without mutation. Add the new source file to the build list in `test_host/run_tests.py`. Extend the generic fake-network fixture in `test_host/net_test.cpp` so the game is exercised through pairing, snapshots, and rematch/link behavior where applicable.

Run:

```powershell
python test_host/run_tests.py
pio run -e m5stack-sticks3
```

## 6. Add a fast build environment

Add a `dev-<name>` environment in `platformio.ini` based on `m5stack-sticks3`, define `ONLY_GAME_<NAME>`, and exclude other game folders with `build_src_filter`. Add matching guards in `src/games/registry.cpp`. Build with `pio run -e dev-<name>`.

## 7. Validate on hardware

Before release, check the game on real StickS3 devices in solo and (if supported) two-device play. Verify orientation, A/B behavior, exit handling, reconnect/rematch behavior, frame rate, and memory. Use a debug build only for development; do not distribute debug firmware as the normal release image. Update the bilingual README and architecture guide when the platform contract or user-facing behavior changes.
