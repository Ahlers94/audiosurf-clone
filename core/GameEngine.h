#ifndef GAME_ENGINE_H
#define GAME_ENGINE_H

#include <stdint.h>
#include "PAL.h"
#include "../charts/NoteChart.h"

namespace Engine {

// 1. DEFINITIONS FIRST
enum class EngineState {
    TitleScreen,
    SongSelect,
    Gameplay,
    ResultsScreen
};

struct NoteState { uint8_t hitResult; };
struct FrameResult { uint16_t perfectCount; uint16_t missCount; uint16_t holdDropCount; };

// 2. CLASS DEFINITION SECOND
class GameEngine {
public:
    bool init(const PAL::PlatformBundle& bundle, uint8_t initialSongId);
    void tick();
    void render();
    bool isRunning() const;
    void shutdown();

private:
    PAL::GraphicsInterface* s_graphics = nullptr;
    PAL::AudioInterface*    s_audio    = nullptr;
    PAL::InputInterface*    s_input    = nullptr;
    PAL::ClockInterface*    s_clock    = nullptr;

    bool        m_isRunning = false;
    EngineState m_currentState; // Now recognized
    uint8_t     m_selectedSong;
    uint16_t    m_score = 0;
    uint16_t    m_combo = 0;
    uint16_t    m_missCount = 0;

    void updateGameplaySimulation(PAL::InputState pressed);
    FrameResult evaluateChart(PAL::FP16 trackPos, PAL::InputState pressed); // Now recognized
    
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
