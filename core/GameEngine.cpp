#include "GameEngine.h"

namespace Engine {

// =============================================================================
// COMPILATION STATIC LINKER CHART DIRECTORIES
// =============================================================================
static const Note S_SONG_0_NOTES[] = {
    { 0x0800, 0x0000, 0, 0x00, {0,0} },
    { 0x0800, 0x0000, 3, 0x00, {0,0} }, // Chord Test: Simultaneous Notes on Lane 0 and 3
    { 0x1000, 0x0000, 1, 0x00, {0,0} },
    { 0x2000, 0x0400, 2, 0x01, {0,0} }  // Hold note configuration
};

static const NoteChart S_MASTER_CHART_CATALOG[TOTAL_SONGS] = {
    { S_SONG_0_NOTES, sizeof(S_SONG_0_NOTES) / sizeof(Note), 0, 128 },
    { nullptr, 0, 1, 120 }, // Placeholder for Song 1
    { nullptr, 0, 2, 140 }  // Placeholder for Song 2
};

// =============================================================================
// LIFECYCLE & STATE RESTORE SYSTEMS
// =============================================================================

bool GameEngine::init(const PAL::PlatformBundle& bundle, uint8_t initialSongId) {
    s_graphics = bundle.graphics;
    s_audio    = bundle.audio;
    s_input    = bundle.input;

    if (!s_graphics || !s_audio || !s_input) return false;
    if (!s_graphics->init(640, 480)) return false;
    if (!s_audio->init()) return false;
    if (!s_input->init()) return false;

    m_currentState = EngineState::TitleScreen;
    m_selectedSong = initialSongId;
    return true;
}

void GameEngine::resetGameplayTrack(uint8_t songId) {
    m_activeChart = &S_MASTER_CHART_CATALOG[songId];
    m_readHead    = 0;
    m_score       = 0;
    m_combo       = 0;
    m_missCount   = 0;

    // Reset runtime parallel RAM block fields instantly
    for (uint16_t i = 0; i < MAX_NOTES_PER_CHART; ++i) {
        m_noteStates[i].hitResult = Judgment::Pending;
        m_noteStates[i].holdPhase = HoldState::Inactive;
    }

    s_audio->play(songId);
}

void GameEngine::tick() {
    s_input->poll();
    PAL::InputState pressed = s_input->readPressedActions();

    switch (m_currentState) {
        case EngineState::TitleScreen:
            if (pressed != 0) m_currentState = EngineState::SongSelect;
            break;

        case EngineState::SongSelect:
            if (pressed & static_cast<uint8_t>(PAL::InputAction::LaneLeft)) {
                if (m_selectedSong > 0) m_selectedSong--;
            }
            if (pressed & static_cast<uint8_t>(PAL::InputAction::LaneRight)) {
                if (m_selectedSong < (TOTAL_SONGS - 1)) m_selectedSong++;
            }
            if (pressed & static_cast<uint8_t>(PAL::InputAction::Confirm)) {
                resetGameplayTrack(m_selectedSong);
                m_currentState = EngineState::Gameplay;
            }
            break;

        case EngineState::Gameplay:
            updateGameplaySimulation(pressed);
            break;

        case EngineState::ResultsScreen:
            if (pressed & static_cast<uint8_t>(PAL::InputAction::Confirm)) {
                m_currentState = EngineState::SongSelect;
            }
            break;
    }
}

// =============================================================================
// ROBUST EVALUATION EVALUATOR ENGINE LOOP
// =============================================================================

void GameEngine::updateGameplaySimulation(PAL::InputState pressed) {
    PAL::FP16 progress = static_cast<PAL::FP16>(s_audio->getTrackProgress());

    if (progress >= 0xFFFF || m_activeChart->notes == nullptr) {
        s_audio->stop();
        m_currentState = EngineState::ResultsScreen;
        return;
    }

    uint16_t count = m_activeChart->noteCount;
    const Note* notes = m_activeChart->notes;

    // -------------------------------------------------------------------------
    // PASS 1: PROGRESS THE TRACK HEAD & REAP EXPIRED MISSES
    // -------------------------------------------------------------------------
    while (m_readHead < count && progress > notes[m_readHead].timeline && 
          (progress - notes[m_readHead].timeline) > WINDOW_MISS) 
    {
        if (m_noteStates[m_readHead].hitResult == Judgment::Pending) {
            m_noteStates[m_readHead].hitResult = Judgment::Miss;
            m_missCount++;
            m_combo = 0;
        }
        m_readHead++; // Safely increment now that expiration threshold limits pass
    }

    // -------------------------------------------------------------------------
    // PASS 2: PROCESS THE LOCAL JUDGMENT VIEWPORT (Handles Multi-Lane Chords)
    // -------------------------------------------------------------------------
    uint16_t scanIdx = m_readHead;
    while (scanIdx < count && notes[scanIdx].timeline <= (progress + WINDOW_MISS)) {
        
        if (m_noteStates[scanIdx].hitResult == Judgment::Pending) {
            PAL::FP16 noteTime = notes[scanIdx].timeline;
            uint16_t diff = (progress > noteTime) ? (progress - noteTime) : (noteTime - progress);

            // Verify if target match lane input triggers action
            uint8_t laneMask = (1 << notes[scanIdx].lane);
            if (pressed & laneMask) {
                
                if (diff <= WINDOW_PERFECT) {
                    m_noteStates[scanIdx].hitResult = Judgment::Perfect;
                    m_score += 300;
                    m_combo++;
                } else if (diff <= WINDOW_GOOD) {
                    m_noteStates[scanIdx].hitResult = Judgment::Good;
                    m_score += 100;
                    m_combo++;
                } else if (diff <= WINDOW_MISS) {
                    m_noteStates[scanIdx].hitResult = Judgment::Miss;
                    m_combo = 0;
                    m_missCount++;
                }
            }
        }
        scanIdx++;
    }
}

// =============================================================================
// HARDENED HIGHWAY SCENE RENDERING
// =============================================================================

void GameEngine::render() {
    s_graphics->beginFrame();

    switch (m_currentState) {
        case EngineState::TitleScreen:   renderTitleScreen();    break;
        case EngineState::SongSelect:    renderSongSelectMenu(); break;
        case EngineState::Gameplay:      renderGameplayScene();  break;
        case EngineState::ResultsScreen: renderResultsScreen();  break;
    }

    s_graphics->endFrame();
}

void GameEngine::renderGameplayScene() {
    PAL::FP16 progress = static_cast<PAL::FP16>(s_audio->getTrackProgress());
    uint8_t energy = s_audio->getEnergyLevel();

    // 1. Draw Static Lane Guide Infrastructure
    for (uint8_t lane = 0; lane < MAX_LANES; ++lane) {
        PAL::SFP16 laneX = static_cast<PAL::SFP16>((170 + (lane * 100)) << 8);
        s_graphics->drawVoxelColumn(
            laneX - (2 << 8), laneX + (2 << 8),
            0, static_cast<PAL::SFP16>(400 << 8),
            static_cast<PAL::SFP16>(30 << 8), 7
        );
    }

    // 2. Iterate through the visible viewport horizon (From read-head forward)
    uint16_t scanIdx = m_readHead;
    uint16_t count = m_activeChart->noteCount;
    const Note* notes = m_activeChart->notes;

    // Render ahead up to 0x1C00 timeline window units
    while (scanIdx < count && notes[scanIdx].timeline <= (progress + 0x1C00)) {
        if (m_noteStates[scanIdx].hitResult == Judgment::Pending) {
            int32_t delta = static_cast<int32_t>(notes[scanIdx].timeline) - static_cast<int32_t>(progress);
            
            // Map timeline delta linearly to visual 3D Z-depth space
            PAL::SFP16 visualZ = static_cast<PAL::SFP16>((delta * 32) >> 8);
            PAL::SFP16 laneX   = static_cast<PAL::SFP16>((170 + (notes[scanIdx].lane * 100)) << 8);
            PAL::SFP16 targetY = static_cast<PAL::SFP16>((60 << 8) + (energy << 3));

            s_graphics->drawBlock(laneX, targetY, visualZ, notes[scanIdx].lane);
        }
        scanIdx++;
    }

    s_graphics->drawHUD(m_score, m_combo, m_missCount, 0);
}

void GameEngine::renderTitleScreen() {
    s_graphics->drawVoxelColumnGlow(
        static_cast<PAL::SFP16>(120 << 8), static_cast<PAL::SFP16>(520 << 8),
        static_cast<PAL::SFP16>(200 << 8), static_cast<PAL::SFP16>(280 << 8),
        static_cast<PAL::SFP16>(5 << 8), 2, 255
    );
    s_graphics->drawHUD(0, 0, 0, 0); 
}

void GameEngine::renderSongSelectMenu() {
    for (uint8_t i = 0; i < TOTAL_SONGS; ++i) {
        PAL::SFP16 xPos = static_cast<PAL::SFP16>((160 + (i * 160)) << 8);
        PAL::SFP16 yPos = static_cast<PAL::SFP16>(240 << 8);
        PAL::SFP16 zPos = static_cast<PAL::SFP16>(10 << 8);

        if (i == m_selectedSong) {
            s_graphics->drawBlock(xPos, yPos, zPos, i);
            s_graphics->drawVoxelColumnGlow(xPos - (15<<8), xPos + (15<<8), yPos - (30<<8), yPos + (30<<8), zPos, i, 160);
        } else {
            s_graphics->drawParticle(xPos, yPos, zPos, i, 80);
        }
    }
}

void GameEngine::renderResultsScreen() {
    s_graphics->drawBlock(static_cast<PAL::SFP16>(320 << 8), static_cast<PAL::SFP16>(240 << 8), static_cast<PAL::SFP16>(15 << 8), 7);
    s_graphics->drawHUD(m_score, 0, m_missCount, 0);
}

} // namespace Engine
