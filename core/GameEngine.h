#ifndef GAME_ENGINE_H
#define GAME_ENGINE_H

#include "pal/PAL.h"
#include "charts/NoteChart.h" // Includes Note, NoteChart, and MAX_NOTES_PER_CHART definitions

namespace Engine {

// ─── Hold-gate state machine ──────────────────────────────────────────────────
// Strictly forward tracking: Inactive → Gating → Complete | DroppedEarly.
// No back-edges allowed; a dropped hold cannot recover within the same track timeline.
enum class HoldState : uint8_t {
    Inactive     = 0, // Tap note, or hold duration onset not yet reached
    Gating       = 1, // Lane button held down — gate is open, score accumulating
    Complete     = 2, // Maintained cleanly through the full holdLength span — success
    DroppedEarly = 3  // Lane released early before holdLength elapsed — miss register
};

// ─── Per-note runtime judgment record ─────────────────────────────────────────
// Parallel array matching the ROM-resident Note layout; only mutable states live here.
// 2 bytes × 512 entries = 1 KB — fits entirely inside a single SH-4 data-cache block.
struct NoteState {
    uint8_t  hitResult; // 0=pending, 1=perfect, 2=good, 3=miss, 4=ignored
    HoldState holdPhase;
};
static_assert(sizeof(NoteState) == 2, "NoteState must be exactly 2 bytes");

// ─── Streaming runtime judgment record ────────────────────────────────────────
// Parallel array matching the ring buffer tracking footprint.
struct StreamingNoteState {
    uint8_t hitResult;
    uint8_t padding; // Maintains 2-byte structure parity for aligned cache matching
};
static_assert(sizeof(StreamingNoteState) == 2, "StreamingNoteState must be exactly 2 bytes");

// ─── Particle Structure ───────────────────────────────────────────────────────
// 8 bytes (Aligned pair of 32-bit words) — fits two distinct slots per SH-4 cache line.
// posX/posY are signed Q8.8 screen-space coordinates.
struct alignas(4) Particle {
    PAL::SFP16 posX;
    PAL::SFP16 posY;
    int16_t    velY;
    uint8_t    lifetime;
    uint8_t    colorIndex; // Indexes into lane palette directories
};
static_assert(sizeof(Particle) == 8, "Particle must be exactly 8 bytes");

// ─── Frame-level judgment summary ─────────────────────────────────────────────
struct FrameResult {
    uint8_t perfectCount;
    uint8_t goodCount;
    uint8_t missCount;
    uint8_t holdDropCount;
    uint8_t activeHolds;
};

// ─── Judgment windows (unsigned Q8.8 FP16 timeline deltas) ───────────────────
static constexpr PAL::FP16 WINDOW_PERFECT = 0x0010; // ±~0.06 timeline units
static constexpr PAL::FP16 WINDOW_GOOD    = 0x0028; // ±~0.15 timeline units
static constexpr PAL::FP16 WINDOW_MISS    = 0x0050; // Forced-miss boundary threshold

// ─── Particle pool constants ──────────────────────────────────────────────────
static constexpr uint8_t MAX_PARTICLES  = 32;
static constexpr uint8_t BURST_COUNT    = 6;
static constexpr uint8_t PARTICLE_LIFE  = 16;   // Clean power-of-two to avoid division stalls
// Base upward velocity: -1.5 px/frame in Q8.8 format
static constexpr int16_t PARTICLE_VEL_Y = -0x0180;

// ─── Lane geometry (screen-space Q8.8 SFP16 configuration) ────────────────────
static constexpr PAL::SFP16 LANE_X[4] = {
    static_cast<PAL::SFP16>(100 << 8),
    static_cast<PAL::SFP16>(220 << 8),
    static_cast<PAL::SFP16>(340 << 8),
    static_cast<PAL::SFP16>(460 << 8),
};
static constexpr PAL::SFP16 LANE_HIT_Y = static_cast<PAL::SFP16>(400 << 8);

// ─── Bitwise lane-to-button mask index catalog ──────────────────────────────
static constexpr PAL::InputState LANE_MASKS[4] = {
    static_cast<PAL::InputState>(PAL::InputAction::LaneLeft),
    static_cast<PAL::InputState>(PAL::InputAction::LaneRight),
    static_cast<PAL::InputState>(PAL::InputAction::LaneUp),
    static_cast<PAL::InputState>(PAL::InputAction::LaneDown),
};

enum class EngineState : uint8_t {
    TitleScreen,
    SongSelect,
    Gameplay,
    ResultsScreen
};

// ─── GameEngine Main Class Architecture ───────────────────────────────────────
class GameEngine {
public:
    GameEngine()  = default;
    ~GameEngine() = default;

    bool init(const PAL::PlatformBundle& bundle, uint8_t initialSongId);
    void tick();
    void render();
    bool isRunning() const;

private:
    // PAL Layer Target Interfaces
    PAL::GraphicsInterface* s_graphics = nullptr;
    PAL::AudioInterface* s_audio    = nullptr;
    PAL::InputInterface* s_input    = nullptr;

    // Core System Status Regs
    EngineState m_currentState = EngineState::TitleScreen;
    uint8_t     m_selectedSong = 0;
    bool        m_isRunning    = true;

    // 3 Fixed ROM Tracks + 1 Live Streaming Audio CD Track
    static constexpr uint8_t TOTAL_SONGS = 4;

    // Gameplay Score Tracking Accumulators
    uint16_t m_score     = 0;
    uint16_t m_combo     = 0;
    uint16_t m_missCount = 0;

    // Static Album/Pre-Made Chart Buffers
    const NoteChart* m_activeChart = nullptr;
    NoteState        m_noteStates[MAX_NOTES_PER_CHART];
    uint16_t         m_readHead = 0;

    // ─── HYBRID MATCH-3 & STREAMING CONFIGURATIONS ───────────────────────────
    static constexpr uint8_t  GRID_MAX_ROWS      = 6;
    static constexpr uint16_t GRID_BASE_Y        = 400 << 8; // Screen coordinate alignment anchor
    static constexpr uint16_t GRID_BLOCK_SPACING = 16 << 8;  // Height delta spacing between stack rows
    static constexpr uint8_t  CD_STREAM_TRACK_ID = 99;       // Song selection index flag forcing live CD route

    static constexpr uint16_t RING_BUFFER_SIZE   = 128;      // Power of 2 required for fast bitmask operations
    static constexpr uint16_t RING_BUFFER_MASK   = 0x007F;   // RING_BUFFER_SIZE - 1 wrap mask

    bool     m_isStreamingMode = false;
    uint16_t m_streamHead      = 0; // Read-index pointer boundary
    uint16_t m_streamTail      = 0; // Write-index pointer boundary (driven by audio thread)

    // Parallel ring arrays for dynamic CD note tracking
    Note               m_streamingNotes[RING_BUFFER_SIZE];
    StreamingNoteState m_streamingNoteStates[RING_BUFFER_SIZE];

    // Array-backed Puzzle Board State Tracking Structures
    uint8_t m_puzzleGrid[3][GRID_MAX_ROWS]; // Stores lane color blocks (1, 2, or 3=Grey)
    uint8_t m_gridHeights[3];               // Current block count tracking per active lane

    // Contiguous Particle Stack Storage
    Particle m_particles[MAX_PARTICLES];

    // Simulation Engine Functions (Write access boundaries to state tracking arrays)
    void loadChart(const NoteChart* chart);
    void resetScoreCounters();
    void resetPuzzleGrid();
    void updateGameplaySimulation(PAL::InputState pressed);

    FrameResult evaluateChart(PAL::FP16 trackPos, PAL::InputState pressed);
    bool pushBlockToGrid(uint8_t lane, uint8_t blockType);
    void checkAndResolveMatches();

    void spawnBurst(uint8_t lane, uint8_t count);
    void tickParticles(); 

    // Render Generation Pipeline (Strictly Read-Only Constraints)
    void renderParticles()      const; 
    void renderTitleScreen()    const;
    void renderSongSelectMenu() const;
    void renderGameplayScene()  const; 
    void renderResultsScreen()  const;

    // Hardware Optimized Math Utilities
    static PAL::FP16 fp16AbsDelta(PAL::FP16 a, PAL::FP16 b);
};

} // namespace Engine

#endif // GAME_ENGINE_H
