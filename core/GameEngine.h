#ifndef GAME_ENGINE_H
#define GAME_ENGINE_H

#include "pal/PAL.h"

namespace Engine {

enum class EngineState : uint8_t {
    TitleScreen,
    SongSelect,
    Gameplay,
    ResultsScreen
};

// --- TIMING JUDGMENT REGISTER SCHEMES ---
enum class Judgment : uint8_t { Pending, Perfect, Good, Miss, Ignored };
enum class HoldState : uint8_t { Inactive, Holding, ReleasedEarly, Complete };

// --- HARD PACKED STRUCT SEGMENTATION (8-Bytes, 32-Bit Aligned Boundary) ---
struct alignas(4) Note {
    PAL::FP16  timeline;    // [0x0000 -> 0xFFFF] Unsigned Q8.8 Track Position
    PAL::FP16  holdLength;  // 0x0000 = Tap Note, >0 = Sustained Hold Delta Gate
    uint8_t    lane;        // 0 to 3 Mapping (Left, Right, Up, Down)
    uint8_t    flags;       // Bitwise: Bit 0 = Accented, Bit 1 = Kinetic Modifier
    uint8_t    _reserved[2];// Clean zero pad protecting SH-4 compiler alignment
};

struct NoteState {
    Judgment  hitResult;    // Operational tracking status
    HoldState holdPhase;    // Active phase tracking mechanics
};

struct NoteChart {
    const Note* notes;
    uint16_t    noteCount;
    uint8_t     songId;
    uint8_t     bpmHint;
};

class GameEngine {
public:
    GameEngine() = default;
    ~GameEngine() = default;

    bool init(const PAL::PlatformBundle& bundle, uint8_t initialSongId);
    void tick();
    void render();
    bool isRunning() const;

private:
    PAL::GraphicsInterface* s_graphics = nullptr;
    PAL::AudioInterface*    s_audio    = nullptr;
    PAL::InputInterface*    s_input    = nullptr;

    EngineState m_currentState = EngineState::TitleScreen;
    uint8_t     m_selectedSong = 0;
    bool        m_isRunning    = true;

    static constexpr uint8_t TOTAL_SONGS = 3;
    static constexpr uint8_t MAX_LANES   = 4;
    static constexpr uint16_t MAX_NOTES_PER_CHART = 512;

    // --- ENCAPSULATED WORKING GAMEPLAY STATE SEGMENTS ---
    const NoteChart* m_activeChart = nullptr;
    NoteState        m_noteStates[MAX_NOTES_PER_CHART];
    uint16_t         m_readHead = 0;

    // Game Metrics Tracking
    uint32_t m_score     = 0;
    uint16_t m_combo     = 0;
    uint16_t m_missCount = 0;

    // Deterministic Timing Windows (Calculated in Unsigned Q8.8 Format)
    static constexpr PAL::FP16 WINDOW_PERFECT = 0x00A0; // ~60ms
    static constexpr PAL::FP16 WINDOW_GOOD    = 0x0140; // ~120ms
    static constexpr PAL::FP16 WINDOW_MISS    = 0x0200; // ~180ms Boundary Threshold

    void updateGameplaySimulation(PAL::InputState pressed);
    void resetGameplayTrack(uint8_t songId);

    void renderTitleScreen();
    void renderSongSelectMenu();
    void renderGameplayScene();
    void renderResultsScreen();
};

} // namespace Engine

#endif // GAME_ENGINE_H
