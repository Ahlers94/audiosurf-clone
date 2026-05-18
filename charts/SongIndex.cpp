#include "charts/SongIndex.h"
#include "GameEngine.h" // Ensures we see Engine::TOTAL_SONGS directly

namespace Engine::Charts {

// ── Table definition ──────────────────────────────────────────────────────────
// Hardened: Sized explicitly by Engine::TOTAL_SONGS to prevent layout drift
const NoteChart* const kSongTable[Engine::TOTAL_SONGS] = {
    &kSong00_Chart, // Slot 0: November Wind
    &kSong00_Chart, // Slot 1: Placeholder (I Just Want My Soul)
    &kSong00_Chart, // Slot 2: Placeholder (Something Will Grow)
    &kSong00_Chart, // Slot 3: Placeholder
    &kSong00_Chart, // Slot 4: Placeholder
    &kSong00_Chart, // Slot 5: Placeholder
    &kSong00_Chart, // Slot 6: Placeholder
    nullptr         // Slot 7: CD_STREAM_TRACK_ID (Bypasses chart, uses real-time ring buffer)
};

// ── Compile-time size guard ───────────────────────────────────────────────────
// Hardened: Asserts directly against your engine constant instead of a hardcoded magic number
static_assert(
    static_cast<uint8_t>(sizeof(kSongTable) / sizeof(kSongTable[0])) == Engine::TOTAL_SONGS,
    "kSongTable size must equal GameEngine::TOTAL_SONGS. Update both tracking layers together."
);

} // namespace Engine::Charts
