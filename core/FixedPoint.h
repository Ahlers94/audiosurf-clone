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

/// Signed 16-bit delta / displacement (lane offsets, height deltas, vectors).
using SFP16 = int16_t;

/// 32-bit intermediate used for multiply-then-shift without overflow.
using FP32  = int32_t;

// ---------------------------------------------------------------------------
// Q8.8 fixed-point constants
// ---------------------------------------------------------------------------

static constexpr FP16  FP_ONE        = 256;    // 1.0 == 256
static constexpr FP16  FP_HALF       = 128;    // 0.5 == 128
static constexpr FP16  FP_FRAC_MASK  = 0x00FF; // Fraction extract
static constexpr FP16  FP_INT_MASK   = 0xFF00; // Integer extract

/// Total number of discrete voxel block slots across the track.
static constexpr uint8_t TRACK_BLOCK_COUNT = 255;

// ---------------------------------------------------------------------------
// Inline Conversion Helpers (Zero-Cost Abstractions)
// ---------------------------------------------------------------------------

/// Convert an integer value directly to Q8.8 fixed-point format.
inline constexpr SFP16 to_fp(int16_t value)
{
    return static_cast<SFP16>(static_cast<int32_t>(value) << 8);
}

/// Convert a Q8.8 fixed-point value back to a standard truncated integer.
inline constexpr int16_t to_int(SFP16 fpValue)
{
    return static_cast<int16_t>(fpValue >> 8);
}

// ---------------------------------------------------------------------------
// Inline Arithmetic Helpers
// ---------------------------------------------------------------------------

/// Unsigned Q8.8 multiply: (a * b) >> 8 — result stays in unsigned range.
inline constexpr FP16 fp_mul(FP16 a, FP16 b)
{
    return static_cast<FP16>((static_cast<uint32_t>(a) * static_cast<uint32_t>(b)) >> 8);
}

/// Signed Q8.8 multiply: (a * b) >> 8 — handles negative vectors safely.
inline constexpr SFP16 fp_smul(SFP16 a, SFP16 b)
{
    return static_cast<SFP16>((static_cast<FP32>(a) * static_cast<FP32>(b)) >> 8);
}

/// Q8.8 linear interpolation between a and b by weight t ∈ [0, 255].
/// Properly zero-extends variables to safely cross the 32768 loop boundary.
inline constexpr FP16 fp_lerp(FP16 a, FP16 b, uint8_t t)
{
    // Zero-extend directly to prevent incorrect signed evaluation jumps
    FP32 start = static_cast<FP32>(a);
    FP32 end   = static_cast<FP32>(b);
    return static_cast<FP16>(start + (((end - start) * static_cast<FP32>(t)) >> 8));
}

/// Extract the integer voxel-block index from the global track position [0, 255].
inline constexpr uint8_t fp_block_index(FP16 trackPos)
{
    return static_cast<uint8_t>(trackPos >> 8);
}

/// Extract sub-block fractional byte (interpolation weight) from trackPos [0, 255].
inline constexpr uint8_t fp_sub_block(FP16 trackPos)
{
    return static_cast<uint8_t>(trackPos & FP_FRAC_MASK);
}

/// Branchless clamp of a signed 16-bit value to [lo, hi].
/// Optimizes cleanly into native conditional move assembly instructions (e.g., shl/cmov).
inline constexpr SFP16 fp_clamp(SFP16 v, SFP16 lo, SFP16 hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

/// Absolute value of a signed fixed-point number.
/// Safe against two's-complement boundary underflows.
inline constexpr FP16 fp_abs(SFP16 v)
{
    return static_cast<FP16>(v < 0 ? -static_cast<FP32>(v) : static_cast<FP32>(v));
}

} // namespace Engine
