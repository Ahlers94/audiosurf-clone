#include "SongIndex.h"
#include "NoteChart.h"

// This is a companion to the header — kept separate so the static_assert
// can see TOTAL_SONGS from GameEngine.h without a circular include.
// SongIndex.cpp is the ONLY file that defines kSongTable.

namespace Engine::Charts {

// ── Table definition ──────────────────────────────────────────────────────────
// Initialiser order must match the extern declarations in SongIndex.h.
// The const-on-pointer-and-pointee means neither the table slots nor the
// chart structs they point to can be mutated at runtime.
const NoteChart* const kSongTable[3] = {
    &kSong00_Chart,
    &kSong01_Chart,
    &kSong02_Chart,
};

// ── Compile-time size guard ───────────────────────────────────────────────────
// If TOTAL_SONGS in GameEngine.h is changed without updating this table
// (or vice versa), this assert fires before a single byte of runtime
// code executes.  The cast silences a signed/unsigned comparison warning
// on SH-4 toolchains that treat array extents as signed.
static_assert(
    static_cast<uint8_t>(sizeof(kSongTable) / sizeof(kSongTable[0])) == 3,
    "kSongTable size must equal GameEngine::TOTAL_SONGS (currently 3). "
    "Update both SongIndex.cpp and GameEngine.h together."
);

} // namespace Engine::Charts
