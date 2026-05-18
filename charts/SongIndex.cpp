#include "charts/SongIndex.h"

// This is a companion to the header — kept separate so the static_assert
// can see TOTAL_SONGS from GameEngine.h without a circular include.
// SongIndex.cpp is the ONLY file that defines kSongTable.

namespace Engine::Charts {

// ── Table definition ──────────────────────────────────────────────────────────
// Initialiser order must match the extern declarations in SongIndex.h.
// The const-on-pointer-and-pointee means neither the table slots nor the
// chart structs they point to can be mutated at runtime.
//
// Slots [1] and [2] point to kSong00_Chart as placeholders until your custom 
// tracker tool populates Song01.cpp and Song02.cpp.
// Slot [3] is explicitly nullptr to signal CD_STREAM_TRACK_ID (Streaming Live Audio).
const NoteChart* const kSongTable[4] = {
    &kSong00_Chart,
    &kSong00_Chart, // Placeholder track allocation
    &kSong00_Chart, // Placeholder track allocation
    nullptr         // Live streaming CD-DA routing flag (Triggers m_isStreamingMode = true)
};

// ── Compile-time size guard ───────────────────────────────────────────────────
// If TOTAL_SONGS in GameEngine.h is changed without updating this table
// (or vice versa), this assert fires before a single byte of runtime
// code executes.  The cast silences a signed/unsigned comparison warning
// on SH-4 toolchains that treat array extents as signed.
static_assert(
    static_cast<uint8_t>(sizeof(kSongTable) / sizeof(kSongTable[0])) == 4,
    "kSongTable size must equal GameEngine::TOTAL_SONGS (currently 4). "
    "Update both SongIndex.cpp and GameEngine.h together."
);

} // namespace Engine::Charts
