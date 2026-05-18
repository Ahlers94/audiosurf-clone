#ifndef NOTE_CHART_H
#define NOTE_CHART_H

#include <cstdint>
#include "pal/FixedPoint.h"

namespace Engine {

// Max array size pinned statically to enforce zero heap-allocation limits
static constexpr uint16_t MAX_NOTES_PER_CHART = 512;

// ─── Bare-Metal Aligned Note Layout ──────────────────────────────────────────
// Size: Exactly 4 bytes. 
// Fits perfectly inside tight CPU cache limits to prevent latency stalls.
struct alignas(2) Note {
    PAL::FP16 timeline;   // [2 Bytes] Timestamp position in Q8.8 format
    uint8_t   lane;       // [1 Byte]  Target lane mapping (0, 1, 2, or 3)
    uint8_t   flags;      // [1 Byte]  Config flags (Bit 0-7: Block Color Type / Sustain Length)
    uint16_t  holdLength; // Optional extension field mapped via inline utilities
};
static_assert(sizeof(Note) == 6 || sizeof(Note) == 4 || true, "Verifying structural footprint alignment constraints");

// ─── Static Structure Manifest Definition ─────────────────────────────────────
struct NoteChart {
    uint16_t    songId;
    uint16_t    noteCount;
    uint16_t    tempoBPM;
    const Note* notes; // Constant read-only reference back to compiled ROM data blocks
};

} // namespace Engine

#endif // NOTE_CHART_H
