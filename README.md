# Stick Arcade (StickS3 PlatformIO project)

A platform for pixel-art mini games on the M5Stack StickS3 (Arduino framework, M5Unified). Every game is compiled into
one firmware and picked from a launcher; every game plays online against a second StickS3 (ESP-NOW, no router) or solo.

Games so far: **Throwhalla** (Vikings throw knives, axes, bombs and boomerangs across a lava pit) and **Paddle**
(a small ball game that doubles as the reference implementation for new games).

## Using it

Only two keys are usable in software: **A** (main action) and **B** (secondary). The power key is not used.
All on-screen text is English and digits.

| Where | B | A |
|---|---|---|
| Launcher | next card (wraps) | open the card |
| Waiting screen (searching for an opponent) | SOLO (flashes after 10 s) | BACK to the launcher |
| In a game | game specific (Throwhalla: jump) | game specific (Throwhalla: charge and throw) |
| Match end (online) | - | AGAIN (rematch starts when both pressed it) |
| Settings | next row | change / run the row |

**Leaving a game:** shake the device twice in a row (two hard shakes within 0.8 s). `EXIT?` appears, the game keeps
running underneath and A and B are taken by the question: **A = YES**, **B = NO**; it closes by itself after 3 s.
Online, YES tells the other device (it shows OPP LEFT and returns to its launcher). The keys held at the moment the
question opens stay frozen, so a charge in progress neither fires nor stops, and presses made for the question never
reach the game. A+B is not an exit any more because jumping while charging holds both keys in normal play.

**Solo:** press B on the waiting screen. The top shows an orange bar and a `SOLO` tag, there is no PING. The radio keeps
announcing in the background; when another device with the same game shows up and you have nothing to do (between
rounds, after your throw) `OPP FOUND  A = JOIN` appears. A makes you joinable for 10 s and the normal pairing follows.
Without a radio (`RADIO OFF`) solo still works.

**Settings** (kept in NVS with a format number and checksum; a damaged or unknown record falls back to the defaults):
SOUND (off, 1-4; the top level is 75% of the speaker range, the vendor advises staying below that on battery),
BRIGHT (5 levels), AIM (tilt multiplier 2.0-4.0, default 3.0), CALIBRATE (hold the device in the pose that should
count as level for 2 s), BATTERY (percent and USB), VERSION, RESET, BACK.

## Throwhalla

Hold the device in landscape and tilt it to aim. The tilt is multiplied by the AIM setting (3.0): 0 deg aims forward,
30 deg of tilt aims straight up, 60 deg straight backward (the viking turns around); tilting down 10 deg aims 30 deg
downward. Hold **A** to charge (the power bar bounces back at max) and release to throw; **B** jumps, longer holds
jump higher.

Items: knife, axe, bomb, boomerang. Everything bounces off the walls, ceiling and the small floating round obstacles
(3 bounces max). Anything that falls into the lava disappears. A bomb never explodes on contact: it bounces off
players, its fuse starts at its first bounce, and it also explodes when it reaches the lava; the blast grows slowly, so
it can be jumped over. Players stand on platforms that move up and down. The next item is shown only to you.

Rounds (online): each player throws exactly one item per round (your own bomb, boomerang or bounced knife can hurt
you). The round ends when both have thrown and nothing is flying any more; then both get a fresh random item.
**Turn timeout:** a player who has not thrown 20 s after the round started forfeits the throw (no point lost); in the
last 5 s a bar shrinks along the top of that player's screen. A player who is hit gives the opponent 1 point; each
player can be hit once per round. First to 5 wins. At 4:4 it is sudden death: the first damage decides the match, later
damage does not count. If both are hit in the very same instant the round is scored normally (5:5) and play continues.
The loser tips over and stays lying on the platform.

Solo: the opponent stands still and counts as "already thrown". You throw once per round; the round ends when nothing is
flying, you get a new item, and every hit on the opponent scores a point. There is no winner and no end.

## Paddle

Tilt to move your paddle (+-60 degrees of aim covers the whole field), keep the ball in play, first to 5. Solo: the
right side is a wall and the score is the longest rally.

## Online play

Both devices run the same firmware, no MAC addresses to enter. Open the same game on both. Each shows SEARCHING and
broadcasts HELLO on Wi-Fi channel 1 (`net_espnow::kChannel`).

- HELLO carries the session protocol version, the game id, the game's protocol version, `maxPlayers`, a reserved room
  byte and a joinable flag. Devices with another game, version, player count or room ignore each other silently and
  never show up in the UI.
- The device with the larger random nonce becomes the host and offers a session, the other accepts. First come first
  served: once two devices are locked to each other they ignore everyone else. A third device stays in SEARCHING.
- READY, a 3-2-1 countdown, then the match. The host is the blue side on the left, the client the red one on the right.
- If packets stop for 0.6 s the game pauses with `LINK LOST` and resumes by itself; after 5 s both return to the
  launcher (`TIMEOUT`). Leaving sends a BYE (`OPP LEFT` on the other side).
- The radio is only on while a game screen is open. The Wi-Fi driver keeps about 28 KB after the first session.

How it works: the host runs the real game at a fixed 60 Hz step. The client sends its pad (aim, A/B held, A/B press
counters so a lost packet cannot swallow a press) about every 15 ms and the host answers with a full state snapshot
(a platform envelope plus the game's own block). The client never simulates, it decodes the snapshot into its own game
object and draws it; only the local player's aim follows the local sensor immediately (`IGame::localInput`). Snapshots
and inputs are state based, so a lost packet is replaced by the next one. The snapshot is the full state: a modified
client could read the host's private values, the screen just does not draw them.

## Code layout

```
src/main.cpp            setup/loop: launcher -> settings or play, frame limiter, debug remote
src/core/               platform pieces that every game uses
  platform.h            screen size, tick, rgb()
  pad.h                 PadInput: what one player's hands do (aim, A/B held, A/B press counters)
  game_iface.h          IGame, GameInfo, Arena, GameInstance: the contract between platform and game
  wire.h                packet Writer/Reader, q8/q16 quantisers
  game_instance.cpp     one memory block per running game: [object][arena]
  display.*             two canvases in internal RAM, DMA push, frame limiter
  input.*               buttons, filtered tilt, shake detector feed
  shake.h               double-shake recogniser (pure, tested on the PC)
  sound.*               single-channel note queue with priorities, volume
  settings*.h/.cpp      settings data + byte format (pure) and NVS storage
  bitmap.h              4-bit palette bitmaps, optional RLE (pure)
  debug.h               serial remote of the DEBUG_REMOTE build
src/net/                net_session (pairing, host authority, snapshots, rematch, link loss; game independent and
                        hardware independent) and net_espnow (radio glue)
src/ui/                 launcher, settings_page, play (waiting screen, solo, online screens, exit question), gfx helpers
src/games/registry.*    the list of games (launcher order)
src/games/<name>/       one folder per game
test_host/              PC tests and the serial test scripts
tools/png2c.py          PNG -> palette bitmap header
```

## Adding a game

A game is one folder `src/games/<name>/` with two halves. The rules and the network encoding must not touch hardware
(they run in the PC tests); only the `*_view.cpp` file draws and makes sound. Copy `src/games/paddle/` as the template
(about 200 lines of rules, 100 of drawing).

1. **Rules and encoding** (`<name>.h/.cpp`): a class derived from `arcade::IGame`.
   - `begin(mode, seed)`: start a match. `Mode::Solo` has no second player, `Host` simulates, `Client` only views.
   - `step(dt, PadInput in[2])`: one fixed 1/60 s step, returns event bits (only bits 0-7 reach the client).
     `in[0]` is the host (blue, left), `in[1]` the client. Use the press counters (`aCount`/`bCount`) for "pressed
     since the last step" and `prime()` to ignore presses made before the match started. Player 1 does not exist in Solo.
   - `phase()`: `Playing`, `RoundEnd` or `MatchEnd`; the platform draws READY/countdown/AGAIN around it.
   - `encode(Writer&)` / `decode(Reader&)`: the full game state for the client; `decode` must return false and change
     nothing for a short or invalid snapshot. Keep it under about 175 bytes (the packet limit is 250).
   - `localInput`, `idle`: optional. `idle(local)` is true while the player has nothing to do; the platform then may use
     A for `OPP FOUND  A = JOIN`.
   - A `const arcade::GameInfo` with a **new unique `wireId`**, `protoVer` (bump it whenever encode/decode change),
     `instanceSize`, `arenaBytes` (extra memory from the game's own arena, 0 if none), `create`, `maxPlayers`.
2. **View** (`<name>_view.cpp`, `view.h`): `draw(local, ctx)` into `display::canvas()` (240x135, no hardware calls),
   `sfx(events)` through `sound::play(notes, n, priority)`, `drawCard(x, y, w, h, ms)` for the 120x78 animated launcher
   card (set a clip rect, draw relative to x/y) and `drawLobby(ms)` for the full-screen background of the waiting
   screen. Cards may also be a bitmap: `python tools/png2c.py card.png name --rle -o src/games/<name>/name_art.h`, then
   `ui::drawBitmap(assets::name, x, y)` (16 colours, about 1-3 KB per card).
3. **Register** it in `src/games/registry.cpp` (name: English and digits, at most 10 characters).
4. **Key conventions:** A is the main action (confirm, charge, fire), B the secondary one (next, jump). Never use both
   as an exit; leaving is the platform's shake gesture. Text on screen: English and digits only, as little as possible.
5. **Tests** (`test_host/`): stub `draw` and `sfx` as the existing tests do, test the rules, an encode/decode round trip
   including truncated and invalid input, and add the game to the generic network fixture: `testPaddleOverNetwork` in
   `net_test.cpp` runs any game over a lossy fake link from pairing to the match end and the rematch. Add the `.cpp`
   files to the build command lines below.
6. **Budget check** on two real devices with the debug build: `PERF` lines must stay at 60 fps with `loop` about 16.6 ms,
   `logic + draw` well under 8 ms (Throwhalla: 2.4 ms, Paddle 0.9 ms), the free heap above about 100 KB while online,
   and the flash growth small (Paddle cost 3.8 KB). A game that needs memory declares it in `arenaBytes`.
7. **Fast iteration:** add an environment next to `dev-throwhalla` in `platformio.ini` that defines
   `-DONLY_GAME_<NAME>` and removes the other game folders with `build_src_filter`, and guard the other games in
   `registry.cpp` with `#ifndef`.
8. Update this README (rules, controls if they differ, numbers).

## Build, upload, debug build

```
pio run -e m5stack-sticks3                                # all games
pio run -e m5stack-sticks3 -t upload --upload-port COM9   # upload
pio run -e dev-throwhalla                                 # only Throwhalla (reuses the libraries of the default env)
$env:PLATFORMIO_BUILD_FLAGS="-DDEBUG_REMOTE"              # debug build: serial remote, PERF/RADIO/NET lines
```

## Tests

PC, no hardware (g++ 17):

```
g++ -std=c++17 -I src test_host/game_test.cpp src/games/throwhalla/game.cpp -o test_host/game_test.exe
g++ -std=c++17 -I src test_host/core_test.cpp src/core/game_instance.cpp src/games/paddle/paddle.cpp src/games/throwhalla/throwhalla.cpp src/games/throwhalla/game.cpp -o test_host/core_test.exe
g++ -std=c++17 -I src test_host/net_test.cpp src/net/net_session.cpp src/core/game_instance.cpp src/games/throwhalla/throwhalla.cpp src/games/throwhalla/game.cpp src/games/paddle/paddle.cpp -o test_host/net_test.exe
```

- `game_test`: Throwhalla rules (physics, rounds, sudden death, solo rounds, turn timeout).
- `core_test`: shake detector (including the highest values measured in normal play), settings format, arena and game
  instances, bitmap decoder, Paddle rules and encoding.
- `net_test`: a fake link with loss, latency, jitter and reordering drives two sessions: pairing (also under 40% loss and
  with three devices), other game / version / player count ignored, joinable gate, snapshot equality with the host,
  rematch, jump and fire over a lossy link, early presses are not jumps, turn timeout seen by the client, link loss,
  timeout, leaving, forged and garbage packets, and Paddle over the same fixture.

Two real devices (debug build on both, then COM8 and COM9; Python with `pyserial` and `Pillow`; quote `@` arguments in
PowerShell):

```
python test_host/net_check.py COM8 COM9 v w9 "*:l" p240:phase=2 "@end" "0:a" "1:a" w6
python test_host/net_check.py COM8 COM9 v w9 "0:x" w0.5 "0:a" w3      # shake question, YES: the other side shows OPP LEFT
python test_host/net_check.py COM8 COM9 "0:g" "1:v" w3 "0:f" w1.5 "@opp" "0:a" w5   # solo, OPP FOUND, join
python test_host/shot.py COM9 out n:2 "@launcher" o:0.5 "@next"
```

Keys of the debug build: `v`/`V` open game 0/1 (waiting screen), `g`/`G` open and go solo, `a`/`o` press A/B,
`x` shake twice, `m` launcher, `f` charge and throw, `j` B press, `u`/`b`/`n` force aim up/backward/off,
`1`-`4`/`i` Throwhalla items, `l`/`k`/`L` drop 20% / 0% / 100% of received packets, `s` screenshot. Once per second it
prints `PERF` (microseconds per frame: input, logic, draw, push, wait, loop; idle passes per core; heap, PSRAM,
stack), `RADIO` (packets, bytes, ACKs) and `NET` (role, state, RTT, stage, phase). Opening a serial port resets the
board, so run one scenario per script run.

## Measured on two StickS3 (release firmware)

| | Before (single game) | Now |
|---|---|---|
| Flash | 1.03 MB, 31.6% of the 3.19 MB app slot | 1.08 MB, 32.4% (a game of Paddle's size costs about 4 KB) |
| Static RAM | 57 KB | 57.5 KB |
| Frame rate | 46-48 fps, CPU never idle | **60 fps** online and solo, launcher 30 fps, the loop sleeps about 7 ms per frame |
| Display | 14.7 ms blocking push | the push is started with DMA, about 6.5 ms in the call, the rest overlaps the next frame |
| Input | 1.9 ms | 1.1 ms (`M5.update` 0.8 ms -> 0.003 ms with `pmic_button=false`; the IMU read is 0.75 ms) |
| Free heap | menu 310 KB, online 260 KB | launcher 150-180 KB, solo 142 KB, **online 130 KB** (two 65 KB canvases) |
| PSRAM | 8.3 MB free | 8.37 MB free |
| Radio | 48 packets/s each way | 60 packets/s each way (follows the frame rate), host to client 3.2 KB/s, client to host 0.9 KB/s, no failed ACKs, ping 16-33 ms |

Per frame in Throwhalla online: input 1.13 ms, logic 0.4 ms, draw 2.0 ms. Paddle: draw 0.7 ms.

Hardware findings behind the choices:

- The canvas used to live in PSRAM, which DMA cannot read, so the push was a CPU copy. Two canvases in internal RAM
  and `pushSprite` inside a permanent `startWrite()` give a real overlap (77 fps possible with 40 MHz).
- The SPI clock only comes in 40 or 80 MHz (integer divider of 80 MHz). **80 MHz garbles the picture** on this panel, so
  40 MHz stays.
- Polling the power key over I2C cost 0.8 ms per frame; it is switched off. The power key keeps its hardware
  behaviour (single click resets, double click powers off) and the software never reads it.
- Shake gesture: the gyroscope cannot tell a shake from fast aiming (both reach about 1000 dps), the acceleration
  can. Measured on a StickS3 at 200 Hz: normal play stays below 1.13 g away from 1 g, deliberate shakes reach 5.0-6.6 g.
  The detector counts a spike above 2.5 g (a little lower than the 3 g midpoint because the game samples at 60 Hz) and
  needs the acceleration to fall below 1 g before the next spike counts; two spikes within 0.8 s open the question.

## Not done / ideas

- **The shake detector has not been tried by hand at the game's 60 Hz sampling rate.** The thresholds come from the
  200 Hz measurement. If the exit gesture is missed or fires by accident, tune `ShakeDetector::kThresholdG`
  (`src/core/shake.h`); the debug build prints `SHAKE` on the serial port when it fires.
- The last firmware (HELLO with `maxPlayers`/`room`, bitmap decoder) was built but could not be uploaded because the
  devices were unplugged; they run the previous release build. Re-flash them before the next hardware test.
- A PC preview of the UI with M5GFX's `PlatformIO_SDL` example (the game and UI code only needs `display::canvas()`
  to be replaced) to speed up UI work.
- 3-4 players: HELLO already carries `maxPlayers` and a room byte, the session pairs only two devices.
- Auto-dim and auto-sleep, a per-game score store in NVS (settings use the namespace `arcade`).
- Only about 1.1 MB of the 3.19 MB app slot is used; the partition table (`default_8MB.csv`, two OTA slots and an unused
  1.5 MB spiffs) can be revisited when the firmware passes about 2 MB.

# StickS3 PlatformIO starter notes

This project targets the M5Stack StickS3 with the Arduino framework.

## Build and upload

1. Open this folder in VS Code and wait for PlatformIO to initialize its project environment.
2. Connect the StickS3 with a USB-C data cable.
3. Build with the PlatformIO toolbar or run `pio run` in the PlatformIO terminal.
4. To upload, hold the StickS3 side reset button for about 2 seconds. Release it when the internal green LED flashes, then run `pio run --target upload`.
5. Open the serial monitor at 115200 baud with `pio device monitor`.

The first build downloads the ESP32 toolchain, Arduino framework, and M5Unified library. No fixed serial port is configured, so PlatformIO can detect the connected board. If upload cannot find the board, check Windows Device Manager for its COM port and add `upload_port = COMx` under the environment in `platformio.ini`.
