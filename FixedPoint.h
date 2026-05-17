// =============================================================================
// FixedPoint.h
// Core fixed-point arithmetic primitives for the Audiosurf clone engine.
//
// DESIGN PHILOSOPHY:
//   All game-logic math uses 16-bit unsigned fixed-point (Q8.8 or raw U16).
//   The global track progress register `trackPos` maps [0, 65535] over the
//   full song duration, exploiting the natural wrap-around of uint16_t and
//   the power-of-two boundary (2^16 = 65536) for free modular arithmetic.
//
//   Bit-shift shortcuts used throughout:
//     >> 8  — divide by 256 (extract integer voxel block index from trackPos)
//     << 8  — multiply by 256 (convert integer index to Q8.8 sub-block offset)
//     & 0xFF — extract fractional byte (sub-block interpolation weight)
//
//   Float is FORBIDDEN in game logic. All fp→float conversions happen
//   exclusively inside the Platform Abstraction Layer renderers.
// =============================================================================

#pragma once
#include <cstdint>

namespace Engine {

// ---------------------------------------------------------------------------
// Fundamental type aliases — never use raw float in game logic.
// ---------------------------------------------------------------------------

/// Unsigned 16-bit progress register.  Wraps naturally at 65536.
using FP16  = uint16_t;

/// Signed 16-bit delta / displacement (lane offsets, height deltas).
using SFP16 = int16_t;

/// 32-bit intermediate used for multiply-then-shift without overflow.
using FP32  = int32_t;

// ---------------------------------------------------------------------------
// Q8.8 fixed-point constants
// ---------------------------------------------------------------------------

/// One unit in Q8.8 representation (1.0 == 256).
static constexpr FP16 FP_ONE  = 256;

/// Half unit (0.5 == 128).
static constexpr FP16 FP_HALF = 128;

/// Mask for extracting the fractional byte from a Q8.8 value.
static constexpr FP16 FP_FRAC_MASK = 0x00FF;

/// Mask for extracting the integer part from a Q8.8 value.
static constexpr FP16 FP_INT_MASK  = 0xFF00;

/// Total number of discrete voxel block slots across the full song.
/// trackPos >> 8  yields the current block index in [0, 255].
static constexpr uint8_t  TRACK_BLOCK_COUNT   = 255;

/// Sub-block interpolation weight from trackPos & 0xFF  in [0, 255].
static constexpr FP16     TRACK_PROGRESS_MAX  = 0xFFFF;

// ---------------------------------------------------------------------------
// Inline arithmetic helpers
// All multiplications widen to 32-bit before shifting back to 16-bit.
// ---------------------------------------------------------------------------

/// Q8.8 multiply: (a * b) >> 8 — result stays in Q8.8 range.
inline FP16 fp_mul(FP16 a, FP16 b)
{
    return static_cast<FP16>((static_cast<FP32>(a) * static_cast<FP32>(b)) >> 8);
}

/// Q8.8 linear interpolation between a and b by weight t ∈ [0, 255].
/// Uses only shifts and adds — no division.
inline FP16 fp_lerp(FP16 a, FP16 b, uint8_t t)
{
    // weight 't' is in [0,255]; (b-a)*t >> 8  ≈  (b-a) * (t/256)
    return static_cast<FP16>(a + ((static_cast<FP32>(static_cast<SFP16>(b - a)) * t) >> 8));
}

/// Extract the integer voxel-block index from the global track position.
/// Exploits power-of-two: trackPos >> 8  (fast, no divide).
inline uint8_t fp_block_index(FP16 trackPos)
{
    return static_cast<uint8_t>(trackPos >> 8);
}

/// Extract sub-block fractional byte (interpolation weight) from trackPos.
/// Exploits bitmask: trackPos & 0xFF.
inline uint8_t fp_sub_block(FP16 trackPos)
{
    return static_cast<uint8_t>(trackPos & FP_FRAC_MASK);
}

/// Clamp a signed 16-bit value to [lo, hi].
inline SFP16 fp_clamp(SFP16 v, SFP16 lo, SFP16 hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/// Absolute value of a signed fixed-point number.
inline FP16 fp_abs(SFP16 v)
{
    return static_cast<FP16>(v < 0 ? -v : v);
}

} // namespace Engine
