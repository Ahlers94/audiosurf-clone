// =============================================================================
// FixedPoint.h
// Core fixed-point arithmetic primitives for the Dreamsurf engine.
// =============================================================================

#pragma once
#include <cstdint>

namespace Engine {

// ---------------------------------------------------------------------------
// Fundamental type aliases — float is strictly forbidden in simulation logic.
// ---------------------------------------------------------------------------

/// Unsigned 16-bit progress register. Wraps naturally at 65536.
using FP16  = uint16_t;

/// Signed 16-bit delta / displacement (lane offsets, height deltas).
using SFP16 = int16_t;

/// 32-bit intermediate used for multiply-then-shift without overflow.
using FP32  = int32_t;

// ---------------------------------------------------------------------------
// Q8.8 fixed-point constants
// ---------------------------------------------------------------------------

static constexpr FP16  FP_ONE           = 256;    // 1.0 == 256
static constexpr FP16  FP_HALF          = 128;    // 0.5 == 128
static constexpr FP16  FP_FRAC_MASK     = 0x00FF; // Fraction extract
static constexpr FP16  FP_INT_MASK      = 0xFF00; // Integer extract

/// Total number of discrete voxel block slots across the track.
static constexpr uint8_t TRACK_BLOCK_COUNT = 255;

// ---------------------------------------------------------------------------
// Inline arithmetic helpers
// ---------------------------------------------------------------------------

/// Q8.8 multiply: (a * b) >> 8 — result stays in Q8.8 range.
inline FP16 fp_mul(FP16 a, FP16 b)
{
    return static_cast<FP16>((static_cast<FP32>(a) * static_cast<FP32>(b)) >> 8);
}

/// Q8.8 linear interpolation between a and b by weight t ∈ [0, 255].
/// Avoids unsigned subtraction wrap-around anomalies.
inline FP16 fp_lerp(FP16 a, FP16 b, uint8_t t)
{
    // Cast to signed types prior to subtraction to maintain pure arithmetic safety
    FP32 delta = static_cast<FP32>(static_cast<SFP16>(b)) - static_cast<FP32>(static_cast<SFP16>(a));
    return static_cast<FP16>(static_cast<FP32>(a) + ((delta * t) >> 8));
}

/// Extract the integer voxel-block index from the global track position [0, 255].
inline uint8_t fp_block_index(FP16 trackPos)
{
    return static_cast<uint8_t>(trackPos >> 8);
}

/// Extract sub-block fractional byte (interpolation weight) from trackPos [0, 255].
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
