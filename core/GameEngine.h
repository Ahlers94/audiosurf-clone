#ifndef GAME_ENGINE_H
#define GAME_ENGINE_H

#include <stdint.h>
#include "PAL.h"
#include "../charts/NoteChart.h"

// Define the class and its dependencies at the top level to avoid conflicts
class GameEngine; 

namespace Engine {

enum class EngineState {
    TitleScreen,
    SongSelect,
    Gameplay,
    ResultsScreen
};

struct NoteState { uint8_t hitResult; };
struct FrameResult { uint16_t perfectCount; uint16_t missCount; uint16_t holdDropCount; };

class GameEngine {
public:
    // Use the namespace directly without :: or Engine:: prefixes
    bool init(const PAL::PlatformBundle& bundle, uint8_t initialSongId);
    void tick();
    void render();
    bool isRunning() const;
    void shutdown();

private:
    PAL::Graphics* s_graphics = nullptr;
    PAL::Audio*    s_audio    = nullptr;
    PAL::Input*    s_input    = nullptr;
    PAL::Clock*    s_clock    = nullptr;

    bool        m_isRunning = false;
    EngineState m_currentState;
    uint8_t     m_selectedSong;
    uint16_t    m_score = 0;
    uint16_t    m_combo = 0;
    uint16_t    m_missCount = 0;

    void updateGameplaySimulation(PAL::InputState pressed);
    FrameResult evaluateChart(PAL::FP16 trackPos, PAL::InputState pressed);
    
    // ... rest of your helpers ...
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
