// =============================================================================
// Managers.h
// Fixed-size static object pools — TrackManager, BlockManager, ParticleManager.
//
// ZERO HEAP ALLOCATION GUARANTEE:
//   All arrays are declared as plain C arrays inside the class body.
//   They live in the BSS segment (zero-initialised at program load).
//   "new" and "malloc" are never called during the gameplay loop.
//
//   Pool iteration pattern used everywhere:
//     for (uint16_t i = 0; i < POOL_SIZE; ++i) {
//         if (!pool[i].isActive) continue;
//         // process pool[i]
//     }
//   This linear scan is cache-friendly and branchless on modern CPUs and
//   the SH-4 alike — far cheaper than a linked-list traversal.
// =============================================================================

#pragma once
#include "GameTypes.h"
#include "FixedPoint.h"

namespace Engine {

// ===========================================================================
// TrackManager
// ===========================================================================
//
// Owns the ring-buffer of VoxelSegments that represent the procedural track.
// The ring index is computed directly from the global trackPos:
//
//   FP shortcut:  ringIndex = fp_block_index(trackPos)  (== trackPos >> 8)
//
// This means the ring naturally advances as trackPos increments, and wraps
// back to 0 when trackPos wraps from 65535 to 0 — no modulo needed.
//
// "Look-ahead" generation: the engine pre-fills LOOK_AHEAD_SEGMENTS segments
// ahead of the player so geometry is always ready to render without stalls.
// ===========================================================================

static constexpr uint8_t LOOK_AHEAD_SEGMENTS = 32;

class TrackManager
{
public:
    TrackManager() { reset(); }

    /// Invalidate all segments.
    void reset()
    {
        for (uint16_t i = 0; i < TRACK_SEGMENT_COUNT; ++i)
            segments[i].reset();
    }

    /// Return the segment at a given trackPos.
    /// FP shortcut: index extracted by >> 8, array access is O(1).
    VoxelSegment& segmentAt(FP16 trackPos)
    {
        return segments[fp_block_index(trackPos)];
    }

    const VoxelSegment& segmentAt(FP16 trackPos) const
    {
        return segments[fp_block_index(trackPos)];
    }

    /// Mark a range of segments ahead of `currentPos` as needing generation.
    /// Called by the procedural generator each tick.
    /// Returns the index of the first segment that needs data.
    uint8_t firstDirtyAhead(FP16 currentPos) const
    {
        // Start one segment beyond current position.
        // FP shortcut: add FP_ONE (256) to advance one full block.
        uint8_t start = fp_block_index(static_cast<FP16>(currentPos + FP_ONE));
        return start;
    }

    // Public array — managers own their data as value, not pointer.
    VoxelSegment segments[TRACK_SEGMENT_COUNT];
};

// ===========================================================================
// BlockManager
// ===========================================================================
//
// Flat static pool of Block objects.  Spawn/despawn by toggling isActive.
//
// Collision detection FP shortcut:
//   distance = abs((int16_t)(block.trackPos - player.trackPos))
//   The cast to int16_t handles the signed delta across the wrap boundary.
//   No division; just a subtraction and comparison against a constant snap
//   distance (BLOCK_SNAP_DIST, measured in FP16 units).
// ===========================================================================

/// Snap distance for block collection (in FP16 units ≈ half a voxel segment).
static constexpr FP16 BLOCK_SNAP_DIST = 128;  // 0.5 * FP_ONE

class BlockManager
{
public:
    BlockManager() { reset(); }

    void reset()
    {
        for (uint16_t i = 0; i < BLOCK_POOL_SIZE; ++i)
            blocks[i].reset();
        activeCount = 0;
    }

    /// Activate the next free slot.  Returns nullptr if pool is full.
    Block* spawn(FP16 trackPos, uint8_t lane, uint8_t colorIndex, uint8_t score)
    {
        for (uint16_t i = 0; i < BLOCK_POOL_SIZE; ++i) {
            if (!blocks[i].isActive) {
                blocks[i].trackPos   = trackPos;
                blocks[i].lane       = lane;
                blocks[i].colorIndex = colorIndex;
                blocks[i].scoreValue = score;
                blocks[i].isActive   = true;
                ++activeCount;
                return &blocks[i];
            }
        }
        return nullptr;  // Pool exhausted — caller handles gracefully.
    }

    /// Deactivate a block (does not free memory).
    void despawn(Block& block)
    {
        if (block.isActive) {
            block.reset();
            if (activeCount > 0) --activeCount;
        }
    }

    /// Test a block against the player and despawn on hit.
    /// Returns scoreValue if collected, 0 otherwise.
    /// FP shortcut: signed subtraction + abs for distance, no division.
    uint8_t testCollect(Block& block, FP16 playerTrackPos, uint8_t playerLane)
    {
        if (!block.isActive) return 0;
        if (block.lane != playerLane) return 0;

        // Signed distance along track (handles wrap via int16_t cast).
        SFP16 dist = static_cast<SFP16>(block.trackPos - playerTrackPos);
        if (fp_abs(dist) < BLOCK_SNAP_DIST) {
            uint8_t sv = block.scoreValue;
            despawn(block);
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
//
// Flat static pool of Particle sparks for neon burst effects.
//
// Physics update per tick (fully integer):
//   p.x  += p.vx;         // position += velocity (Q8.8 + Q8.8)
//   p.y  += p.vy;
//   p.vy -= GRAVITY_FP;   // FP shortcut: subtract compile-time constant
//   p.life--;             // countdown; pool slot freed when 0
//
// No square-root, no trig — all straight-line Euler integration.
// ===========================================================================

/// Gravity constant in Q8.8 per tick (approx 0.06 world units/tick²).
static constexpr SFP16 GRAVITY_FP = 16;  // 16/256 = 0.0625

class ParticleManager
{
public:
    ParticleManager() { reset(); }

    void reset()
    {
        for (uint16_t i = 0; i < PARTICLE_POOL_SIZE; ++i)
            particles[i].reset();
    }

    /// Emit a burst of `count` particles at world position (x, y, z).
    /// Velocities are seeded via a fast LCG so no <random> is needed.
    void burst(SFP16 x, SFP16 y, SFP16 z,
               uint8_t colorIndex, uint8_t count, uint8_t lifetime)
    {
        for (uint8_t n = 0; n < count; ++n) {
            Particle* p = allocate();
            if (!p) return;  // Pool full — silently drop.

            p->x = x; p->y = y; p->z = z;
            p->colorIndex = colorIndex;
            p->life       = lifetime;
            p->isActive   = true;

            // LCG seeded on pool index — deterministic, no heap, no stdlib rand.
            // FP shortcut: velocity spread via bit-mask on a simple LCG output.
            lcgSeed = lcgSeed * 1664525u + 1013904223u;
            p->vx = static_cast<SFP16>(((lcgSeed >> 16) & 0x3F) - 32); // ±32 Q8.8
            p->vy = static_cast<SFP16>(((lcgSeed >> 8)  & 0x3F) + 16); // upward bias
            p->vz = static_cast<SFP16>(((lcgSeed)       & 0x3F) - 32);
        }
    }

    /// Advance all active particles by one tick.
    void update()
    {
        for (uint16_t i = 0; i < PARTICLE_POOL_SIZE; ++i) {
            Particle& p = particles[i];
            if (!p.isActive) continue;

            // Euler integration — pure integer arithmetic.
            p.x  = static_cast<SFP16>(p.x + p.vx);
            p.y  = static_cast<SFP16>(p.y + p.vy);
            p.z  = static_cast<SFP16>(p.z + p.vz);
            p.vy = static_cast<SFP16>(p.vy - GRAVITY_FP);

            if (p.life == 0) { p.isActive = false; }
            else              { --p.life;            }
        }
    }

    Particle particles[PARTICLE_POOL_SIZE];

private:
    Particle* allocate()
    {
        for (uint16_t i = 0; i < PARTICLE_POOL_SIZE; ++i)
            if (!particles[i].isActive) return &particles[i];
        return nullptr;
    }

    uint32_t lcgSeed = 0xDEADBEEFu;
};

} // namespace Engine
