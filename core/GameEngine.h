#ifndef GAME_ENGINE_H
#define GAME_ENGINE_H

#include <stdint.h>
#include "PAL.h" // Ensure this path is 100% correct
#include "../charts/NoteChart.h"

// Bring PAL types into the current scope before the Engine namespace
using PAL::Graphics;
using PAL::Audio;
using PAL::Input;
using PAL::Clock;
using PAL::PlatformBundle;
using PAL::InputState;
using PAL::FP16;

namespace Engine {

// Engine State definitions
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
    bool init(const PlatformBundle& bundle, uint8_t initialSongId);
    void tick();
    void render();
    bool isRunning() const;
    void shutdown();

private:
    Graphics* s_graphics = nullptr;
    Audio*    s_audio    = nullptr;
    Input*    s_input    = nullptr;
    Clock*    s_clock    = nullptr;

    bool        m_isRunning = false;
    EngineState m_currentState;
    uint8_t     m_selectedSong;
    uint16_t    m_score = 0;
    uint16_t    m_combo = 0;
    uint16_t    m_missCount = 0;

    void updateGameplaySimulation(InputState pressed);
    FrameResult evaluateChart(FP16 trackPos, InputState pressed);
    
    // ... (rest of your helpers remain same)
};

} // namespace Engine
#endif // GAME_ENGINE_H
