// =============================================================================
// Managers.h
// Fixed-size static object pools — TrackManager, BlockManager, ParticleManager.
//
// ZERO HEAP ALLOCATION GUARANTEE:
//   All arrays are declared as plain C arrays inside the class body.
//   They live in the BSS segment (zero-initialised at program load).
//   "new" and "malloc" are never called during the gameplay loop.
// =============================================================================

#pragma once
#include "GameTypes.h"
#include "FixedPoint.h"

namespace Engine {

// ===========================================================================
// TrackManager
// ===========================================================================
static constexpr uint8_t LOOK_AHEAD_SEGMENTS = 32;

class TrackManager
{
public:
    TrackManager() { reset(); }

    /// Invalidate all segments.
    void reset()
    {
        for (uint16_t i = 0; i < TRACK_SEGMENT_COUNT; ++i) {
            segments[i].reset();
        }
    }

    /// Return the segment at a given trackPos.
    inline VoxelSegment& segmentAt(FP16 trackPos)
    {
        return segments[fp_block_index(trackPos)];
    }

    inline const VoxelSegment& segmentAt(FP16 trackPos) const
    {
        return segments[fp_block_index(trackPos)];
    }

    /// Returns the index of the first segment that needs data.
    /// Safely handles fixed-point integer coordinate bit scaling.
    uint8_t firstDirtyAhead(FP16 currentPos) const
    {
        uint32_t forwardPos = static_cast<uint32_t>(currentPos) + (LOOK_AHEAD_SEGMENTS << 8); 
        return fp_block_index(static_cast<FP16>(forwardPos));
    }

    // Public allocation space
    VoxelSegment segments[TRACK_SEGMENT_COUNT];
};

// ===========================================================================
// BlockManager
// ===========================================================================
static constexpr FP16 BLOCK_SNAP_DIST = 128; // 0.5 * FP_ONE

class BlockManager
{
public:
    BlockManager() { reset(); }

    void reset()
    {
        for (uint16_t i = 0; i < BLOCK_POOL_SIZE; ++i) {
            blocks[i].reset();
        }
        activeCount = 0;
    }

    /// Activate the next free slot. Returns nullptr if pool is full.
    Block* spawn(FP16 trackPos, uint8_t lane, uint8_t colorIndex, uint8_t score)
    {
        for (uint16_t i = 0; i < BLOCK_POOL_SIZE; ++i) {
            if (!blocks[i].isActive) {
                blocks[i].trackPos   = trackPos;
                blocks[i].lane       = lane & 0x07; // Guarantee fit inside 3-bit space
                blocks[i].colorIndex = colorIndex & 0x07;
                blocks[i].scoreValue = score;
                blocks[i].isActive   = true;
                ++activeCount;
                return &blocks[i];
            }
        }
        return nullptr;
    }

    /// Deactivate a block with an explicit block layout overwrite.
    void despawn(Block& block)
    {
        if (block.isActive) {
            block.reset();
            if (activeCount > 0) --activeCount;
        }
    }

    /// Test a block against the player and despawn on hit.
    uint8_t testCollect(Block& block, FP16 playerTrackPos, uint8_t playerLane)
    {
        // Guard against duplicate reads on dead pool slots instantly
        if (!block.isActive) return 0;
        if (static_cast<uint8_t>(block.lane) != playerLane) return 0;

        // Wrap-safe signed distance delta check
        SFP16 dist = static_cast<SFP16>(block.trackPos - playerTrackPos);
        if (fp_abs(dist) < BLOCK_SNAP_DIST) {
            uint8_t sv = block.scoreValue;
            despawn(block); // Invalidates active flags immediately before extraction flags clear
            return sv;
        }
        return 0;
    }

    Block    blocks[BLOCK_POOL_SIZE];
    uint16_t activeCount = 0;
};

// ===========================================================================
// ParticleManager
// ===========================================================================
static constexpr SFP16 GRAVITY_FP = 16; // 16/256 = 0.0625 world units/tick²

class ParticleManager
{
public:
    ParticleManager() { reset(); }

    void reset()
    {
        for (uint16_t i = 0; i < PARTICLE_POOL_SIZE; ++i) {
            particles[i].reset();
        }
        nextFreeSlot = 0;
    }

    /// Emit a burst of particles. Optimized to ensure strict O(1) allocation.
    void burst(SFP16 x, SFP16 y, SFP16 z, uint8_t colorIndex, uint8_t count, uint8_t lifetime)
    {
        for (uint8_t n = 0; n < count; ++n) {
            // Constant-time O(1) index selection using circular ring index recycling
            uint16_t slot = nextFreeSlot;
            nextFreeSlot = static_cast<uint16_t>((nextFreeSlot + 1) & (PARTICLE_POOL_SIZE - 1));

            Particle& p = particles[slot];
            p.x = x; p.y = y; p.z = z;
            p.colorIndex = colorIndex & 0x07;
            p.life       = lifetime;
            p.isActive   = true;

            // Direct bitfield mask mappings step past nested function overhead blocks
            lcgSeed = lcgSeed * 1664525u + 1013904223u;
            p.vx = static_cast<SFP16>(((lcgSeed >> 16) & 0x3F) - 32); // ±32 Q8.8
            p.vy = static_cast<SFP16>(((lcgSeed >> 8)  & 0x3F) + 16); // Upward velocity bias
            p.vz = static_cast<SFP16>(((lcgSeed)       & 0x3F) - 32); // ±32 Q8.8
        }
    }

    /// Advance active particles using pure integer Euler integration.
    void update()
    {
        for (uint16_t i = 0; i < PARTICLE_POOL_SIZE; ++i) {
            Particle& p = particles[i];
            if (!p.isActive) continue;

            // Check if particle has reached the end of its life before updating physics
            if (p.life == 0) {
                p.isActive = false;
                continue;
            }
            --p.life;

            // Apply equations of motion only to valid, active elements
            p.x  = static_cast<SFP16>(p.x + p.vx);
            p.y  = static_cast<SFP16>(p.y + p.vy);
            p.z  = static_cast<SFP16>(p.z + p.vz);
            p.vy = static_cast<SFP16>(p.vy - GRAVITY_FP);
        }
    }

    Particle particles[PARTICLE_POOL_SIZE];

private:
    uint32_t lcgSeed      = 0xDEADBEEFu;
    uint16_t nextFreeSlot = 0; ///< Rolling allocation ring pointer
};

} // namespace Engine
