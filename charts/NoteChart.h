#ifndef NOTE_CHART_H
#define NOTE_CHART_H

#include <cstdint>
#include "FixedPoint.h"

namespace Engine {

static constexpr uint16_t MAX_NOTES_PER_CHART = 512;

// ─── Bare-Metal Aligned Note Layout ──────────────────────────────────────────
// Size: Exactly 4 bytes. Fits perfectly across high-speed hardware data lanes.
struct alignas(4) Note {
    PAL::FP16 timeline;   // [2 Bytes] Playhead timestamp (Q8.8 format)
    uint8_t   lane;       // [1 Byte]  Target lane mapping (0, 1, 2, or 3)
    uint8_t   flags;      // [1 Byte]  System bitmask (or compressed hold length)
};

// Strict compile-time safety check
static_assert(sizeof(Note) == 4, "Consistency Error: Struct Note size is not 4 bytes!");

struct NoteChart {
    uint16_t    songId;
    uint16_t    noteCount;
    uint16_t    tempoBPM;
    const Note* notes; 
};

} // namespace Engine
#endif // NOTE_CHART_H
