#include "charts/SongIndex.h"

// Song00.cpp — "Stress Protocol" stress-test chart
//
// BPM:      120
// Meter:    4/4
// Duration: 16 bars (timeline 0x0000 → 0xFFFF, 16 × 0x1000 units)
// Notes:    74 total — taps, chords, and holds
//
// Timeline encoding:
//    bar_unit  = 0x1000  (one bar)
//    beat_unit = 0x0400  (one quarter note)
//    sixteenth = 0x0100  (one sixteenth note)

namespace Engine::Charts {

// ── Lane index aliases (Ensures mapping aligns with LANE_X[4] layout) ───────────
static constexpr uint8_t L = 0; // LaneLeft
static constexpr uint8_t R = 1; // LaneRight
static constexpr uint8_t U = 2; // LaneUp
static constexpr uint8_t D = 3; // LaneDown

// ── Flag bit aliases ──────────────────────────────────────────────────────────
static constexpr uint8_t F_NONE = 0x00;
static constexpr uint8_t F_ACC  = 0x04; // Accented (Visual layer only)

// ── Canonical Note Array (16-bar scale fitting inside uint16_t) ───────────────
// Standardized 4-Byte Struct Layout: { timeline, lane, flags }
static const Note kSong00_Notes_v2[] = {
    // ── SECTION 1 (bars 0–3): Fast tap sequences, single lane per bar ─────────
    // Bar 0 — Lane L, eight sixteenth taps
    { 0x0000, L, F_ACC  },
    { 0x0100, L, F_NONE },
    { 0x0200, L, F_NONE },
    { 0x0300, L, F_NONE },
    { 0x0400, L, F_ACC  }, // beat 1
    { 0x0500, L, F_NONE },
    { 0x0600, L, F_NONE },
    { 0x0700, L, F_NONE },

    // Bar 1 — Lane R, eight sixteenth taps
    { 0x1000, R, F_ACC  },
    { 0x1100, R, F_NONE },
    { 0x1200, R, F_NONE },
    { 0x1300, R, F_NONE },
    { 0x1400, R, F_ACC  }, // beat 1
    { 0x1500, R, F_NONE },
    { 0x1600, R, F_NONE },
    { 0x1700, R, F_NONE },

    // Bar 2 — Lane U, eight sixteenth taps
    { 0x2000, U, F_ACC  },
    { 0x2100, U, F_NONE },
    { 0x2200, U, F_NONE },
    { 0x2300, U, F_NONE },
    { 0x2400, U, F_ACC  }, // beat 1
    { 0x2500, U, F_NONE },
    { 0x2600, U, F_NONE },
    { 0x2700, U, F_NONE },

    // Bar 3 — Lane D, eight sixteenth taps
    { 0x3000, D, F_ACC  },
    { 0x3100, D, F_NONE },
    { 0x3200, D, F_NONE },
    { 0x3300, D, F_NONE },
    { 0x3400, D, F_ACC  }, // beat 1
    { 0x3500, D, F_NONE },
    { 0x3600, D, F_NONE },
    { 0x3700, D, F_NONE },

    // ── SECTION 2 (bars 4–7): Chords — simultaneous multi-lane hits ───────────
    // Bar 4 — two-note chords, L+R on beats, U+D on offbeats
    { 0x4000, L, F_ACC  }, // beat 0: L+R chord
    { 0x4000, R, F_ACC  },
    { 0x4200, U, F_NONE }, // offbeat: U+D chord
    { 0x4200, D, F_NONE },
    { 0x4400, L, F_ACC  }, // beat 1: L+R
    { 0x4400, R, F_ACC  },
    { 0x4600, U, F_NONE }, // offbeat: U+D
    { 0x4600, D, F_NONE },

    // Bar 5 — four-lane grand chord on beats 0 and 1
    { 0x5000, L, F_ACC  }, // beat 0: all-four chord
    { 0x5000, R, F_ACC  },
    { 0x5000, U, F_ACC  },
    { 0x5000, D, F_ACC  },
    { 0x5400, L, F_ACC  }, // beat 1: all-four chord
    { 0x5400, R, F_ACC  },
    { 0x5400, U, F_ACC  },
    { 0x5400, D, F_ACC  },

    // Bars 6–7 — alternating single taps between chords
    { 0x6000, L, F_NONE },
    { 0x6200, R, F_NONE },
    { 0x6400, U, F_NONE },
    { 0x6600, D, F_NONE },
    { 0x7000, D, F_NONE },
    { 0x7200, U, F_NONE },
    { 0x7400, R, F_NONE },
    { 0x7600, L, F_NONE },

    // ── SECTION 3 (bars 8–11): Long holds with concurrent tap overlay ─────────
    // Bar 8: L hold starts, R taps fire concurrently
    { 0x8000, L, F_ACC  }, // L hold: 0x8000 → 0x9000
    { 0x8200, R, F_NONE }, 
    { 0x8400, R, F_NONE },
    { 0x8600, R, F_NONE },
    { 0x8800, R, F_ACC  }, // mid-hold accent tap
    { 0x8A00, R, F_NONE },
    { 0x8C00, R, F_NONE },
    { 0x8E00, R, F_NONE },

    // Bar 9: U hold starts, D taps fire concurrently
    { 0x9000, U, F_ACC  }, // U hold: 0x9000 → 0xA000
    { 0x9200, D, F_NONE },
    { 0x9400, D, F_NONE },
    { 0x9600, D, F_NONE },
    { 0x9800, D, F_ACC  },
    { 0x9A00, D, F_NONE },
    { 0x9C00, D, F_NONE },
    { 0x9E00, D, F_NONE },

    // Bars 10–11: Two simultaneous holds (R + D), pure continuous loop test
    { 0xA000, R, F_ACC  }, // R hold: 0xA000 → 0xC000 (two bars)
    { 0xA000, D, F_ACC  }, 

    // ── SECTION 4 (bars 12–15): Staggered overlapping holds, all four lanes ───
    { 0xC000, L, F_ACC  }, // L hold: 0xC000 → 0xE000
    { 0xC400, R, F_ACC  }, // R hold: 0xC400 → 0xE400
    { 0xC800, U, F_ACC  }, // U hold: 0xC800 → 0xE800
    { 0xD800, D, F_ACC  }  // D hold: 0xD800 → 0xEF80 (sub-0xFFFF boundary)
};

constexpr uint16_t kNoteCount = static_cast<uint16_t>(sizeof(kSong00_Notes_v2) / sizeof(Note));

// ── Chronological sort guard ───────────────────────────────────────────────────
static_assert(
    kSong00_Notes_v2[kNoteCount - 1].timeline >= kSong00_Notes_v2[0].timeline,
    "Song00: Chart end boundary points backwards in time."
);

// ── Chart descriptor ───────────────────────────────────────────────────────────
const NoteChart kSong00_Chart = {
    0,               // songId
    kNoteCount,      // noteCount
    120,             // tempoBPM
    kSong00_Notes_v2 // notes array reference
};

} // namespace Engine::Charts
