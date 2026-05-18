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

#include "NoteChart.h"
#include "GameEngine.h" // Required for Engine::TOTAL_SONGS array boundaries

namespace Engine::Charts {

// ── Per-song chart forward declarations ───────────────────────────────────────
// Each symbol is defined in its corresponding Song##.cpp translation unit.
extern const NoteChart kSong00_Chart; // Track 1: November Wind
extern const NoteChart kSong01_Chart; // Track 2: I Just Want My Soul
extern const NoteChart kSong02_Chart; // Track 3: Something Will Grow
extern const NoteChart kSong03_Chart; // Track 4
extern const NoteChart kSong04_Chart; // Track 5
extern const NoteChart kSong05_Chart; // Track 6
extern const NoteChart kSong06_Chart; // Track 7

// ── Chart lookup table ────────────────────────────────────────────────────────
// Indexed directly by GameEngine::m_selectedSong.
// Declared extern here; defined once in SongIndex.cpp.
//
// NOTE: Slot [7] (CD_STREAM_TRACK_ID) is reserved for External Audio Stream mode,
// which points to nullptr to explicitly trigger the real-time ring buffer engine.
extern const NoteChart* const kSongTable[Engine::TOTAL_SONGS];

} // namespace Engine::Charts

// Pull the table into the Engine namespace for clean access inside GameEngine.cpp
namespace Engine {
    using Engine::Charts::kSongTable;
}

#endif // SONG_INDEX_H
