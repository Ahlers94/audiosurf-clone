#ifndef GAME_ENGINE_H
#define GAME_ENGINE_H

#include <stdint.h>
#include "PAL.h"
#include "../charts/NoteChart.h"

namespace Engine {

// Since PAL is inside Engine, we don't need ::PAL or PAL::
// we can just use PAL:: directly because we are already in the Engine scope.

class GameEngine {
public:
    // Lifecycle
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
    EngineState m_currentState;
    uint8_t     m_selectedSong;
    uint16_t    m_score = 0;
    uint16_t    m_combo = 0;
    uint16_t    m_missCount = 0;

    // Gameplay members
    void updateGameplaySimulation(PAL::InputState pressed);
    FrameResult evaluateChart(PAL::FP16 trackPos, PAL::InputState pressed);
    
    // Helpers
    void resetPuzzleGrid();
    void loadChart(const NoteChart* chart);
    void resetScoreCounters();
    void tickParticles();
    void renderParticles();
    bool pushBlockToGrid(uint8_t lane, uint8_t blockType);
    void checkAndResolveMatches();
    void spawnBurst(uint8_t lane, uint8_t count);
    
    // UI/Render helpers
    void renderTitleScreen() const;
    void renderSongSelectMenu() const;
    void renderGameplayScene();
    void renderResultsScreen() const;
};

} // namespace Engine
#endif // GAME_ENGINE_H
