// =============================================================================
// GameEngine.h
// The central engine class.  Owns all gameplay state and drives the main loop.
//
// ARCHITECTURE SUMMARY:
//   GameEngine holds every manager as a VALUE member (no heap, no pointer).
//   It receives a PlatformBundle at init time and talks to hardware only
//   through those three abstract interfaces — never directly.
//
//   Main loop contract (called from main.cpp or the Emscripten main loop cb):
//
//     engine.init(bundle);
//     while (engine.isRunning()) {
//         engine.tick();          // fixed-timestep update
//         engine.render();        // platform-agnostic draw calls
//     }
//     engine.shutdown();
//
//   On Dreamcast, main.cpp calls tick()+render() inside a vid_waitvbl() loop.
//   On Web, emscripten_set_main_loop() drives the same tick()+render() pair.
// =============================================================================

#pragma once
#include "core/FixedPoint.h"
#include "core/GameTypes.h"
#include "core/Managers.h"
#include "pal/PAL.h"

namespace Engine {

// ===========================================================================
// Game state machine
// ===========================================================================

enum class GameState : uint8_t
{
    Idle        = 0,  ///< Pre-game, splash screen.
    Playing     = 1,  ///< Active gameplay.
    Paused      = 2,  ///< Pause menu overlay.
    GameOver    = 3,  ///< Post-game score screen.
};

// ===========================================================================
// Track generator config
// ===========================================================================
// These constants tune the procedural height-map generator.
// All values are in FP16 / uint8_t — no floats.

static constexpr uint8_t HEIGHT_MAX        = 12;   ///< Max voxel column height.
static constexpr uint8_t HEIGHT_STEP_MAX   = 2;    ///< Max height delta between adjacent segs.
static constexpr uint8_t LANE_WIDTH_FP_INT = 48;   ///< Lane width in Q8.8 integer part (48/256 ≈ 0.19 world units).
static constexpr FP16    LANE_WIDTH_FP     = static_cast<FP16>(LANE_WIDTH_FP_INT * FP_ONE / 10);

// Lane-slide speed: how fast laneOffset moves toward FP_ONE per tick.
// FP shortcut: add this constant each tick, compare with FP_ONE using integer >=.
static constexpr FP16    LANE_SLIDE_SPEED  = 32;   ///< 32/256 ≈ 0.125 of a lane per tick.

// ===========================================================================
// GameEngine
// ===========================================================================

class GameEngine
{
public:
    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    GameEngine() = default;

    /// Initialise engine with platform-specific implementations.
    /// `bundle` is provided by the platform factory — engine does NOT own it.
    bool init(PAL::PlatformBundle bundle, uint8_t songId)
    {
        platform = bundle;

        // Initialise all subsystems through the PAL.
        if (!platform.graphics->init(640, 480)) return false;
        if (!platform.audio->init())            return false;
        if (!platform.input->init())            return false;

        // Reset all static pools — no heap touched.
        trackManager.reset();
        blockManager.reset();
        particleManager.reset();
        player.reset();

        state            = GameState::Idle;
        globalTrackPos   = 0;
        running          = true;
        generatorSeed    = 0xCAFEBABEu;

        // Pre-generate the first LOOK_AHEAD_SEGMENTS segments before audio
        // starts, so the renderer has geometry immediately.
        for (uint8_t i = 0; i < LOOK_AHEAD_SEGMENTS; ++i)
            generateSegment(i);

        // Start audio — from this point getTrackProgress() is the clock.
        platform.audio->play(songId);
        state = GameState::Playing;
        return true;
    }

    bool isRunning() const { return running; }

    // -----------------------------------------------------------------------
    // tick()  —  Fixed-timestep update.
    // Called once per frame (60 Hz target; Dreamcast VBL-locked).
    //
    // FIXED-POINT UPDATE PIPELINE:
    //   1. Sync globalTrackPos from audio clock.
    //   2. Process input → update player lane (integer).
    //   3. Advance lane-slide interpolation (FP16 += constant).
    //   4. Pre-generate track segments ahead of cursor.
    //   5. Test block collisions (FP16 subtraction + abs).
    //   6. Tick particles (Euler integration, integer only).
    // -----------------------------------------------------------------------
    void tick()
    {
        if (!running) return;

        // ------------------------------------------------------------------
        // 1. SYNC TRACK POSITION FROM AUDIO CLOCK
        //    FP shortcut: getTrackProgress() returns FP16 directly;
        //    no conversion needed.  The audio layer IS the song clock.
        // ------------------------------------------------------------------
        FP16 newPos = platform.audio->getTrackProgress();
        globalTrackPos = newPos;
        player.trackPos = globalTrackPos;

        if (globalTrackPos == TRACK_PROGRESS_MAX) {
            state   = GameState::GameOver;
            running = false;
            return;
        }

        // ------------------------------------------------------------------
        // 2. INPUT PROCESSING
        // ------------------------------------------------------------------
        platform.input->poll();
        PAL::InputState actions = platform.input->readActions();

        if (state == GameState::Playing) {
            handlePlayingInput(actions);
        } else if (state == GameState::Paused) {
            handlePausedInput(actions);
            return;  // Skip physics while paused.
        }

        // ------------------------------------------------------------------
        // 3. LANE-SLIDE INTERPOLATION (Q8.8 arithmetic)
        //
        //    laneOffset advances toward FP_ONE each tick by LANE_SLIDE_SPEED.
        //    FP shortcut: compare integer >= FP_ONE (no float needed).
        //    When laneOffset >= FP_ONE, snap lane and reset offset to 0.
        // ------------------------------------------------------------------
        if (player.lane != player.targetLane) {
            player.laneOffset = static_cast<FP16>(player.laneOffset + LANE_SLIDE_SPEED);
            if (player.laneOffset >= FP_ONE) {
                player.lane       = player.targetLane;
                player.laneOffset = 0;
            }
        }

        // ------------------------------------------------------------------
        // 4. PROCEDURAL TRACK GENERATION (look-ahead)
        //
        //    FP shortcut:  nextSegIdx = fp_block_index(globalTrackPos) + i
        //    The >> 8 extract gives us the integer segment index in O(1).
        // ------------------------------------------------------------------
        uint8_t curIdx = fp_block_index(globalTrackPos);
        for (uint8_t i = 1; i <= LOOK_AHEAD_SEGMENTS; ++i) {
            uint8_t genIdx = static_cast<uint8_t>(curIdx + i); // wraps at 256 naturally
            if (!trackManager.segments[genIdx].isActive) {
                generateSegment(genIdx);
                spawnBlocksForSegment(genIdx);
            }
        }

        // Retire segments that have scrolled past the player.
        // FP shortcut: segments behind = index < curIdx; ring wraps cleanly.
        uint8_t retireIdx = static_cast<uint8_t>(curIdx - 2); // 2 segments behind
        trackManager.segments[retireIdx].reset(); // isActive → false

        // ------------------------------------------------------------------
        // 5. BLOCK COLLISION DETECTION
        //
        //    FP shortcut: signed subtraction of two FP16 values.
        //    The distance is in Q8.8 units; compare against BLOCK_SNAP_DIST
        //    constant — pure integer compare, no division.
        // ------------------------------------------------------------------
        for (uint16_t i = 0; i < BLOCK_POOL_SIZE; ++i) {
            uint8_t collected = blockManager.testCollect(
                blockManager.blocks[i],
                player.trackPos,
                player.lane);

            if (collected > 0) {
                // Award score: value * combo multiplier.
                // FP shortcut: both are uint8_t, product fits in uint16_t.
                player.score += static_cast<uint32_t>(collected) * player.combo;

                // Increment combo (cap at 255).
                if (player.combo < 255) ++player.combo;

                // Spawn a neon burst at the block's approximate world position.
                SFP16 bx = static_cast<SFP16>(blockManager.blocks[i].lane * LANE_WIDTH_FP_INT);
                particleManager.burst(bx, 0, 0,
                                      blockManager.blocks[i].colorIndex,
                                      /*count=*/12, /*lifetime=*/30);
            }
        }

        // ------------------------------------------------------------------
        // 6. PARTICLE TICK
        //    Pure integer Euler integration — see ParticleManager::update().
        // ------------------------------------------------------------------
        particleManager.update();

        // ------------------------------------------------------------------
        // 7. CAMERA UPDATE (passed to PAL — float conversion happens there)
        // ------------------------------------------------------------------
        platform.graphics->updateCamera(globalTrackPos,
                                        player.renderX(LANE_WIDTH_FP));
    }

    // -----------------------------------------------------------------------
    // render()  —  Frame draw.
    //
    // Iterates the visible window of segments and issues drawVoxelColumn /
    // drawVoxelColumnGlow pairs to achieve the neon-bloom effect without
    // any off-screen framebuffer or blur shader.
    //
    // Draw order:
    //   Pass 1 — glow pass  (wider, semi-transparent, additive blending)
    //   Pass 2 — solid pass (tight inner geometry, fully opaque)
    //   Pass 3 — blocks
    //   Pass 4 — particles
    //   Pass 5 — HUD
    //
    // All coordinate math here is still FP16; the PAL converts to float.
    // -----------------------------------------------------------------------
    void render()
    {
        if (!running && state != GameState::GameOver) return;

        platform.graphics->beginFrame();

        uint8_t curIdx = fp_block_index(globalTrackPos);

        // Render RENDER_WINDOW_SIZE segments ahead of the player.
        static constexpr uint8_t RENDER_WINDOW_SIZE = 24;

        for (uint8_t w = 0; w < RENDER_WINDOW_SIZE; ++w) {
            uint8_t segIdx = static_cast<uint8_t>(curIdx + w);
            const VoxelSegment& seg = trackManager.segments[segIdx];
            if (!seg.isActive) continue;

            // Depth offset along Z (furthest segments rendered first).
            // FP shortcut: w * FP_ONE gives a Q8.8 Z value, no multiply needed
            // beyond a small integer × constant.
            SFP16 segZ = static_cast<SFP16>(w * FP_ONE);

            // Sub-block interpolation offset using fractional trackPos byte.
            // FP shortcut: sub = fp_sub_block(globalTrackPos) & 0xFF
            //              The fractional byte IS the blend weight.
            uint8_t subBlend = fp_sub_block(globalTrackPos);

            for (uint8_t col = 0; col < TRACK_WIDTH_VOXELS; ++col) {
                // World-space X extents for this column.
                SFP16 xLeft  = static_cast<SFP16>(col       * LANE_WIDTH_FP_INT);
                SFP16 xRight = static_cast<SFP16>((col + 1) * LANE_WIDTH_FP_INT);

                // Interpolate height between this segment and the next.
                // FP shortcut: fp_lerp uses >> 8 internally — no division.
                uint8_t nextSegIdx = static_cast<uint8_t>(segIdx + 1);
                uint8_t h0 = seg.heightMap[col];
                uint8_t h1 = trackManager.segments[nextSegIdx].heightMap[col];
                uint8_t h  = fp_lerp(static_cast<FP16>(h0 << 4),
                                     static_cast<FP16>(h1 << 4),
                                     subBlend) >> 4;

                SFP16 yBottom = 0;
                SFP16 yTop    = static_cast<SFP16>(h * FP_ONE);
                uint8_t ci    = seg.colorMap[col];

                // Glow alpha driven by energy level.
                // FP shortcut: energyLevel >> 1 gives [0,127] alpha range.
                uint8_t glowAlpha = seg.energyLevel >> 1;

                // --- PASS 1: Glow (wider, semi-transparent) ---
                SFP16 glowPad = static_cast<SFP16>(FP_HALF / 2); // 0.25 units
                platform.graphics->drawVoxelColumnGlow(
                    static_cast<SFP16>(xLeft  - glowPad),
                    static_cast<SFP16>(xRight + glowPad),
                    static_cast<SFP16>(yBottom - glowPad),
                    static_cast<SFP16>(yTop    + glowPad),
                    ci, glowAlpha);

                // --- PASS 2: Solid inner geometry ---
                platform.graphics->drawVoxelColumn(xLeft, xRight, yBottom, yTop, ci);
            }
        }

        // --- PASS 3: Blocks ---
        for (uint16_t i = 0; i < BLOCK_POOL_SIZE; ++i) {
            const Block& b = blockManager.blocks[i];
            if (!b.isActive) continue;
            SFP16 bx = static_cast<SFP16>(b.lane * LANE_WIDTH_FP_INT);
            platform.graphics->drawBlock(bx, FP_ONE, 0, b.colorIndex);
        }

        // --- PASS 4: Particles ---
        for (uint16_t i = 0; i < PARTICLE_POOL_SIZE; ++i) {
            const Particle& p = particleManager.particles[i];
            if (!p.isActive) continue;
            // Alpha fades with remaining life.
            // FP shortcut: (life << 1) approximates a linear 0→510 fade
            //              clamped by uint8_t rollover to [0,255].
            uint8_t alpha = static_cast<uint8_t>(
                p.life > 127 ? 255 : p.life << 1);
            platform.graphics->drawParticle(p.x, p.y, p.z, p.colorIndex, alpha);
        }

        // --- PASS 5: HUD ---
        platform.graphics->drawHUD(player.score, player.combo, 8, 8);

        platform.graphics->endFrame();
    }

    // -----------------------------------------------------------------------
    // shutdown()
    // -----------------------------------------------------------------------
    void shutdown()
    {
        platform.audio->stop();
        platform.audio->shutdown();
        platform.input->shutdown();
        platform.graphics->shutdown();
        running = false;
    }

private:
    // -----------------------------------------------------------------------
    // Input handlers
    // -----------------------------------------------------------------------

    void handlePlayingInput(PAL::InputState actions)
    {
        using A = PAL::InputAction;

        if (actions & static_cast<uint8_t>(A::LaneLeft)) {
            if (player.targetLane > 0) {
                player.targetLane--;
                player.laneOffset = 0;  // Reset slide progress.
            }
        }
        if (actions & static_cast<uint8_t>(A::LaneRight)) {
            if (player.targetLane < LANE_COUNT - 1) {
                player.targetLane++;
                player.laneOffset = 0;
            }
        }
        if (actions & static_cast<uint8_t>(A::Pause)) {
            state = GameState::Paused;
            platform.audio->setPaused(true);
        }
    }

    void handlePausedInput(PAL::InputState actions)
    {
        using A = PAL::InputAction;
        if (actions & static_cast<uint8_t>(A::Pause)) {
            state = GameState::Playing;
            platform.audio->setPaused(false);
        }
        if (actions & static_cast<uint8_t>(A::Back)) {
            running = false;
        }
    }

    // -----------------------------------------------------------------------
    // Procedural track generator
    //
    // Fills one VoxelSegment using a fast integer LCG + clamped walk.
    // No floats, no stdlib random, no heap.
    //
    // Height walk:  new_height = clamp(prev_height + delta, 0, HEIGHT_MAX)
    //   where delta ∈ {-HEIGHT_STEP_MAX … +HEIGHT_STEP_MAX} from LCG bits.
    // -----------------------------------------------------------------------

    void generateSegment(uint8_t segIdx)
    {
        VoxelSegment& seg = trackManager.segments[segIdx];

        // Pull energy from the audio layer at this moment.
        seg.energyLevel = platform.audio->getEnergyLevel();

        for (uint8_t col = 0; col < TRACK_WIDTH_VOXELS; ++col) {
            // LCG — 2 cycles, minimal cost.
            generatorSeed = generatorSeed * 1664525u + 1013904223u;
            int8_t delta = static_cast<int8_t>((generatorSeed >> 24) % (2 * HEIGHT_STEP_MAX + 1))
                           - HEIGHT_STEP_MAX;

            uint8_t prevH = (segIdx > 0)
                ? trackManager.segments[static_cast<uint8_t>(segIdx - 1)].heightMap[col]
                : (HEIGHT_MAX / 2);

            int16_t newH = static_cast<int16_t>(prevH) + delta;
            // Clamp without branching using fp_clamp on the signed value.
            seg.heightMap[col] = static_cast<uint8_t>(
                fp_clamp(static_cast<SFP16>(newH), 0, HEIGHT_MAX));

            // Color index: high nibble of energy + col offset, modulo palette.
            // FP shortcut: >> 4 extracts glow tier (energy high nibble).
            seg.colorMap[col] = static_cast<uint8_t>(
                ((seg.energyLevel >> 4) + col) % PALETTE_SIZE);
        }

        seg.isActive = true;
    }

    // -----------------------------------------------------------------------
    // Block spawner — called after generateSegment() for a given index.
    // Randomly populates up to 2 blocks per segment from the static pool.
    // -----------------------------------------------------------------------

    void spawnBlocksForSegment(uint8_t segIdx)
    {
        const VoxelSegment& seg = trackManager.segments[segIdx];

        // Only spawn on energetic beats.
        // FP shortcut: compare high nibble >> 4 against threshold.
        if ((seg.energyLevel >> 4) < 6) return;

        // Reconstruct FP16 trackPos for this segment index.
        // FP shortcut: index << 8 == index * 256 == FP16 for this block.
        FP16 segTrackPos = static_cast<FP16>(static_cast<uint16_t>(segIdx) << 8);

        // Use LCG to decide lane placement.
        generatorSeed = generatorSeed * 1664525u + 1013904223u;
        uint8_t lane  = static_cast<uint8_t>((generatorSeed >> 20) % LANE_COUNT);
        uint8_t color = seg.colorMap[lane * 3 % TRACK_WIDTH_VOXELS];
        uint8_t sv    = static_cast<uint8_t>(1 + (seg.energyLevel >> 5)); // 1–8 pts

        blockManager.spawn(segTrackPos, lane, color, sv);
    }

    // -----------------------------------------------------------------------
    // Data members  — ALL value types, zero heap footprint.
    // -----------------------------------------------------------------------

    PAL::PlatformBundle platform;        ///< Non-owning interface pointers.

    TrackManager    trackManager;        ///< 256 × VoxelSegment ring-buffer.
    BlockManager    blockManager;        ///< 128-slot block pool.
    ParticleManager particleManager;     ///< 512-slot particle pool.
    Player          player;              ///< Single player state.

    FP16        globalTrackPos   = 0;    ///< Master song progress register.
    GameState   state            = GameState::Idle;
    bool        running          = false;
    uint32_t    generatorSeed    = 0;    ///< LCG seed for procedural gen.
};

} // namespace Engine
