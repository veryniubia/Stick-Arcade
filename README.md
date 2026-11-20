# Stick Duel (StickS3 PlatformIO project)

Pixel throwing duel for the M5Stack StickS3 (Arduino framework, M5Unified).

## Controls

- Menu: **B** moves the selection, **A** confirms. VERSUS plays against another StickS3 over ESP-NOW (see "Online versus").
- In game: tilt the device (held in landscape) to aim. The tilt is multiplied by 3: 0 deg aims forward, 30 deg aims straight up, 60 deg aims straight backward (the viking turns around); tilting down 10 deg aims 30 deg downward. Hold **A** to charge (the power bar bounces back at max) and release to throw; **B** jumps, and holding it longer jumps higher.
- Hold **A + B** for about 0.8 seconds to go back to the menu. Training shows a reminder in the bottom bar, and every game shows it at the top for the first 5 seconds. On the match-end screen press **A**.

## Rules

Items: knife, axe, bomb, boomerang. Everything bounces off the walls, ceiling and the small floating round obstacles (3 bounces max). Anything that falls into the lava disappears. A bomb never explodes on contact: it bounces off players, its fuse starts at its first bounce, and it also explodes when it reaches the lava; the blast grows slowly, so it can be jumped over. Players stand on platforms that move up and down. The next item is shown only to you.

Versus rounds:

- Each player throws exactly one item per round (your own bomb, boomerang or bounced knife can hurt you). The round ends when both have thrown and nothing is flying any more; then both get a fresh random item and the next round starts. Nobody can throw twice, so a round waits for a player who has not thrown yet.
- A player who is hit gives the opponent 1 point; each player can be hit once per round, so a round gives at most 1 point to each side. Being hit does not stop you from throwing your item.
- First to 5 points wins. At 4:4 it is sudden death: the first damage decides the match immediately, damage that happens afterwards does not count. If both are hit in the very same instant, the round is scored normally (5:5) and play continues.
- The loser of the match tips over and stays lying on the platform.

## Online versus (ESP-NOW, two devices)

Both devices run the same firmware. No router and no MAC address entry.

1. On both devices pick **VERSUS**. Each shows SEARCHING and broadcasts a hello on Wi-Fi channel 1 (`net_espnow::kChannel`).
2. The device with the larger random nonce becomes the host and offers a session to the other one, which accepts. Pairing is first come, first served: once two devices are locked to each other they ignore everyone else. If a third device is searching nearby it stays in SEARCHING.
3. READY, then a 3-2-1 countdown, then the match. The host is the blue viking on the left, the client the red one on the right. The score shows "mine, opponent's" in each player's colour. Only your own next item, aim line and power bar are drawn. The ping is shown before and during the first seconds of the match.
4. First to 5 wins. On the result screen press **A** for a rematch; it starts once both players pressed A (OPP READY tells you the other side is waiting). Hold **A + B** at any time to leave; the other device shows OPP LEFT and returns to the menu.
5. If packets stop for 0.6 s the game pauses with LINK LOST and resumes by itself when the link comes back. After 5 s both devices return to the menu (TIMEOUT).

How it works:

- The host runs the real `Game` at 60 Hz. The client sends its aim, A/B state and a jump counter (so a lost packet cannot swallow a jump) about every 15 ms, and the host sends a state snapshot (players, projectiles, scores, clock, sound events of the last two snapshots; typically 50-75 bytes, at most about 175 with 8 projectiles in flight). Snapshots and inputs are state based, so a lost packet is simply replaced by the next one. The client never simulates; it copies the snapshot into its own `Game` and draws it with `render::drawGame(game, 1, ...)`.
- The seed in every snapshot lets the client rebuild the obstacle layout and platform phases with `Game::begin`; the clock keeps the platforms in step.
- The snapshot is the full game state, so the host's private values (next item, aim, power) are in the packet and a modified client could read them. The screen just does not draw them.
- Session logic (`src/net_session.*`) has no hardware dependency; `src/net_espnow.*` is the radio glue and `src/versus.*` the screens. The radio is only on while the VERSUS screen is open.

### Tests

PC, no hardware: a fake link with loss, latency, jitter and reordering drives two sessions (pairing, three devices, state sync, rematch, jump and fire over a lossy link, link loss, timeout, leaving, garbage packets):

```
g++ -std=c++17 -I src test_host/net_test.cpp src/net_session.cpp src/game.cpp -o test_host/net_test.exe
.\test_host\net_test.exe
```

Two real devices (debug build on both, for example `pio run -t upload --upload-port COM8`, then COM9, with `$env:PLATFORMIO_BUILD_FLAGS="-DDEBUG_REMOTE"`):

```
python test_host/net_check.py COM8 COM9 v w9 "*:l" p150:phase=2 "@end" "0:a" "1:a" w6
```

That pairs, plays with 20% of received packets dropped until a match ends, takes screenshots and starts a rematch. A debug build prints, once per second on every screen, a `PERF` line (microseconds per frame for input / logic / draw / display push, FreeRTOS idle passes per core, free heap, free PSRAM, free loop-task stack) and, while VERSUS is open, a `RADIO` line (packets and bytes per second, ACK ok/failed) and a `NET role=1 st=PLAY rtt=.. score=..` line (role 1 = host, 2 = client). Other serial keys in a debug build: `v` open VERSUS, `l`/`k`/`L` drop 20%/0%/100% of received packets, `a` press A, `x` hold A+B. Opening a serial port resets the board, so run one scenario per script run.

Measured on two devices (release firmware: 1.03 MB flash = 31.6% of the 3.19 MB app slot, 57 KB static RAM): menu 46 fps, training 48 fps, versus 48 fps, with about 20 ms per frame of which 14.7 ms is the blocking 240x135 display push and about 6 ms is real work. Free internal heap is 310 KB in the menu, 298 KB in training (sound buffers) and 260 KB while VERSUS is open; the Wi-Fi driver keeps about 28 KB after the first VERSUS session and does not grow with later ones. PSRAM: 8.3 MB free. Radio: 48 packets/s each way, host to client 2.5-3.5 KB/s, client to host 0.7 KB/s, no failed ACKs at close range, ping 20-30 ms (one frame of it is the loop cadence). Wi-Fi tasks use about 5% of core 0; the main loop runs on core 1 and never sleeps.

Checklist for a new pair of devices: pairing from both menus in either order; READY and countdown; fire, jump and bomb from both sides; ping around 20-40 ms; walk apart or `L` for LINK LOST and recovery; hold A+B on one side; finish a match and rematch.

## Development

Build with `-DDEBUG_REMOTE` (for example `$env:PLATFORMIO_BUILD_FLAGS="-DDEBUG_REMOTE"`) to enable a serial remote for screenshots: `python test_host/shot.py COM9 out g:0.5 "@name"`. The release build does not include it.

## Tests

Game rules are hardware-free and can be checked on the PC:

```
g++ -std=c++17 -I src test_host/game_test.cpp src/game.cpp -o test_host/game_test.exe
.\test_host\game_test.exe
```

# StickS3 PlatformIO starter notes

This project targets the M5Stack StickS3 with the Arduino framework.

## Build and upload

1. Open this folder in VS Code and wait for PlatformIO to initialize its project environment.
2. Connect the StickS3 with a USB-C data cable.
3. Build with the PlatformIO toolbar or run `pio run` in the PlatformIO terminal.
4. To upload, hold the StickS3 side reset button for about 2 seconds. Release it when the internal green LED flashes, then run `pio run --target upload`.
5. Open the serial monitor at 115200 baud with `pio device monitor`.

The first build downloads the ESP32 toolchain, Arduino framework, and M5Unified library. No fixed serial port is configured, so PlatformIO can detect the connected board. If upload cannot find the board, check Windows Device Manager for its COM port and add `upload_port = COMx` under the environment in `platformio.ini`.
