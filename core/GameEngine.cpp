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

class GameEngine {
public:
    GameEngine() = default;
    ~GameEngine() = default;

    // Core lifecycle methods called by main() in platform layers
    bool init(const PAL::PlatformBundle& bundle, uint8_t initialSongId);
    void tick();
    void render();
    bool isRunning() const;

private:
    // Platform Interfaces
    PAL::GraphicsInterface* s_graphics = nullptr;
    PAL::AudioInterface*    s_audio    = nullptr;
    PAL::InputInterface*    s_input    = nullptr;

    // State Tracking Variables
    EngineState m_currentState = EngineState::TitleScreen;
    uint8_t     m_selectedSong = 0;
    bool        m_isRunning    = true;

    static constexpr uint8_t TOTAL_SONGS = 3;

    // Internal Simulation Sub-Ticks (Implemented in .cpp)
    void updateGameplaySimulation(PAL::InputState pressed);

    // Procedural UI Rendering Channels (Implemented in .cpp)
    void renderTitleScreen();
    void renderSongSelectMenu();
    void renderGameplayScene();
    void renderResultsScreen();
};

} // namespace Engine

#endif // GAME_ENGINE_H
