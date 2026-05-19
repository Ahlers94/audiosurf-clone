// =============================================================================
// GameEngine.h
// Core Match-3 Audio-Rhythm Engine Logic Pipeline
// =============================================================================

#ifndef GAME_ENGINE_H
#define GAME_ENGINE_H

#include <stdint.h>
#include "PAL.h"

struct NoteChart;

namespace Engine {

enum class EngineState : uint8_t {
    TitleScreen,
    SongSelect,
    Gameplay,
    ResultsScreen
};

enum class HoldState : uint8_t {
    Inactive,
    Gating,
    Complete,
    DroppedEarly
};

// --- CACHE-ALIGNED PACKED STRUCTS ---

struct alignas(4) Particle {
    Engine::PAL::SFP16 posX;
    Engine::PAL::SFP16 posY;
    int16_t          velY;
    uint8_t          lifetime;
    uint8_t          colorIndex;
};

struct alignas(4) Note {
    Engine::PAL::FP16   timeline;
    uint16_t            holdLength;
    uint8_t             lane;
    uint8_t             flags;
    uint8_t             _padding[2];
};

struct alignas(2) NoteState {
    uint8_t  hitResult;
    HoldState holdPhase;
};

// --- CONFIGURATION CONSTANTS ---
constexpr uint8_t  MAX_PARTICLES = 32;
constexpr uint16_t MAX_NOTES_PER_CHART = 512;
constexpr uint16_t RING_BUFFER_SIZE = 64;
constexpr uint16_t RING_BUFFER_MASK = RING_BUFFER_SIZE - 1;
constexpr uint8_t  TOTAL_SONGS = 8;
constexpr uint8_t  CD_STREAM_TRACK_ID = 7;

constexpr uint16_t WINDOW_PERFECT = 0x0180;
constexpr uint16_t WINDOW_GOOD    = 0x0400;
constexpr uint16_t WINDOW_MISS    = 0x0600;

constexpr uint8_t  BURST_COUNT   = 8;
constexpr uint8_t  PARTICLE_LIFE = 16;
constexpr int16_t  PARTICLE_VEL_Y = -120;
constexpr Engine::PAL::SFP16 LANE_HIT_Y = 400 << 8;

constexpr uint8_t  GRID_MAX_ROWS = 6;
constexpr Engine::PAL::SFP16 GRID_BASE_Y = 420 << 8;
constexpr Engine::PAL::SFP16 GRID_BLOCK_SPACING = 24 << 8;

static const Engine::PAL::SFP16 LANE_X[3] = { 240 << 8, 320 << 8, 400 << 8 };
static const uint32_t LANE_MASKS[3] = { 1 << 0, 1 << 1, 1 << 2 };

struct FrameResult {
    uint8_t perfectCount;
    uint8_t goodCount;
    uint8_t missCount;
    uint8_t holdDropCount;
    uint8_t activeHolds;
};

class GameEngine {
public:
    GameEngine() : m_isRunning(false), m_activeChart(nullptr) {}
    ~GameEngine() = default;

    bool init(const Engine::PAL::PlatformBundle& bundle, uint8_t initialSongId);
    void tick();
    void render();
    void shutdown() {} 
    bool isRunning() const { return m_isRunning; }

private:
    void loadChart(const NoteChart* chart);
    void resetScoreCounters();
    void resetPuzzleGrid();
    void spawnBurst(uint8_t lane, uint8_t count);
    void tickParticles();
    
    bool pushBlockToGrid(uint8_t lane, uint8_t blockType);
    void checkAndResolveMatches();
    
    FrameResult evaluateChart(Engine::PAL::FP16 trackPos, Engine::PAL::InputState pressed);
    void updateGameplaySimulation(Engine::PAL::InputState pressed);

    void renderParticles() const;
    void renderGameplayScene() const;
    void renderTitleScreen() const;
    void renderSongSelectMenu() const;
    void renderResultsScreen() const;

    static inline Engine::PAL::FP16 fp16AbsDelta(Engine::PAL::FP16 a, Engine::PAL::FP16 b) {
        int32_t d = static_cast<int32_t>(a) - static_cast<int32_t>(b);
        return static_cast<Engine::PAL::FP16>((d ^ (d >> 31)) - (d >> 31));
    }

    // --- PAL INTERFACES ---
    Engine::PAL::GraphicsInterface* s_graphics = nullptr;
    Engine::PAL::AudioInterface*    s_audio    = nullptr;
    Engine::PAL::InputInterface*    s_input    = nullptr;
    Engine::PAL::ClockInterface*    s_clock    = nullptr; // Integrated PAL Clock

    // --- ENGINE STATE ---
    EngineState m_currentState = EngineState::TitleScreen;
    uint8_t     m_selectedSong = 0;
    bool        m_isRunning    = false;
    bool        m_isStreamingMode = false;
    
    uint32_t    m_score     = 0;
    uint16_t    m_combo     = 0;
    uint16_t    m_missCount = 0;

    // --- CLOCK & CAMERA REGISTERS ---
    Engine::PAL::FP16   m_localTrackAccumulator = 0;
    Engine::PAL::FP16   m_cameraZ = 0; 

    // --- PUZZLE STATE ---
    uint32_t    m_puzzleGrid[3] = {0, 0, 0};
    uint8_t     m_gridHeights[3] = {0, 0, 0};

    // --- CHART DATA ---
    const NoteChart* m_activeChart = nullptr;
    uint16_t         m_readHead    = 0;
    
    NoteState        m_noteStates[MAX_NOTES_PER_CHART];
    Particle         m_particles[MAX_PARTICLES];

    // --- STREAMING BUFFER ---
    uint16_t         m_streamHead = 0;
    uint16_t         m_streamTail = 0;
    Note             m_streamingNotes[RING_BUFFER_SIZE];
    NoteState        m_streamingNoteStates[RING_BUFFER_SIZE];
};

} // namespace Engine

#endif // GAME_ENGINE_H
