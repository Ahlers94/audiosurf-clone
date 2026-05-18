#ifndef SONG_INDEX_H
#define SONG_INDEX_H

// SongIndex.h — chart table linkage header
//
// This header is the single point of truth for the kSongTable array that
// GameEngine::tick() indexes during song selection confirmation.
//
// OWNERSHIP RULES:
//   - kSongTable is defined exactly once, in SongIndex.cpp.
//   - Each kSong##_Chart symbol is defined exactly once, in its Song##.cpp.
//   - GameEngine.cpp includes this header and reads kSongTable[m_selectedSong].
//   - No other translation unit defines or redeclares these symbols.
//
// ADDING A SONG:
//   1. Create charts/Song##.cpp following the Song00.cpp template.
//   2. extern-declare its NoteChart below.
//   3. Add it to kSongTable in SongIndex.cpp.
//   4. Increment TOTAL_SONGS in GameEngine.h.
//   All four steps must be kept in sync — a mismatch is a linker error,
//   not a silent runtime out-of-bounds, which is the intended failure mode.

#include "NoteChart.h"

namespace Engine::Charts {

// ── Per-song chart forward declarations ───────────────────────────────────────
// Each symbol is defined in its corresponding Song##.cpp translation unit.
// The extern linkage means this header can be included in multiple TUs
// without producing duplicate-symbol linker errors.
extern const NoteChart kSong00_Chart; // Album Track 1 (e.g., November Wind)
extern const NoteChart kSong01_Chart; // Album Track 2 (e.g., I Just Want My Soul)
extern const NoteChart kSong02_Chart; // Album Track 3 (e.g., Something Will Grow)

// ── Chart lookup table ────────────────────────────────────────────────────────
// Indexed directly by GameEngine::m_selectedSong.
// Size must equal GameEngine::TOTAL_SONGS — a static_assert in SongIndex.cpp
// enforces this so a forgotten table entry is a compile error.
//
// Declared extern here; defined once in SongIndex.cpp.
// Pointer array rather than reference array: pointers are nullable, which
// lets us assert against a null slot at loadChart() time rather than
// dereferencing a dangling reference.
//
// NOTE: Slot [3] is reserved for CD_STREAM_TRACK_ID (99) / External Audio Stream mode,
// which points to nullptr to explicitly trigger the real-time ring buffer engine.
extern const NoteChart* const kSongTable[4];

} // namespace Engine::Charts

// Pull the table into the Engine namespace so GameEngine.cpp can write
//   kSongTable[m_selectedSong]
// without a fully-qualified Engine::Charts:: prefix at every call site.
namespace Engine {
    using Engine::Charts::kSongTable;
}

#endif // SONG_INDEX_H
