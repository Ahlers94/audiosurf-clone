// =============================================================================
// GameTypes.h
// Core data structures for the Audiosurf clone.
//
// MEMORY CONTRACT:
//   Every struct here must be trivially constructible and destructible.
//   No virtual tables. No pointers to heap memory. No STL containers.
//   All arrays are fixed-size at compile time — the only allocator is the
//   program's BSS/data segment, resolved at link time.
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
// Size: Exactly 36 Bytes (Aligned to 4-byte boundaries for SH-4 performance)
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

    /// Explicit structure alignment padding bytes to ensure clean 32-bit boundary.
    uint8_t reserved0;
    uint8_t reserved1;

    /// Reset to a silent, flat, inactive state.
    void reset()
    {
        for (uint8_t i = 0; i < TRACK_WIDTH_VOXELS; ++i) {
            heightMap[i] = 0;
            colorMap[i]  = 0;
        }
        energyLevel = 0;
        isActive    = false;
        reserved0   = 0;
        reserved1   = 0;
    }
};

// ---------------------------------------------------------------------------
// Block
// ---------------------------------------------------------------------------
// Size: Exactly 8 Bytes (Optimized footprint for low L1 cache usage)
// ---------------------------------------------------------------------------
struct Block
{
    /// Position along the song timeline in global FP16 coordinates (2 bytes).
    FP16    trackPos;

    /// Score value when collected (1 byte).
    uint8_t scoreValue;

    /// Packed bitfield structure avoiding multi-byte alignment padding (1 byte total).
    uint8_t lane       : 3;  ///< Which lane column this block occupies [0..4].
    uint8_t colorIndex : 3;  ///< Palette color index [0..7].
    uint8_t isActive   : 1;  ///< Object-pool status flag.
    uint8_t reserved   : 1;  ///< Leftover bit padding.

    /// Additional padding to lock struct to a clean 32-bit boundary alignment (4 bytes).
    uint8_t structuralPadding[4];

    void reset()
    {
        trackPos   = 0;
        scoreValue = 0;
        lane       = 0;
        colorIndex = 0;
        isActive   = false;
        reserved   = 0;
        
        for(uint8_t i = 0; i < 4; ++i) {
            structuralPadding[i] = 0;
        }
    }
};

// ---------------------------------------------------------------------------
// Particle
// ---------------------------------------------------------------------------
// Size: Exactly 16 Bytes (Perfect alignment for SH-4 cache-line prefetching)
// ---------------------------------------------------------------------------
struct Particle
{
    SFP16   x, y, z;     ///< World-space position (Q8.8) -> 6 bytes
    SFP16   vx, vy, vz;  ///< Velocity per tick (Q8.8)     -> 6 bytes
    uint8_t colorIndex;  ///< Palette index                -> 1 byte
    uint8_t life;        ///< Remaining lifetime in ticks  -> 1 byte
    bool    isActive;    ///< Execution allocation state   -> 1 byte
    uint8_t pad;         ///< Structural padding byte      -> 1 byte

    void reset()
    {
        x = y = z = 0;
        vx = vy = vz = 0;
        colorIndex = 0;
        life       = 0;
        isActive   = false;
        pad        = 0;
    }
};

// ---------------------------------------------------------------------------
// Player
// ---------------------------------------------------------------------------
// Size: Exactly 12 Bytes (Clean 32-bit multiple alignment profile)
// ---------------------------------------------------------------------------
struct Player
{
    /// Smooth lane-slide interpolation position (Q8.8).
    /// 0 = fully in current lane, FP_ONE = fully in target lane.
    FP16     laneOffset;

    /// The player's own view of the global track progress (FP16).
    FP16     trackPos;

    /// Score accumulator.
    uint32_t score;

    /// Current lane index, clamped to [0, LANE_COUNT-1].
    uint8_t  lane;

    /// Target lane for smooth interpolation (set on input event).
    uint8_t  targetLane;

    /// Combo multiplier (capped at 255 to stay in uint8_t).
    uint8_t  combo;

    /// True if the player is alive (has not crashed).
    bool     isAlive;

    void reset()
    {
        lane       = LANE_COUNT / 2;   // Start in the middle lane.
        laneOffset = 0;
        targetLane = lane;
        trackPos   = 0;
        score      = 0;
        combo      = 1;
        isAlive    = true;
    }

    /// Returns the Q8.8 render X position of the player along the track width.
    /// LANE_WIDTH_FP must be defined as a compile-time FP16 constant by caller.
    inline FP16 renderX(FP16 laneWidthFP) const
    {
        FP16 base = static_cast<FP16>(static_cast<FP16>(lane) * laneWidthFP);
        return static_cast<FP16>(base + fp_lerp(0, laneWidthFP, static_cast<uint8_t>(laneOffset)));
    }
};

} // namespace Engine
