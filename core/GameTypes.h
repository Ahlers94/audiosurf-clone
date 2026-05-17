// =============================================================================
// GameTypes.h
// Core data structures for the Audiosurf clone.
//
// MEMORY CONTRACT:
//   Every struct here must be trivially constructible and destructible.
//   No virtual tables.  No pointers to heap memory.  No STL containers.
//   All arrays are fixed-size at compile time — the only allocator is the
//   program's BSS/data segment, resolved at link time.
//
//   On a 16 MB Dreamcast:
//     TrackManager  ≈ TRACK_SEGMENT_COUNT * sizeof(VoxelSegment)
//     BlockManager  ≈ BLOCK_POOL_SIZE     * sizeof(Block)
//     ParticlePool  ≈ PARTICLE_POOL_SIZE  * sizeof(Particle)
//   All three fit comfortably in ~1 MB, leaving the rest for audio buffers
//   and the PowerVR display lists.
// =============================================================================

#pragma once
#include <cstdint>
#include "FixedPoint.h"

namespace Engine {

// ---------------------------------------------------------------------------
// Palette & Lane Constants
// ---------------------------------------------------------------------------

/// Number of discrete colour palette entries (fits in 3 bits).
static constexpr uint8_t PALETTE_SIZE         = 8;

/// Number of player lanes across the track.
static constexpr uint8_t LANE_COUNT           = 5;

/// Number of visible voxel columns (width of track grid).
static constexpr uint8_t TRACK_WIDTH_VOXELS   = 16;

/// Number of voxel segments in the circular track ring-buffer.
static constexpr uint16_t TRACK_SEGMENT_COUNT = 256;

/// Maximum blocks alive simultaneously (static pool).
static constexpr uint16_t BLOCK_POOL_SIZE     = 128;

/// Maximum particles alive simultaneously (static pool).
static constexpr uint16_t PARTICLE_POOL_SIZE  = 512;

// ---------------------------------------------------------------------------
// VoxelSegment
// ---------------------------------------------------------------------------
//
// One horizontal "slice" of the track at a given song position.
// Stored in a flat ring-buffer indexed by (trackPos >> 8).
//
// heightMap[] — one byte per column, encoding the vertical extrusion.
//               Value range [0, 15] in fixed voxel units.
//               FP shortcut: height << 4 gives a Q8.8-compatible offset
//               without a multiply when scaling to world-space later.
//
// colorMap[]  — packed palette index (3 bits used, upper 5 reserved).
//               Kept as uint8_t so the renderer can table-lookup RGBA
//               without branching.
//
// energyLevel — audio energy marker in [0, 255] sampled from the MP3
//               beat-detection stream.  Used to modulate glow intensity.
//               FP shortcut: energyLevel >> 4 selects 1 of 16 glow tiers.
// ---------------------------------------------------------------------------

struct VoxelSegment
{
    /// Vertical extrusion per column, range [0, 15].
    uint8_t heightMap[TRACK_WIDTH_VOXELS];

    /// Palette index per column, range [0, PALETTE_SIZE-1].
    uint8_t colorMap[TRACK_WIDTH_VOXELS];

    /// Audio energy captured at this segment's song timestamp.
    /// High nibble = glow tier (energyLevel >> 4),
    /// Low  nibble = sub-tier interpolation weight.
    uint8_t energyLevel;

    /// True when this slot has been filled by the track generator.
    bool    isActive;

    /// Reset to a silent, flat, inactive state.
    void reset()
    {
        for (uint8_t i = 0; i < TRACK_WIDTH_VOXELS; ++i) {
            heightMap[i] = 0;
            colorMap[i]  = 0;
        }
        energyLevel = 0;
        isActive    = false;
    }
};

// ---------------------------------------------------------------------------
// Block
// ---------------------------------------------------------------------------
//
// A collectible grid block riding the track surface.  Managed by BlockManager
// via a static object pool — no heap allocation ever occurs.
//
// trackPos   — the absolute song-position (same coordinate space as the
//              global progress register) at which this block sits.
//              FP shortcut: (block.trackPos - player.trackPos) >> 8 gives
//              the integer segment distance ahead without division.
//
// lane       — [0, LANE_COUNT-1], stored as uint8_t to save space.
//
// colorIndex — palette lookup index for this block's render color.
//
// scoreValue — points awarded on collection, range [1, 255].
// ---------------------------------------------------------------------------

struct Block
{
    /// Position along the song timeline in global FP16 coordinates.
    FP16    trackPos;

    /// Which lane column this block occupies.
    uint8_t lane;

    /// Palette color index.
    uint8_t colorIndex;

    /// Score value when collected.
    uint8_t scoreValue;

    /// Object-pool active flag — set false to "destroy" without freeing.
    bool    isActive;

    void reset()
    {
        trackPos   = 0;
        lane       = 0;
        colorIndex = 0;
        scoreValue = 0;
        isActive   = false;
    }
};

// ---------------------------------------------------------------------------
// Particle
// ---------------------------------------------------------------------------
//
// A single spark in the neon-glow particle burst emitted on block collection
// or on beat events.  Stored in a flat fixed-size pool.
//
// All positions and velocities are Q8.8 fixed-point.
// FP shortcut: velocity components are added directly to position each tick;
// gravity is a small negative constant applied to vy each frame via addition.
// ---------------------------------------------------------------------------

struct Particle
{
    SFP16   x, y, z;        ///< World-space position (Q8.8).
    SFP16   vx, vy, vz;     ///< Velocity per tick (Q8.8).
    uint8_t colorIndex;      ///< Palette index.
    uint8_t life;            ///< Remaining lifetime in ticks [0, 255].
    bool    isActive;

    void reset()
    {
        x = y = z = 0;
        vx = vy = vz = 0;
        colorIndex = 0;
        life       = 0;
        isActive   = false;
    }
};

// ---------------------------------------------------------------------------
// Player
// ---------------------------------------------------------------------------
//
// All player state.  One instance lives inside GameEngine as a value member.
//
// lane         — current integer lane [0, LANE_COUNT-1].
// laneOffset   — Q8.8 sub-lane interpolation offset for smooth sliding.
//                FP shortcut: interpolated render X = lane * LANE_WIDTH_FP
//                + laneOffset, where LANE_WIDTH_FP is a compile-time constant.
//
// trackPos     — mirrors the engine's global progress register.  Kept here
//                so collision tests can compare Block.trackPos directly:
//                  hit = (abs(block.trackPos - player.trackPos) < SNAP_DIST)
//                  && (block.lane == player.lane);
//                Both sides are FP16 — the subtraction wraps safely.
//
// score        — 32-bit to accommodate long songs with many blocks.
//
// combo        — multiplier counter; reset on missed block.
// ---------------------------------------------------------------------------

struct Player
{
    /// Current lane index, clamped to [0, LANE_COUNT-1].
    uint8_t lane;

    /// Smooth lane-slide interpolation position (Q8.8).
    /// 0 = fully in current lane, FP_ONE = fully in target lane.
    FP16    laneOffset;

    /// Target lane for smooth interpolation (set on input event).
    uint8_t targetLane;

    /// The player's own view of the global track progress (FP16).
    FP16    trackPos;

    /// Score accumulator.
    uint32_t score;

    /// Combo multiplier (capped at 255 to stay in uint8_t).
    uint8_t  combo;

    /// True if the player is alive (has not crashed).
    bool     isAlive;

    void reset()
    {
        lane        = LANE_COUNT / 2;   // Start in the middle lane.
        laneOffset  = 0;
        targetLane  = lane;
        trackPos    = 0;
        score       = 0;
        combo       = 1;
        isAlive     = true;
    }

    /// Returns the Q8.8 render X position of the player along the track width.
    /// LANE_WIDTH_FP must be defined as a compile-time FP16 constant by caller.
    /// FP shortcut: multiply by shift, not by float multiplication.
    inline FP16 renderX(FP16 laneWidthFP) const
    {
        // lane * laneWidthFP  (integer * FP16 = FP16, no overflow for small lane)
        FP16 base = static_cast<FP16>(lane * laneWidthFP);
        // Add the smooth interpolation delta.
        return static_cast<FP16>(base + fp_lerp(0, laneWidthFP, static_cast<uint8_t>(laneOffset)));
    }
};

} // namespace Engine
