#ifndef GAME_ENGINE_H
#define GAME_ENGINE_H

#include <stdint.h>

// Forward declaration to ensure header self-containment without include loops
struct NoteChart;

// Mock types representing your Platform Abstraction Layer (PAL)
namespace PAL {
    typedef uint16_t FP16;
    typedef int16_t  SFP16;
    typedef uint32_t InputState;

    enum class InputAction : uint32_t {
        Confirm = 1 << 0
    };

    struct Graphics {
        bool init(int width, int height);
        void beginFrame();
        void endFrame();
        void drawParticle(SFP16 x, SFP16 y, SFP16 size, uint8_t color, uint8_t alpha);
        void drawBlock(SFP16 x, SFP16 y, SFP16 z, uint8_t type);
        void drawVoxelColumn(SFP16 x1, SFP16 x2, SFP16 y1, SFP16 y2, SFP16 z, uint8_t songId);
        void drawVoxelColumnGlow(SFP16 x1, SFP16 x2, SFP16 y1, SFP16 y2, SFP16 z, uint8_t songId, uint8_t alpha);
        void drawHUD(uint16_t score, uint16_t combo, uint16_t miss, uint8_t flags);
    };

    struct Audio {
        bool init();
        void play(uint8_t songId);
        void stop();
        void setPaused(bool p);
        bool isPaused(); // Hardening Hook: Essential for drift-resistant evaluation layers
        uint16_t getTrackProgress();
        uint8_t getEnergyLevel();
    };

    struct Input {
        bool init();
        void poll();
        InputState readPressedActions();
        InputState readHeldActions();
        bool isHeld(InputAction a);
    };

    struct PlatformBundle {
        Graphics* graphics;
        Audio* audio;
        Input* input;
    };
}

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
    PAL::SFP16 posX;        // 2 bytes
    PAL::SFP16 posY;        // 2 bytes
    int16_t    velY;        // 2 bytes
    uint8_t    lifetime;    // 1 byte
    uint8_t    colorIndex;  // 1 byte
};                          // Total: 8 bytes (Perfect 32-bit boundary alignment)

struct alignas(4) Note {
    PAL::FP16 timeline;     // 2 bytes
    uint16_t  holdLength;   // 2 bytes
    uint8_t   lane;         // 1 byte
    uint8_t   flags;        // 1 byte
    uint8_t   _padding[2];  // 2 bytes (Hardware structure alignment padding)
};                          // Total: 8 bytes

struct alignas(2) NoteState {
    uint8_t   hitResult;    // 1 byte
    HoldState holdPhase;    // 1 byte
};                          // Total: 2 bytes

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
constexpr PAL::SFP16 LANE_HIT_Y = 400 << 8;

constexpr uint8_t  GRID_MAX_ROWS = 6;
constexpr PAL::SFP16 GRID_BASE_Y = 420 << 8;
constexpr PAL::SFP16 GRID_BLOCK_SPACING = 24 << 8;

static const PAL::SFP16 LANE_X[4] = { 240 << 8, 320 << 8, 400 << 8, 480 << 8 };
static const uint32_t LANE_MASKS[4] = { 1 << 0, 1 << 1, 1 << 2, 1 << 3 };

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

    bool init(const PAL::PlatformBundle& bundle, uint8_t initialSongId);
    void tick();
    void render();
    void shutdown() {} // Standard clear down hook mirroring main execution loops
    bool isRunning() const;

private:
    void loadChart(const NoteChart* chart);
    void resetScoreCounters();
    void resetPuzzleGrid();
    void spawnBurst(uint8_t lane, uint8_t count);
    void tickParticles();
    
    bool pushBlockToGrid(uint8_t lane, uint8_t blockType);
    void checkAndResolveMatches();
    
    FrameResult evaluateChart(PAL::FP16 trackPos, PAL::InputState pressed);
    void updateGameplaySimulation(PAL::InputState pressed);

    void renderParticles() const;
    void renderGameplayScene() const;
    void renderTitleScreen() const;
    void renderSongSelectMenu() const;
    void renderResultsScreen() const;

    // Fast inline branchless delta tracking
    static inline PAL::FP16 fp16AbsDelta(PAL::FP16 a, PAL::FP16 b) {
        int32_t d = static_cast<int32_t>(a) - static_cast<int32_t>(b);
        return static_cast<PAL::FP16>((d ^ (d >> 31)) - (d >> 31));
    }

    // --- PLATFORM POINTER INTERFACES ---
    PAL::Graphics* s_graphics = nullptr;
    PAL::Audio* s_audio    = nullptr;
    PAL::Input* s_input    = nullptr;

    // --- GAME ENGINE REGISTERS ---
    EngineState m_currentState = EngineState::TitleScreen;
    uint8_t     m_selectedSong = 0;
    bool        m_isRunning    = false;
    bool        m_isStreamingMode = false;
    
    uint16_t    m_score     = 0;
    uint16_t    m_combo     = 0;
    uint16_t    m_missCount = 0;

    // --- AUDIO LOOP CLOCK HARDENING REGISTERS ---
    PAL::FP16   m_lastHardwareTrackPos = 0;
    PAL::FP16   m_localTrackAccumulator = 0;
    uint8_t     m_syncTickCounter = 0;

    // --- OPTIMIZED MATCH-3 BIT-PACKED BOARD MATRIX ---
    uint16_t    m_puzzleGrid[3] = {0, 0, 0};
    uint8_t     m_gridHeights[3] = {0, 0, 0};

    // --- CACHE-FRIENDLY ZERO-ALLOCATION CONTIGUOUS MEMORY BLOCKS ---
    const NoteChart* m_activeChart = nullptr;
    uint16_t         m_readHead    = 0;
    
    NoteState        m_noteStates[MAX_NOTES_PER_CHART];
    Particle         m_particles[MAX_PARTICLES];

    // Ring buffer components for real-time live streaming mode calculations
    uint16_t         m_streamHead = 0;
    uint16_t         m_streamTail = 0;
    Note             m_streamingNotes[RING_BUFFER_SIZE];
    NoteState        m_streamingNoteStates[RING_BUFFER_SIZE];
};

} // namespace Engine

#endif // GAME_ENGINE_H
