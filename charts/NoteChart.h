#ifndef NOTE_CHART_H
#define NOTE_CHART_H

#include <cstdint>
#include "FixedPoint.h"

namespace Engine {

static constexpr uint16_t MAX_NOTES_PER_CHART = 512;

// ─── Bare-Metal Aligned Note Layout ──────────────────────────────────────────
// Using explicit uint16_t for timeline to guarantee 2-byte size,
// ensuring the full struct is exactly 4 bytes.
#pragma pack(push, 1)
struct Note {
    uint16_t timeline;   // [2 Bytes] Explicitly 16-bit
    uint8_t  lane;       // [1 Byte]
    uint8_t  flags;      // [1 Byte]
};
#pragma pack(pop)

// Strict compile-time safety check: 2 + 1 + 1 = 4
static_assert(sizeof(Note) == 4, "Consistency Error: Struct Note size is not 4 bytes!");

struct NoteChart {
    uint16_t   songId;
    uint16_t   noteCount;
    uint16_t   tempoBPM;
    const Note* notes; 
};

} // namespace Engine
#endif // NOTE_CHART_H
