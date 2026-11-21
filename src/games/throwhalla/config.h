#pragma once

namespace cfg {

constexpr int SCREEN_W = 240;
constexpr int SCREEN_H = 135;

// The bottom strip (below the lava) holds the HUD.
constexpr float LAVA_Y = 106.0f;
constexpr int HUD_Y = 118;

constexpr float PLAYER_X[2] = {40.0f, 200.0f};
constexpr float PLAYER_HALF_W = 5.0f;
constexpr float PLAYER_H = 15.0f;
constexpr float HAND_DX = 7.0f;
constexpr float HAND_DY = 9.0f;

// Platforms and obstacles all oscillate vertically on a sine wave with the same period and amplitude;
// only their starting phase (and the obstacles' x) is random.
constexpr float PLATFORM_HALF_W = 14.0f;
constexpr float PLATFORM_THICK = 6.0f;
constexpr float PLATFORM_CENTER_Y = 80.0f;
constexpr float MOTION_AMP = 16.0f;
constexpr float MOTION_PERIOD_SEC = 3.4f;

// Obstacles sweep almost from the ceiling down to the lava, on a slower period to keep their speed moderate.
constexpr int OBSTACLE_COUNT = 3;
constexpr float OBSTACLE_R = 4.0f;
constexpr float OBSTACLE_CENTER_Y = 53.0f;
constexpr float OBSTACLE_AMP = 45.0f;
constexpr float OBSTACLE_PERIOD_SEC = 4.5f;
constexpr float OBSTACLE_X_MIN = 70.0f;
constexpr float OBSTACLE_X_MAX = 170.0f;
constexpr float OBSTACLE_JITTER = 0.25f;  // fraction of a slot; keeps neighbours well apart

// Jump height depends on how long the button is held: releasing early caps the upward speed.
constexpr float JUMP_V = 240.0f;
constexpr float JUMP_CUT_V = 110.0f;
constexpr float JUMP_G = 720.0f;

// Aim angle is relative to the facing direction: 0 = forward, 90 = straight up, 180 = straight backward.
// The platform multiplies the device tilt by the AIM setting (default 3.0) before it reaches the game:
// 30 deg of tilt aims up, 60 deg aims backward.
constexpr float AIM_MIN_DEG = -30.0f;
constexpr float AIM_MAX_DEG = 180.0f;
constexpr float AIM_FLIP_UP_DEG = 95.0f;    // above this the viking turns around
constexpr float AIM_FLIP_DOWN_DEG = 85.0f;  // and turns back below this (hysteresis)

constexpr float CHARGE_HALF_SEC = 0.75f;
constexpr float FIRE_COOLDOWN_SEC = 0.35f;

constexpr float WORLD_G = 260.0f;
constexpr float PROJ_LIFE_SEC = 5.0f;
constexpr int MAX_BOUNCES = 3;
constexpr float OWNER_IMMUNE_SEC = 0.15f;
constexpr float HIT_NOTICE_SEC = 0.7f;

// The bomb never explodes on contact: the fuse starts at its first bounce, then the blast grows slowly.
constexpr float BOMB_FUSE_SEC = 2.0f;
constexpr float BOMB_BLAST_R = 26.0f;
constexpr float BOMB_BLAST_SEC = 0.7f;
constexpr float BOMB_BLAST_GROW_SEC = 0.45f;
constexpr float BOMB_BLAST_FADE_SEC = 0.15f;

constexpr float BOOMERANG_PULL = 255.0f;

constexpr int WIN_SCORE = 5;
constexpr float ROUND_END_SEC = 1.2f;

// Versus only: a player who has not thrown this long after the round started forfeits the throw,
// so a round cannot hang on an idle opponent. The last TURN_WARN_SEC seconds are shown as a bar.
constexpr float TURN_TIMEOUT_SEC = 20.0f;
constexpr float TURN_WARN_SEC = 5.0f;

constexpr int MAX_PROJ = 8;

constexpr int MAX_POWERUPS = 3;
constexpr float POWERUP_R = 6.0f;
constexpr float BURGER_SCALE = 1.6f;
constexpr float LIGHTNING_SPEED = 1.5f;
constexpr float ICE_SPEED = 0.6f;
constexpr float SPLIT_ANGLE = 0.20943951f;  // each child diverges by 12 degrees

}  // namespace cfg
