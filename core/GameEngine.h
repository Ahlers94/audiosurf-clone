// =============================================================================
// GameEngine.h
// The central engine class. Owns all gameplay state and drives the main loop.
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
    Idle     = 0,  ///< Pre-game, splash screen.
    Playing  = 1,  ///< Active gameplay.
    Paused   = 2,  ///< Pause menu overlay.
    GameOver = 3,  ///< Post-game score screen.
};

// ===========================================================================
// Track generator config (Optimized for Q8.8 Grid Alignment)
// ===========================================================================
static constexpr uint8_t HEIGHT_MAX        = 12;   ///< Max voxel column height.
static constexpr uint8_t HEIGHT_STEP_MAX   = 2;    ///< Max height delta between adjacent segs.

// 48 fixed-point integer units. Scaled to Q8.8 gives exactly 12288.
static constexpr uint16_t LANE_WIDTH_FP     = 48 << 8; 
static constexpr uint8_t  LANE_WIDTH_FP_INT = 48;   

// Lane-slide speed: 32/256 ≈ 0.125 of a lane per tick.
static constexpr FP16    LANE_SLIDE_SPEED  = 32;   

// ===========================================================================
// GameEngine
// ===========================================================================
class GameEngine
{
public:
    GameEngine() = default;

    /// Initialise engine with platform-specific implementations.
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

        state           = GameState::Idle;
        globalTrackPos  = 0;
        running         = true;
        generatorSeed   = 0xCAFEBABEu;

        // Pre-generate the first segments so geometry exists instantly
        for (uint8_t i = 0; i < LOOK_AHEAD_SEGMENTS; ++i)
            generateSegment(i);

        // Start audio — from this point getTrackProgress() is the clock.
        platform.audio->play(songId);
        state = GameState::Playing;
        return true;
    }

    bool isRunning() const { return running; }

    /// Fixed-timestep update loop (Runs at 60Hz Target)
    void tick()
    {
        if (!running) return;

        // 1. SYNC TRACK POSITION FROM AUDIO CLOCK
        FP16 newPos = platform.audio->getTrackProgress();
        globalTrackPos = newPos;
        player.trackPos = globalTrackPos;

        if (globalTrackPos == TRACK_PROGRESS_MAX) {
            state   = GameState::GameOver;
            running = false;
            return;
        }

        // 2. INPUT PROCESSING
        platform.input->poll();
        PAL::InputState actions = platform.input->readActions();

        if (state == GameState::Playing) {
            handlePlayingInput(actions);
        } else if (state == GameState::Paused) {
            handlePausedInput(actions);
            return; // Halt engine physics during pause state
        }

        // 3. LANE-SLIDE INTERPOLATION (Q8.8 arithmetic)
        if (player.lane != player.targetLane) {
            player.laneOffset = static_cast<FP16>(player.laneOffset + LANE_SLIDE_SPEED);
            if (player.laneOffset >= FP_ONE) {
                player.lane       = player.targetLane;
                player.laneOffset = 0;
            }
        }

        // 4. PROCEDURAL TRACK GENERATION (Ring-buffer look-ahead)
        uint8_t curIdx = fp_block_index(globalTrackPos);
        for (uint8_t i = 1; i <= LOOK_AHEAD_SEGMENTS; ++i) {
            uint8_t genIdx = static_cast<uint8_t>(curIdx + i); // Explicitly wraps at 255
            if (!trackManager.segments[genIdx].isActive) {
                generateSegment(genIdx);
                spawnBlocksForSegment(genIdx);
            }
        }

        // Retire historic segments behind the cursor
        uint8_t retireIdx = static_cast<uint8_t>(curIdx - 2); 
        trackManager.segments[retireIdx].reset(); 

        // 5. BLOCK COLLISION DETECTION
        for (uint16_t i = 0; i < BLOCK_POOL_SIZE; ++i) {
            uint8_t collected = blockManager.testCollect(
                blockManager.blocks[i],
                player.trackPos,
                player.lane);

            if (collected > 0) {
                player.score += static_cast<uint32_t>(collected) * player.combo;
                if (player.combo < 255) ++player.combo;

                // Position calculation matching hardware scaling offsets
                SFP16 bx = static_cast<SFP16>(blockManager.blocks[i].lane * LANE_WIDTH_FP_INT);
                particleManager.burst(bx, 0, 0,
                                      blockManager.blocks[i].colorIndex,
                                      /*count=*/12, /*lifetime=*/30);
            }
        }

        // 6. PARTICLE TICK (Euler Integer Integration)
        particleManager.update();

        // 7. CAMERA UPDATE (Float conversion occurs downstream inside PAL)
        platform.graphics->updateCamera(globalTrackPos, player.renderX(LANE_WIDTH_FP));
    }

    /// Platform-agnostic rendering call stack
    void render()
    {
        if (!running && state != GameState::GameOver) return;

        platform.graphics->beginFrame();
        uint8_t curIdx = fp_block_index(globalTrackPos);
        static constexpr uint8_t RENDER_WINDOW_SIZE = 24;

        for (uint8_t w = 0; w < RENDER_WINDOW_SIZE; ++w) {
            uint8_t segIdx = static_cast<uint8_t>(curIdx + w);
            const VoxelSegment& seg = trackManager.segments[segIdx];
            if (!seg.isActive) continue;

            SFP16 segZ = static_cast<SFP16>(w * FP_ONE);
            uint8_t subBlend = fp_sub_block(globalTrackPos);

            for (uint8_t col = 0; col < TRACK_WIDTH_VOXELS; ++col) {
                SFP16 xLeft  = static_cast<SFP16>(col       * LANE_WIDTH_FP_INT);
                SFP16 xRight = static_cast<SFP16>((col + 1) * LANE_WIDTH_FP_INT);

                // Safe linear interpolation across ring boundaries
                uint8_t nextSegIdx = static_cast<uint8_t>(segIdx + 1);
                uint8_t h0 = seg.heightMap[col];
                uint8_t h1 = trackManager.segments[nextSegIdx].heightMap[col];
                uint8_t h  = fp_lerp(static_cast<FP16>(h0 << 4),
                                     static_cast<FP16>(h1 << 4),
                                     subBlend) >> 4;

                SFP16 yBottom = 0;
                SFP16 yTop    = static_cast<SFP16>(h * FP_ONE);
                uint8_t ci    = seg.colorMap[col];
                uint8_t glowAlpha = seg.energyLevel >> 1;

                // --- PASS 1: Neon Glow Overlay ---
                SFP16 glowPad = static_cast<SFP16>(FP_HALF / 2); 
                platform.graphics->drawVoxelColumnGlow(
                    static_cast<SFP16>(xLeft  - glowPad),
                    static_cast<SFP16>(xRight + glowPad),
                    static_cast<SFP16>(yBottom - glowPad),
                    static_cast<SFP16>(yTop    + glowPad),
                    ci, glowAlpha);

                // --- PASS 2: Solid Voxel Column ---
                platform.graphics->drawVoxelColumn(xLeft, xRight, yBottom, yTop, ci);
            }
        }

        // --- PASS 3: Game Blocks ---
        for (uint16_t i = 0; i < BLOCK_POOL_SIZE; ++i) {
            const Block& b = blockManager.blocks[i];
            if (!b.isActive) continue;
            SFP16 bx = static_cast<SFP16>(b.lane * LANE_WIDTH_FP_INT);
            platform.graphics->drawBlock(bx, FP_ONE, 0, b.colorIndex);
        }

        // --- PASS 4: Burst Particles ---
        for (uint16_t i = 0; i < PARTICLE_POOL_SIZE; ++i) {
            const Particle& p = particleManager.particles[i];
            if (!p.isActive) continue;
            uint8_t alpha = static_cast<uint8_t>(p.life > 127 ? 255 : p.life << 1);
            platform.graphics->drawParticle(p.x, p.y, p.z, p.colorIndex, alpha);
        }

        // --- PASS 5: Screen Text / HUD Overlay ---
        platform.graphics->drawHUD(player.score, player.combo, 8, 8);
        platform.graphics->endFrame();
    }

    void shutdown()
    {
        platform.audio->stop();
        platform.audio->shutdown();
        platform.input->shutdown();
        platform.graphics->shutdown();
        running = false;
    }

private:
    void handlePlayingInput(PAL::InputState actions)
    {
        using A = PAL::InputAction;
        if (actions & static_cast<uint8_t>(A::LaneLeft)) {
            if (player.targetLane > 0) {
                player.targetLane--;
                player.laneOffset = 0; 
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

    void generateSegment(uint8_t segIdx)
    {
        VoxelSegment& seg = trackManager.segments[segIdx];
        seg.energyLevel = platform.audio->getEnergyLevel();

        // Safe continuous wrapping evaluation for structural ring-buffer
        uint8_t prevIdx = static_cast<uint8_t>(segIdx - 1);
        bool hasHistory = trackManager.segments[prevIdx].isActive;

        for (uint8_t col = 0; col < TRACK_WIDTH_VOXELS; ++col) {
            // High-velocity LCG
            generatorSeed = generatorSeed * 1664525u + 1013904223u;
            
            // Branchless division-free mapping for height delta
            int8_t rawMod = static_cast<int8_t>((generatorSeed >> 24) & 0x07); 
            if (rawMod > 4) rawMod = 2; // Constrain bounds without costly raw modulo loops
            int8_t delta = static_cast<int8_t>(rawMod - HEIGHT_STEP_MAX);

            uint8_t prevH = hasHistory ? trackManager.segments[prevIdx].heightMap[col] : (HEIGHT_MAX / 2);
            int16_t newH  = static_cast<int16_t>(prevH) + delta;

            seg.heightMap[col] = static_cast<uint8_t>(fp_clamp(static_cast<SFP16>(newH), 0, HEIGHT_MAX));
            seg.colorMap[col]  = static_cast<uint8_t>(((seg.energyLevel >> 4) + col) % PALETTE_SIZE);
        }
        seg.isActive = true;
    }

    void spawnBlocksForSegment(uint8_t segIdx)
    {
        const VoxelSegment& seg = trackManager.segments[segIdx];
        if ((seg.energyLevel >> 4) < 6) return;

        // Shift-left safely re-constructs the target track position context
        FP16 segTrackPos = static_cast<FP16>(static_cast<uint16_t>(segIdx) << 8);

        generatorSeed = generatorSeed * 1664525u + 1013904223u;
        uint8_t lane  = static_cast<uint8_t>((generatorSeed >> 20) % LANE_COUNT);
        uint8_t color = seg.colorMap[lane * 3 % TRACK_WIDTH_VOXELS];
        uint8_t sv    = static_cast<uint8_t>(1 + (seg.energyLevel >> 5)); 

        blockManager.spawn(segTrackPos, lane, color, sv);
    }

    PAL::PlatformBundle platform;
    TrackManager    trackManager;
    BlockManager    blockManager;
    ParticleManager particleManager;
    Player          player;

    FP16      globalTrackPos = 0;
    GameState state          = GameState::Idle;
    bool      running        = false;
    uint32_t  generatorSeed  = 0;
};

} // namespace Engine
