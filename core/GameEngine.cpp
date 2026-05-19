#include "GameEngine.h"
#include "charts/SongIndex.h"

namespace Engine {

// ... (Static LUTs, init, loadChart, reset, etc. remain unchanged) ...

void GameEngine::updateGameplaySimulation(PAL::InputState pressed) {
    // HEARTBEAT INTEGRATION:
    // The engine no longer cares about platform-specific clock hacks.
    // We query the abstraction provided by the PlatformBundle.
    m_localTrackAccumulator = s_clock->getCurrentTime();

    if (m_localTrackAccumulator >= 0xFFFF) {
        s_audio->stop();
        m_currentState = EngineState::ResultsScreen;
        return;
    }

    if (pressed & LANE_MASKS[0] && m_shipLane > 0) --m_shipLane;
    if (pressed & LANE_MASKS[1] && m_shipLane < 2) ++m_shipLane;

    const FrameResult fr = evaluateChart(m_localTrackAccumulator, 0);
    m_score += static_cast<uint16_t>(fr.perfectCount * 300u);
    m_combo = (fr.missCount + fr.holdDropCount > 0) ? 0 : m_combo + fr.perfectCount;
    m_missCount += (fr.missCount + fr.holdDropCount);
    
    tickParticles();
}

bool GameEngine::init(const PAL::PlatformBundle& bundle, uint8_t initialSongId) {
    s_graphics = bundle.graphics;
    s_audio    = bundle.audio;
    s_input    = bundle.input;
    s_clock    = bundle.clock; // Cache the clock interface

    if (!s_graphics || !s_audio || !s_input || !s_clock) return false;
    if (!s_graphics->init(640, 480))           return false;
    if (!s_audio->init())                      return false;
    if (!s_input->init())                      return false;

    m_currentState = EngineState::TitleScreen;
    m_selectedSong = initialSongId;
    m_cameraZ      = 0;

    for (uint8_t i = 0; i < MAX_PARTICLES; ++i) m_particles[i].lifetime = 0;

    resetPuzzleGrid();
    m_shipLane = 1;
    m_localTrackAccumulator = 0;
    m_isRunning = true;
    
    return true;
}

// ... (All other methods: tickParticles, pushBlockToGrid, checkAndResolveMatches, 
//      evaluateChart, tick, render, etc. remain exactly as you wrote them) ...

} // namespace Engine

void GameEngine::tickParticles() {
    for (uint8_t i = 0; i < MAX_PARTICLES; ++i) {
        if (m_particles[i].lifetime == 0) continue;
        --m_particles[i].lifetime;
        m_particles[i].posY = static_cast<PAL::SFP16>(m_particles[i].posY + m_particles[i].velY);
    }
}

bool GameEngine::pushBlockToGrid(uint8_t lane, uint8_t blockType) {
    if (lane >= 3 || blockType == 0) return false;
    uint8_t height = m_gridHeights[lane];
    if (height >= GRID_MAX_ROWS) return true;
    uint16_t shiftAmount = height * 3;
    m_puzzleGrid[lane] |= (static_cast<uint16_t>(blockType & 0x07) << shiftAmount);
    m_gridHeights[lane]++;
    if (blockType != 3) checkAndResolveMatches();
    return (m_gridHeights[lane] >= GRID_MAX_ROWS);
}

void GameEngine::checkAndResolveMatches() {
    bool foundMatch = false;
    uint8_t markedMasks[3] = {0, 0, 0};
    uint8_t unpackedGrid[3][6];
    for (uint8_t l = 0; l < 3; ++l) {
        uint16_t currentLaneData = m_puzzleGrid[l];
        for (uint8_t r = 0; r < m_gridHeights[l]; ++r) unpackedGrid[l][r] = (currentLaneData >> (r * 3)) & 0x07;
    }
    for (uint8_t l = 0; l < 3; ++l) {
        if (m_gridHeights[l] < 3) continue;
        for (uint8_t r = 0; r <= m_gridHeights[l] - 3; ++r) {
            uint8_t color = unpackedGrid[l][r];
            if (color == 0 || color == 3) continue;
            if (unpackedGrid[l][r+1] == color && unpackedGrid[l][r+2] == color) {
                markedMasks[l] |= (7 << r);
                foundMatch = true;
            }
        }
    }
    for (uint8_t r = 0; r < GRID_MAX_ROWS; ++r) {
        if (m_gridHeights[0] <= r || m_gridHeights[1] <= r || m_gridHeights[2] <= r) continue;
        uint8_t color = unpackedGrid[0][r];
        if (color == 0 || color == 3) continue;
        if (unpackedGrid[1][r] == color && unpackedGrid[2][r] == color) {
            markedMasks[0] |= (1 << r); markedMasks[1] |= (1 << r); markedMasks[2] |= (1 << r);
            foundMatch = true;
        }
    }
    if (foundMatch) {
        uint8_t totalCleared = 0;
        for (uint8_t l = 0; l < 3; ++l) {
            if (markedMasks[l] == 0) continue;
            uint16_t newLaneVal = 0;
            uint8_t writeIdx = 0;
            for (uint8_t r = 0; r < m_gridHeights[l]; ++r) {
                if (markedMasks[l] & (1 << r)) { ++totalCleared; continue; }
                newLaneVal |= (static_cast<uint16_t>(unpackedGrid[l][r]) << (writeIdx * 3));
                ++writeIdx;
            }
            m_puzzleGrid[l] = newLaneVal;
            m_gridHeights[l] = writeIdx;
        }
        m_score += static_cast<uint16_t>(totalCleared * 500u * (m_combo > 0 ? (m_combo >> 2) + 1 : 1));
        spawnBurst(1, totalCleared * 2);
    }
}

FrameResult GameEngine::evaluateChart(PAL::FP16 trackPos, PAL::InputState pressed) {
    FrameResult result = {0, 0, 0, 0, 0};
    auto processNotes = [&](uint16_t head, uint16_t tail, bool isStreaming) {
        const uint16_t mask = isStreaming ? RING_BUFFER_MASK : 0xFFFF;
        const uint16_t limit = isStreaming ? tail : (m_activeChart->noteCount < MAX_NOTES_PER_CHART ? m_activeChart->noteCount : MAX_NOTES_PER_CHART);
        if (trackPos > WINDOW_MISS) {
            const PAL::FP16 missFloor = static_cast<PAL::FP16>(trackPos - WINDOW_MISS);
            for (uint16_t i = head; i != limit; ++i) {
                uint16_t idx = isStreaming ? (i & mask) : i;
                const Note& note = isStreaming ? m_streamingNotes[idx] : m_activeChart->notes[idx];
                NoteState& state = isStreaming ? m_streamingNoteStates[idx] : m_noteStates[idx];
                if (note.timeline >= missFloor) break;
                if (state.hitResult == 0) { state.hitResult = 3; ++result.missCount; }
            }
        }
        for (uint16_t i = head; i != limit; ++i) {
            uint16_t idx = isStreaming ? (i & mask) : i;
            const Note& note = isStreaming ? m_streamingNotes[idx] : m_activeChart->notes[idx];
            NoteState& state = isStreaming ? m_streamingNoteStates[idx] : m_noteStates[idx];
            if (note.timeline > trackPos) break;
            if (state.hitResult != 0) continue;
            if (m_shipLane == note.lane) {
                state.hitResult = 1; ++result.perfectCount;
                spawnBurst(note.lane, BURST_COUNT);
                if (pushBlockToGrid(note.lane, note.flags)) ++result.holdDropCount;
            } else { state.hitResult = 3; ++result.missCount; }
        }
    };
    if (m_isStreamingMode) processNotes(m_streamHead, m_streamTail, true);
    else if (m_activeChart) processNotes(m_readHead, 0, false);
    return result;
}

void GameEngine::updateGameplaySimulation(PAL::InputState pressed) {
#ifdef __DREAMCAST__
    m_syncTickCounter++;
    if ((m_syncTickCounter & 0x03) == 0) m_localTrackAccumulator = static_cast<PAL::FP16>(s_audio->getTrackProgress());
    else {
        uint32_t predicted = static_cast<uint32_t>(m_localTrackAccumulator) + 0x0440u;
        m_localTrackAccumulator = (predicted > 0xFFFFu) ? 0xFFFF : static_cast<PAL::FP16>(predicted);
    }
#else
    double audioTime = EM_ASM_DOUBLE({ return window._audioEl ? window._audioEl.currentTime : 0; });
    double audioDur = EM_ASM_DOUBLE({ return window._trackDur || 1; });
    if (audioTime > 0.0 && !s_audio->isPaused()) {
        uint32_t truePos = static_cast<uint32_t>((audioTime / audioDur) * 65535.0);
        m_localTrackAccumulator = static_cast<PAL::FP16>(truePos);
    }
#endif
    if (m_localTrackAccumulator >= 0xFFFF) {
        s_audio->stop();
        m_currentState = EngineState::ResultsScreen;
        return;
    }
    if (pressed & LANE_MASKS[0] && m_shipLane > 0) --m_shipLane;
    if (pressed & LANE_MASKS[1] && m_shipLane < 2) ++m_shipLane;
    const FrameResult fr = evaluateChart(m_localTrackAccumulator, 0);
    m_score += static_cast<uint16_t>(fr.perfectCount * 300u);
    m_combo = (fr.missCount + fr.holdDropCount > 0) ? 0 : m_combo + fr.perfectCount;
    m_missCount += (fr.missCount + fr.holdDropCount);
    tickParticles();
}

void GameEngine::tick() {
    s_input->poll();
    PAL::InputState pressed = s_input->readPressedActions();
    switch (m_currentState) {
        case EngineState::TitleScreen: if (pressed != 0) m_currentState = EngineState::SongSelect; break;
        case EngineState::SongSelect:
            if (pressed & LANE_MASKS[0] && m_selectedSong > 0) --m_selectedSong;
            if (pressed & LANE_MASKS[1] && m_selectedSong < (TOTAL_SONGS - 1)) ++m_selectedSong;
            if (pressed & static_cast<PAL::InputState>(PAL::InputAction::Confirm)) {
                loadChart(m_selectedSong == CD_STREAM_TRACK_ID ? nullptr : kSongTable[m_selectedSong]);
                resetScoreCounters(); s_audio->play(m_selectedSong); m_currentState = EngineState::Gameplay;
            }
            break;
        case EngineState::Gameplay: updateGameplaySimulation(pressed); break;
        case EngineState::ResultsScreen: if (pressed & static_cast<PAL::InputState>(PAL::InputAction::Confirm)) m_currentState = EngineState::SongSelect; break;
    }
}

void GameEngine::render() {
    s_graphics->beginFrame();
    switch (m_currentState) {
        case EngineState::TitleScreen: renderTitleScreen(); break;
        case EngineState::SongSelect: renderSongSelectMenu(); break;
        case EngineState::Gameplay: renderGameplayScene(); break;
        case EngineState::ResultsScreen: renderResultsScreen(); break;
    }
    s_graphics->endFrame();
}

void GameEngine::renderGameplayScene() const {
    const uint8_t energy = s_audio->getEnergyLevel();
    // Update Camera Inertia (The "Spring" effect)
    int32_t diff = static_cast<int32_t>(m_localTrackAccumulator) - static_cast<int32_t>(m_cameraZ);
    m_cameraZ += static_cast<PAL::FP16>((diff * 0x0C00) >> 16);

    const uint32_t viewLookAhead = (static_cast<uint32_t>(m_cameraZ) + 0x2400u) > 0xFFFFu ? 0xFFFFu : (m_cameraZ + 0x2400u);
    
    s_graphics->drawVoxelHighway(320<<8, 200<<8, 0, static_cast<PAL::SFP16>(energy << 7), m_selectedSong);
    s_graphics->drawShipVehicle(LANE_X[m_shipLane], LANE_HIT_Y, m_shipLane);

    auto renderNotes = [&](uint16_t scanIdx, uint16_t limit, bool streaming) {
        while (scanIdx != limit) {
            uint16_t idx = streaming ? (scanIdx & RING_BUFFER_MASK) : scanIdx;
            const Note& n = streaming ? m_streamingNotes[idx] : m_activeChart->notes[idx];
            if (n.timeline > viewLookAhead) break;
            if ((streaming ? m_streamingNoteStates[idx].hitResult : m_noteStates[idx].hitResult) == 0) {
                int32_t zDist = static_cast<int32_t>(n.timeline) - static_cast<int32_t>(m_cameraZ);
                if (zDist > 0) {
                    PAL::SFP16 s = static_cast<PAL::SFP16>((0x100000 / (zDist + 1)) >> 8);
                    s_graphics->drawBlock3D(static_cast<PAL::SFP16>(320 + (((LANE_X[n.lane] - 320) * s) >> 8)),
                                            static_cast<PAL::SFP16>(200 + (((LANE_HIT_Y - 200) * s) >> 8) + (energy << 2)), s, n.flags);
                }
            }
            scanIdx++;
        }
    };

    if (m_isStreamingMode) renderNotes(m_streamHead, m_streamTail, true);
    else if (m_activeChart) renderNotes(m_readHead, (m_activeChart->noteCount < MAX_NOTES_PER_CHART ? m_activeChart->noteCount : MAX_NOTES_PER_CHART), false);

    for (uint8_t l = 0; l < 3; ++l) {
        for (uint8_t r = 0; r < m_gridHeights[l]; ++r) {
            uint8_t t = (m_puzzleGrid[l] >> (r * 3)) & 0x07;
            if (t != 0) s_graphics->drawBlock(static_cast<PAL::SFP16>((500 + (l * GRID_BLOCK_SPACING)) << 8), 
                                              static_cast<PAL::SFP16>(GRID_BASE_Y - (r * GRID_BLOCK_SPACING)), 1<<8, t);
        }
    }
    renderParticles();
    s_graphics->drawHUD(m_score, m_combo, m_missCount, 0);
}

// ... renderTitleScreen, renderSongSelectMenu, renderResultsScreen unchanged ...
}

void GameEngine::renderTitleScreen() const {
    s_graphics->drawVoxelColumnGlow(
        static_cast<PAL::SFP16>(120 << 8), static_cast<PAL::SFP16>(520 << 8),
        static_cast<PAL::SFP16>(200 << 8), static_cast<PAL::SFP16>(280 << 8),
        static_cast<PAL::SFP16>(5   << 8), 2, 255
    );
    s_graphics->drawHUD(0, 0, 0, 0);
}

void GameEngine::renderSongSelectMenu() const {
    for (uint8_t i = 0; i < TOTAL_SONGS; ++i) {
        const PAL::SFP16 xPos = static_cast<PAL::SFP16>((160 + (i * 160)) << 8);
        const PAL::SFP16 yPos = static_cast<PAL::SFP16>(240 << 8);
        const PAL::SFP16 zPos = static_cast<PAL::SFP16>(10  << 8);

        if (i == m_selectedSong) {
            s_graphics->drawBlock(xPos, yPos, zPos, i);
            s_graphics->drawVoxelColumnGlow(
                static_cast<PAL::SFP16>(xPos - (15 << 8)),
                static_cast<PAL::SFP16>(xPos + (15 << 8)),
                static_cast<PAL::SFP16>(yPos - (30 << 8)),
                static_cast<PAL::SFP16>(yPos + (30 << 8)),
                zPos, i, 160
            );
        } else {
            s_graphics->drawParticle(xPos, yPos, zPos, i, 80);
        }
    }
}

void GameEngine::renderResultsScreen() const {
    s_graphics->drawBlock(static_cast<PAL::SFP16>(320 << 8), static_cast<PAL::SFP16>(240 << 8), static_cast<PAL::SFP16>(15 << 8), 7);
    s_graphics->drawHUD(m_score, m_combo, m_missCount, 0);
}

bool GameEngine::isRunning() const { return m_isRunning; }

} // namespace Engine
