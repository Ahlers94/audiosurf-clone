#ifndef GAME_ENGINE_H
#define GAME_ENGINE_H

#include <stdint.h>
#include "PAL.h"
#include "../charts/NoteChart.h"

namespace Engine {

// =============================================================================
// Constants
// =============================================================================

constexpr uint8_t  MAX_PARTICLES        = 16;
constexpr uint8_t  TOTAL_SONGS          = 5;
constexpr uint8_t  CD_STREAM_TRACK_ID   = 255;
constexpr uint16_t RING_BUFFER_MASK     = 0x00FF;   // 256-entry ring
constexpr uint16_t STREAMING_NOTES_MAX  = 256;       // must be power of 2
constexpr PAL::FP16 WINDOW_MISS         = 0x0500;
constexpr uint8_t  BURST_COUNT          = 6;
constexpr uint8_t  GRID_MAX_ROWS        = 6;
constexpr uint8_t  GRID_BLOCK_SPACING   = 18;
constexpr PAL::SFP16 GRID_BASE_Y        = static_cast<PAL::SFP16>(400 << 8);

// Declared here; defined once in GameEngine.cpp
extern const PAL::SFP16 LANE_X[3];
extern const PAL::SFP16 LANE_HIT_Y;
extern const uint8_t    LANE_MASKS[3];

// =============================================================================
// Types
// =============================================================================

enum class EngineState {
    TitleScreen,
    SongSelect,
    Gameplay,
    ResultsScreen
};

struct NoteState {
    uint8_t hitResult;  // 0 = pending, 1 = perfect, 3 = miss
};

struct FrameResult {
    uint16_t perfectCount;
    uint16_t missCount;
    uint16_t holdDropCount;
};

// Particle: positional + velocity fields used by tickParticles()
struct Particle {
    PAL::SFP16 posX;
    PAL::SFP16 posY;
    PAL::SFP16 velY;
    uint16_t   lifetime;
    uint8_t    colorIndex;
};

// =============================================================================
// GameEngine
// =============================================================================

class GameEngine {
public:
    bool init(const PAL::PlatformBundle& bundle, uint8_t initialSongId);
    void tick();
    void render();
    bool isRunning() const;
    void shutdown();

private:
    // -------------------------------------------------------------------------
    // Platform abstraction layer
    // -------------------------------------------------------------------------
    PAL::GraphicsInterface* s_graphics = nullptr;
    PAL::AudioInterface*    s_audio    = nullptr;
    PAL::InputInterface*    s_input    = nullptr;
    PAL::ClockInterface*    s_clock    = nullptr;

    // -------------------------------------------------------------------------
    // Engine lifecycle
    // -------------------------------------------------------------------------
    bool        m_isRunning    = false;
    EngineState m_currentState = EngineState::TitleScreen;
    uint8_t     m_selectedSong = 0;

    // -------------------------------------------------------------------------
    // Scoring
    // -------------------------------------------------------------------------
    uint16_t m_score     = 0;
    uint16_t m_combo     = 0;
    uint16_t m_missCount = 0;

    // -------------------------------------------------------------------------
    // Gameplay / camera
    // -------------------------------------------------------------------------
    PAL::FP16 m_cameraZ               = 0;
    PAL::FP16 m_localTrackAccumulator = 0;
    uint8_t   m_shipLane              = 1;

    // Dreamcast-only sync counter; harmless on other platforms
    uint32_t  m_syncTickCounter = 0;

    // -------------------------------------------------------------------------
    // Chart / streaming playback
    // -------------------------------------------------------------------------
    const NoteChart* m_activeChart     = nullptr;
    uint16_t         m_readHead        = 0;

    bool     m_isStreamingMode         = false;
    Note     m_streamingNotes[STREAMING_NOTES_MAX];
    NoteState m_streamingNoteStates[STREAMING_NOTES_MAX];
    uint16_t m_streamHead              = 0;
    uint16_t m_streamTail              = 0;

    NoteState m_noteStates[MAX_NOTES_PER_CHART];

    // -------------------------------------------------------------------------
    // Puzzle grid (3 lanes × GRID_MAX_ROWS rows, bit-packed, 3 bits per cell)
    // -------------------------------------------------------------------------
    uint16_t m_puzzleGrid[3]   = {0, 0, 0};
    uint8_t  m_gridHeights[3]  = {0, 0, 0};

    // -------------------------------------------------------------------------
    // Particles
    // -------------------------------------------------------------------------
    Particle m_particles[MAX_PARTICLES];

    // -------------------------------------------------------------------------
    // Private methods — gameplay
    // -------------------------------------------------------------------------
    void        updateGameplaySimulation(PAL::InputState pressed);
    FrameResult evaluateChart(PAL::FP16 trackPos, PAL::InputState pressed);

    // -------------------------------------------------------------------------
    // Private methods — chart management
    // -------------------------------------------------------------------------
    void loadChart(const NoteChart* chart);
    void resetScoreCounters();

    // -------------------------------------------------------------------------
    // Private methods — puzzle grid
    // -------------------------------------------------------------------------
    void resetPuzzleGrid();
    bool pushBlockToGrid(uint8_t lane, uint8_t blockType);
    void checkAndResolveMatches();

    // -------------------------------------------------------------------------
    // Private methods — particles
    // -------------------------------------------------------------------------
    void spawnBurst(uint8_t lane, uint8_t count);
    void tickParticles();
    void renderParticles();

    // -------------------------------------------------------------------------
    // Private methods — rendering
    // -------------------------------------------------------------------------
    void renderTitleScreen()    const;
    void renderSongSelectMenu() const;
    void renderGameplayScene();
    void renderResultsScreen()  const;
};

} // namespace Engine

#endif // GAME_ENGINE_H
