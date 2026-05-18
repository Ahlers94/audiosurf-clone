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

    bool init(const PAL::PlatformBundle& bundle, uint8_t initialSongId) {
        s_graphics = bundle.graphics;
        s_audio    = bundle.audio;
        s_input    = bundle.input;

        if (!s_graphics || !s_audio || !s_input) return false;

        // Initialize the hardware abstraction implementations
        if (!s_graphics->init(640, 480)) return false;
        if (!s_audio->init()) return false;
        if (!s_input->init()) return false;

        // Boot straight into the retro title splash screen
        m_currentState = EngineState::TitleScreen;
        m_selectedSong = initialSongId;

        return true;
    }

    void tick() {
        // Uniform input polling at the top of every frame tick
        s_input->poll();
        PAL::InputState pressed = s_input->readPressedActions();

        switch (m_currentState) {
            case EngineState::TitleScreen:
                // Pressing any valid action controller button advances past the title
                if (pressed != 0) {
                    m_currentState = EngineState::SongSelect;
                }
                break;

            case EngineState::SongSelect:
                if (pressed & static_cast<uint8_t>(PAL::InputAction::LaneLeft)) {
                    if (m_selectedSong > 0) m_selectedSong--;
                }
                if (pressed & static_cast<uint8_t>(PAL::InputAction::LaneRight)) {
                    if (m_selectedSong < (TOTAL_SONGS - 1)) m_selectedSong++;
                }
                if (pressed & static_cast<uint8_t>(PAL::InputAction::Confirm)) {
                    // Start audio playback for the track index selection
                    s_audio->play(m_selectedSong);
                    m_currentState = EngineState::Gameplay;
                }
                break;

            case EngineState::Gameplay:
                updateGameplaySimulation(pressed);
                break;

            case EngineState::ResultsScreen:
                if (pressed & static_cast<uint8_t>(PAL::InputAction::Confirm)) {
                    m_currentState = EngineState::SongSelect; // Loop back to menu
                }
                break;
        }
    }

    void render() {
        s_graphics->beginFrame();

        switch (m_currentState) {
            case EngineState::TitleScreen:
                renderTitleScreen();
                break;

            case EngineState::SongSelect:
                renderSongSelectMenu();
                break;

            case EngineState::Gameplay:
                renderGameplayScene();
                break;

            case EngineState::ResultsScreen:
                renderResultsScreen();
                break;
        }

        s_graphics->endFrame();
    }

    bool isRunning() const { return m_isRunning; }

private:
    // Core Platform Interfaces
    PAL::GraphicsInterface* s_graphics = nullptr;
    PAL::AudioInterface*    s_audio    = nullptr;
    PAL::InputInterface*    s_input    = nullptr;

    // State Tracking
    EngineState m_currentState = EngineState::TitleScreen;
    uint8_t     m_selectedSong = 0;
    bool        m_isRunning    = true;

    static constexpr uint8_t TOTAL_SONGS = 3;
    static constexpr uint8_t PALETTE_SIZE = 8;

    // -----------------------------------------------------------------------
    // State Simulation / Sub-Ticks
    // -----------------------------------------------------------------------
    void updateGameplaySimulation(PAL::InputState pressed) {
        (void)pressed; // Hook this to your input timing evaluation arrays later

        PAL::FP16 progress = s_audio->getTrackProgress();
        
        // Track retirement boundaries check (0xFFFF signals song completion limits)
        if (progress >= 0xFFFF) {
            s_audio->stop();
            m_currentState = EngineState::ResultsScreen;
        }
    }

    // -----------------------------------------------------------------------
    // Procedural UI / Sub-Renderers
    // -----------------------------------------------------------------------
    void renderTitleScreen() {
        // Procedurally draw a massive structural geometric neon prompt in 3D viewport space
        s_graphics->drawVoxelColumnGlow(
            static_cast<PAL::SFP16>(120 << 8), static_cast<PAL::SFP16>(520 << 8),
            static_cast<PAL::SFP16>(200 << 8), static_cast<PAL::SFP16>(280 << 8),
            static_cast<PAL::SFP16>(5 << 8), 2, 255
        );
        // Display a string banner through HUD projection mappings
        s_graphics->drawHUD(0, 0, 0, 0); 
    }

    void renderSongSelectMenu() {
        // Render a clean horizontal selection matrix of scrolling blocks
        for (uint8_t i = 0; i < TOTAL_SONGS; ++i) {
            PAL::SFP16 xPos = static_cast<PAL::SFP16>((160 + (i * 160)) << 8);
            PAL::SFP16 yPos = static_cast<PAL::SFP16>(240 << 8);
            PAL::SFP16 zPos = static_cast<PAL::SFP16>(10 << 8);

            if (i == m_selectedSong) {
                // Bright, highly saturated bloom block for selected song entries
                s_graphics->drawBlock(xPos, yPos, zPos, i);
                s_graphics->drawVoxelColumnGlow(xPos - (15<<8), xPos + (15<<8), yPos - (30<<8), yPos + (30<<8), zPos, i, 160);
            } else {
                // Dimmed particles as low-overhead placeholding indicator columns
                s_graphics->drawParticle(xPos, yPos, zPos, i, 80);
            }
        }
    }

    void renderGameplayScene() {
        // Place your core 3D rhythmic column rendering iterations back here
        // Iterates through active track columns, lanes, and scrolling objects
        uint8_t energy = s_audio->getEnergyLevel();
        PAL::SFP16 height = static_cast<PAL::SFP16>(energy << 7);

        s_graphics->drawVoxelColumn(
            static_cast<PAL::SFP16>(200 << 8), static_cast<PAL::SFP16>(440 << 8),
            static_cast<PAL::SFP16>(0 << 8),   height,
            static_cast<PAL::SFP16>(20 << 8),  m_selectedSong
        );
    }

    void renderResultsScreen() {
        // Flat placeholder backdrop indicating completion pass states
        s_graphics->drawBlock(
            static_cast<PAL::SFP16>(320 << 8), static_cast<PAL::SFP16>(240 << 8),
            static_cast<PAL::SFP16>(15 << 8),  7
        );
    }
};

} // namespace Engine

#endif // GAME_ENGINE_H
