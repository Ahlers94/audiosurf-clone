#ifndef GAME_ENGINE_H
#define GAME_ENGINE_H

#include <stdint.h>
#include "PAL.h"
#include "../charts/NoteChart.h"

namespace Engine {

// Constants
constexpr uint8_t MAX_PARTICLES = 16;
constexpr uint8_t TOTAL_SONGS = 5; 
constexpr uint8_t CD_STREAM_TRACK_ID = 255;
extern const uint8_t LANE_MASKS[3];

// Definitions
enum class EngineState {
    TitleScreen,
    SongSelect,
    Gameplay,
    ResultsScreen
};

struct NoteState { uint8_t hitResult; };
struct FrameResult { uint16_t perfectCount; uint16_t missCount; uint16_t holdDropCount; };

struct Particle {
    PAL::FP16 x, y, z;
    uint16_t lifetime;
    uint8_t colorIndex;
};

class GameEngine {
public:
    bool init(const PAL::PlatformBundle& bundle, uint8_t initialSongId);
    void tick();
    void render();
    bool isRunning() const;
    void shutdown();

private:
    // Core Engine Pointers
    PAL::GraphicsInterface* s_graphics = nullptr;
    PAL::AudioInterface*    s_audio    = nullptr;
    PAL::InputInterface*    s_input    = nullptr;
    PAL::ClockInterface*    s_clock    = nullptr;

    // Simulation State
    bool        m_isRunning = false;
    EngineState m_currentState = EngineState::TitleScreen;
    uint8_t     m_selectedSong = 0;
    uint16_t    m_score = 0;
    uint16_t    m_combo = 0;
    uint16_t    m_missCount = 0;
    
    // Gameplay / Physics members
    PAL::FP16 m_cameraZ = 0;
    PAL::FP16 m_localTrackAccumulator = 0;
    uint8_t   m_shipLane = 1;
    Particle  m_particles[MAX_PARTICLES];

    void updateGameplaySimulation(PAL::InputState pressed);
    FrameResult evaluateChart(PAL::FP16 trackPos, PAL::InputState pressed);
    
    void resetPuzzleGrid();
    void loadChart(const NoteChart* chart);
    void resetScoreCounters();
    void tickParticles();
    void renderParticles();
    bool pushBlockToGrid(uint8_t lane, uint8_t blockType);
    void checkAndResolveMatches();
    void spawnBurst(uint8_t lane, uint8_t count);
    
    void renderTitleScreen() const;
    void renderSongSelectMenu() const;
    void renderGameplayScene();
    void renderResultsScreen() const;
};

} // namespace Engine
#endif // GAME_ENGINE_H
