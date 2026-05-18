// =============================================================================
// GameEngine.cpp
// Core Game Logic Pipeline — Fully Isolated Object-Oriented Architecture
// =============================================================================

#include "GameEngine.h"
#include "charts/SongIndex.h"

namespace Engine {

// Zero-allocation lookup array to instantly resolve fading opacity variables
static const uint8_t kAlphaLUT[17] = {
    0, 16, 32, 48, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 224, 240, 255
};

// Global static abstraction interfaces bound at runtime
GraphicsInterface* GameEngine::s_graphics = nullptr;
AudioInterface*    GameEngine::s_audio    = nullptr;
InputInterface*    GameEngine::s_input    = nullptr;

// =============================================================================
// PLATFORM CONFIGURATION INITIALIZATION & STATE REBOOT LAYERS
// =============================================================================

bool GameEngine::init(const PAL::PlatformBundle& bundle, uint8_t initialSongId) {
    s_graphics = bundle.graphics;
    s_audio    = bundle.audio;
    s_input    = bundle.input;

    if (!s_graphics || !s_audio || !s_input) return false;
    if (!s_graphics->init(640, 480))         return false;
    if (!s_audio->init())                    return false;
    if (!s_input->init())                    return false;

    m_currentState = EngineState::TitleScreen;
    m_selectedSong = initialSongId;

    for (uint8_t i = 0; i < MAX_PARTICLES; ++i) {
        m_particles[i].lifetime = 0;
    }

    resetPuzzleGrid();

    m_lastHardwareTrackPos = 0;
    m_localTrackAccumulator = 0;
    m_syncTickCounter = 0;
    m_isRunning = true;
    
    return true;
}

void GameEngine::loadChart(const NoteChart* chart) {
    m_activeChart = chart;
    m_readHead    = 0;
    m_isStreamingMode = (chart == nullptr);

    m_streamHead = 0;
    m_streamTail = 0;

    const uint16_t count = (!chart) ? 0u 
                         : (chart->noteCount < MAX_NOTES_PER_CHART) 
                           ? chart->noteCount : MAX_NOTES_PER_CHART;

    for (uint16_t i = 0; i < count; ++i) {
        m_noteStates[i].hitResult = 0;
        m_noteStates[i].holdPhase = HoldState::Inactive;
    }
}

void GameEngine::resetScoreCounters() {
    m_score     = 0;
    m_combo     = 0;
    m_missCount = 0;
    resetPuzzleGrid();
}

void GameEngine::resetPuzzleGrid() {
    m_puzzleGrid[0] = 0; m_puzzleGrid[1] = 0; m_puzzleGrid[2] = 0;
    m_gridHeights[0] = 0; m_gridHeights[1] = 0; m_gridHeights[2] = 0;
}

// =============================================================================
// ZERO-ALLOCATION LOW-LATENCY PARTICLE STRUCT MECHANICS
// =============================================================================

void GameEngine::spawnBurst(uint8_t lane, uint8_t count) {
    if (lane >= 3) return; 
    uint8_t spawned = 0;

    for (uint8_t i = 0; i < MAX_PARTICLES && spawned < count; ++i) {
        if (m_particles[i].lifetime != 0) continue;

        const int16_t xSpread = static_cast<int16_t>((static_cast<int16_t>(i % 7) - 3) << 5);

        m_particles[i].posX       = static_cast<PAL::SFP16>(LANE_X[lane] + xSpread);
        m_particles[i].posY       = LANE_HIT_Y;
        m_particles[i].velY       = PARTICLE_VEL_Y - static_cast<int16_t>(spawned << 5);
        m_particles[i].lifetime   = PARTICLE_LIFE;
        m_particles[i].colorIndex = lane;
        
        ++spawned;
    }
}

void GameEngine::tickParticles() {
    for (uint8_t i = 0; i < MAX_PARTICLES; ++i) {
        if (m_particles[i].lifetime == 0) continue;
        --m_particles[i].lifetime;
        m_particles[i].posY = static_cast<PAL::SFP16>(m_particles[i].posY + m_particles[i].velY);
    }
}

// =============================================================================
// BIT-PACKED NIBBLE MATCHING MATRIX SYSTEM (High-Efficiency Execution)
// =============================================================================

bool GameEngine::pushBlockToGrid(uint8_t lane, uint8_t blockType) {
    if (lane >= 3 || blockType == 0) return false;
    uint8_t height = m_gridHeights[lane];

    if (height >= GRID_MAX_ROWS) return true; // Column Overflowed

    // Inject block value into targeted 3-bit slot position segment
    uint16_t shiftAmount = height * 3;
    m_puzzleGrid[lane] |= (static_cast<uint16_t>(blockType & 0x07) << shiftAmount);
    m_gridHeights[lane]++;

    // Scan matrix configuration immediately unless block is an obstacle block
    if (blockType != 3) {
        checkAndResolveMatches();
    }

    return (m_gridHeights[lane] >= GRID_MAX_ROWS);
}

void GameEngine::checkAndResolveMatches() {
    bool foundMatch = false;
    uint8_t markedMasks[3] = {0, 0, 0}; // Individual bits 0-5 track cells flagged for erasure

    // Unpack board lane variables cleanly to read types
    uint8_t unpackedGrid[3][6];
    for (uint8_t l = 0; l < 3; ++l) {
        uint16_t currentLaneData = m_puzzleGrid[l];
        for (uint8_t r = 0; r < m_gridHeights[l]; ++r) {
            unpackedGrid[l][r] = (currentLaneData >> (r * 3)) & 0x07;
        }
    }

    // 1. Scan Vertical Lanes
    for (uint8_t l = 0; l < 3; ++l) {
        if (m_gridHeights[l] < 3) continue;
        for (uint8_t r = 0; r <= m_gridHeights[l] - 3; ++r) {
            uint8_t color = unpackedGrid[l][r];
            if (color == 0 || color == 3) continue;
            if (unpackedGrid[l][r+1] == color && unpackedGrid[l][r+2] == color) {
                markedMasks[l] |= (7 << r); // Sets consecutive bits for match span row positions
                foundMatch = true;
            }
        }
    }

    // 2. Scan Horizontal Rows
    for (uint8_t r = 0; r < GRID_MAX_ROWS; ++r) {
        if (m_gridHeights[0] <= r || m_gridHeights[1] <= r || m_gridHeights[2] <= r) continue;
        uint8_t color = unpackedGrid[0][r];
        if (color == 0 || color == 3) continue;
        if (unpackedGrid[1][r] == color && unpackedGrid[2][r] == color) {
            markedMasks[0] |= (1 << r);
            markedMasks[1] |= (1 << r);
            markedMasks[2] |= (1 << r);
            foundMatch = true;
        }
    }

    // 3. Bitwise Column Compression
    if (foundMatch) {
        uint8_t totalCleared = 0;
        for (uint8_t l = 0; l < 3; ++l) {
            if (markedMasks[l] == 0) continue;

            uint16_t newLaneVal = 0;
            uint8_t writeIdx = 0;
            for (uint8_t r = 0; r < m_gridHeights[l]; ++r) {
                if (markedMasks[l] & (1 << r)) {
                    ++totalCleared;
                    continue; // Purge block by skipping insertion index updates
                }
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

// =============================================================================
// HYBRID MODE FORWARD CHART VECTOR WORKER (Supports Pointer & Ring Buffers)
// =============================================================================

FrameResult GameEngine::evaluateChart(PAL::FP16 trackPos, PAL::InputState pressed) {
    FrameResult result = {0, 0, 0, 0, 0};

    if (m_isStreamingMode) {
        if (m_streamHead == m_streamTail) return result;

        if (trackPos > WINDOW_MISS) {
            const PAL::FP16 missFloor = static_cast<PAL::FP16>(trackPos - WINDOW_MISS);
            while (m_streamHead != m_streamTail) {
                uint16_t headIdx = m_streamHead & RING_BUFFER_MASK;
                if (m_streamingNotes[headIdx].timeline >= missFloor) break;
                
                if (m_streamingNoteStates[headIdx].hitResult == 0) {
                    m_streamingNoteStates[headIdx].hitResult = 3; 
                    ++result.missCount;
                }
                ++m_streamHead;
            }
        }

        const uint16_t scanLimit = m_streamTail;
        for (uint16_t i = m_streamHead; i != scanLimit; ++i) {
            uint16_t idx = i & RING_BUFFER_MASK;
            const Note& note = m_streamingNotes[idx];
            NoteState& state = m_streamingNoteStates[idx];

            if (note.timeline > (trackPos + WINDOW_GOOD)) break;
            if (state.hitResult != 0) continue;

            const bool laneBit = (pressed & LANE_MASKS[note.lane]) != 0;
            const PAL::FP16 delta = fp16AbsDelta(trackPos, note.timeline);

            if (laneBit) {
                if (delta <= WINDOW_PERFECT) {
                    state.hitResult = 1;
                    ++result.perfectCount;
                    spawnBurst(note.lane, BURST_COUNT);
                    bool crashed = pushBlockToGrid(note.lane, note.flags); 
                    if (crashed) ++result.holdDropCount;
                } else if (delta <= WINDOW_GOOD) {
                    state.hitResult = 2;
                    ++result.goodCount;
                    spawnBurst(note.lane, BURST_COUNT / 2u);
                    bool crashed = pushBlockToGrid(note.lane, note.flags);
                    if (crashed) ++result.holdDropCount;
                }
            }
        }
        return result;
    }

    if (!m_activeChart) return result;

    const uint16_t noteCount = (m_activeChart->noteCount < MAX_NOTES_PER_CHART)
                               ? m_activeChart->noteCount : MAX_NOTES_PER_CHART;
    if (m_readHead >= noteCount) return result;

    if (trackPos > WINDOW_MISS) {
        const PAL::FP16 missFloor = static_cast<PAL::FP16>(trackPos - WINDOW_MISS);
        while (m_readHead < noteCount) {
            const Note&  note  = m_activeChart->notes[m_readHead];
            NoteState&   state = m_noteStates[m_readHead];

            if (note.timeline >= missFloor) break;
            if (state.hitResult == 0) {
                state.hitResult = 3; 
                ++result.missCount;
            }
            ++m_readHead;
        }
    }

    const uint32_t  lookAheadRaw = static_cast<uint32_t>(trackPos) + WINDOW_GOOD;
    const PAL::FP16 lookAhead    = (lookAheadRaw > 0xFFFFu) ? static_cast<PAL::FP16>(0xFFFF) : static_cast<PAL::FP16>(lookAheadRaw);

    for (uint16_t i = m_readHead; i < noteCount; ++i) {
        const Note& note  = m_activeChart->notes[i];
        NoteState&  state = m_noteStates[i];

        if (note.timeline > lookAhead) break;

        const bool laneBit = (pressed & LANE_MASKS[note.lane]) != 0;
        const bool isHold  = (note.holdLength > 0);
        const PAL::FP16 delta = fp16AbsDelta(trackPos, note.timeline);

        if (!isHold) {
            if (state.hitResult != 0) continue; 
            if (laneBit) {
                if (delta <= WINDOW_PERFECT) {
                    state.hitResult = 1;
                    ++result.perfectCount;
                    spawnBurst(note.lane, BURST_COUNT);
                    pushBlockToGrid(note.lane, note.flags);
                } else if (delta <= WINDOW_GOOD) {
                    state.hitResult = 2;
                    ++result.goodCount;
                    spawnBurst(note.lane, BURST_COUNT / 2u);
                    pushBlockToGrid(note.lane, note.flags);
                }
            }
            continue;
        }

        switch (state.holdPhase) {
            case HoldState::Inactive:
                if (laneBit && delta <= WINDOW_GOOD) {
                    state.holdPhase = HoldState::Gating;
                    state.hitResult = (delta <= WINDOW_PERFECT) ? 1u : 2u;
                    spawnBurst(note.lane, BURST_COUNT);
                }
                break;

            case HoldState::Gating: {
                const uint32_t holdEndRaw = static_cast<uint32_t>(note.timeline) + note.holdLength;
                const PAL::FP16 holdEnd   = (holdEndRaw > 0xFFFFu) ? static_cast<PAL::FP16>(0xFFFF) : static_cast<PAL::FP16>(holdEndRaw);

                if (trackPos >= holdEnd) {
                    state.holdPhase = HoldState::Complete;
                    spawnBurst(note.lane, BURST_COUNT);
                } else if (!laneBit) {
                    state.holdPhase = HoldState::DroppedEarly;
                    state.hitResult = 3; 
                    ++result.holdDropCount;
                } else {
                    ++result.activeHolds;
                }
                break;
            }

            case HoldState::Complete:
            case HoldState::DroppedEarly:
                break; 
        }
    }

    return result;
}

void GameEngine::updateGameplaySimulation(PAL::InputState pressed) {
    // --- DRIFT-RESISTANT COUNTER SYNCHRONIZATION HARDENING LAYER ---
    m_syncTickCounter++;
    
    // Sub-query system: Only query hardware clock bounds once every 4 frames
    if ((m_syncTickCounter & 0x03) == 0) {
        PAL::FP16 hardwarePos = s_audio->getTrackProgress();
        m_localTrackAccumulator = hardwarePos;
    } else {
        // Linearly step internal clock estimation using 60Hz frame pacing ticks (approx 0x0440 out of 0xFFFF scale bounds)
        uint32_t predictedTrack = static_cast<uint32_t>(m_localTrackAccumulator) + 0x0440u;
        m_localTrackAccumulator = (predictedTrack > 0xFFFFu) ? static_cast<PAL::FP16>(0xFFFF) : static_cast<PAL::FP16>(predictedTrack);
    }

    if (m_localTrackAccumulator >= 0xFFFF) {
        s_audio->stop();
        m_currentState = EngineState::ResultsScreen;
        return;
    }

    const FrameResult fr = evaluateChart(m_localTrackAccumulator, pressed);

    const uint8_t totalHits  = fr.perfectCount + fr.goodCount;
    const uint8_t totalMiss  = fr.missCount    + fr.holdDropCount;

    m_score     += static_cast<uint16_t>(fr.perfectCount * 300u) + static_cast<uint16_t>(fr.goodCount * 100u);
    m_combo      = (totalMiss > 0) ? 0u : static_cast<uint16_t>(m_combo + totalHits);
    m_missCount += totalMiss;

    tickParticles();
}

// =============================================================================
// PLATFORM ENGINE TICK ROUTINES
// =============================================================================

void GameEngine::tick() {
    s_input->poll();
    PAL::InputState pressed = s_input->readPressedActions();

    switch (m_currentState) {
        case EngineState::TitleScreen:
            if (pressed != 0) m_currentState = EngineState::SongSelect;
            break;

        case EngineState::SongSelect:
            if (pressed & static_cast<PAL::InputState>(PAL::InputAction::LaneLeft)) { 
                if (m_selectedSong > 0) --m_selectedSong;
            }
            if (pressed & static_cast<PAL::InputState>(PAL::InputAction::LaneRight)) { 
                if (m_selectedSong < (TOTAL_SONGS - 1)) ++m_selectedSong;
            }
            if (pressed & static_cast<PAL::InputState>(PAL::InputAction::Confirm)) {
                if (m_selectedSong == CD_STREAM_TRACK_ID) {
                    loadChart(nullptr); 
                } else {
                    loadChart(kSongTable[m_selectedSong]);
                }
                resetScoreCounters();
                s_audio->play(m_selectedSong);
                m_currentState = EngineState::Gameplay;
            }
            break;

        case EngineState::Gameplay:
            updateGameplaySimulation(pressed);
            break;

        case EngineState::ResultsScreen:
            if (pressed & static_cast<PAL::InputState>(PAL::InputAction::Confirm)) {
                m_currentState = EngineState::SongSelect;
            }
            break;
    }
}

// =============================================================================
// STREAM RENDERING GENERATION PIPELINE 
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

void GameEngine::renderParticles() const {
    for (uint8_t i = 0; i < MAX_PARTICLES; ++i) {
        if (m_particles[i].lifetime == 0) continue;

        // Optimized zero-overhead table indexing step
        const uint8_t alpha = kAlphaLUT[m_particles[i].lifetime & 0x0F];

        s_graphics->drawParticle(
            m_particles[i].posX, m_particles[i].posY,
            static_cast<PAL::SFP16>(2 << 8), 
            m_particles[i].colorIndex, alpha
        );
    }
}

void GameEngine::renderGameplayScene() const {
    const uint8_t energy = s_audio->getEnergyLevel();

    const uint32_t viewLookAheadRaw = static_cast<uint32_t>(m_localTrackAccumulator) + 0x1C00u;
    const uint16_t viewLookAhead    = (viewLookAheadRaw > 0xFFFFu) ? 0xFFFFu : static_cast<uint16_t>(viewLookAheadRaw);

    // ── 1. Draw Voxel Highway Footprint Corridor (Opaque) ────────────────────
    const PAL::SFP16 height = static_cast<PAL::SFP16>(energy << 7);
    s_graphics->drawVoxelColumn(
        static_cast<PAL::SFP16>(200 << 8), static_cast<PAL::SFP16>(440 << 8),
        static_cast<PAL::SFP16>(0   << 8), height,
        static_cast<PAL::SFP16>(20  << 8), m_selectedSong
    );

    // ── 2. Draw Match-3 Matrix Grid Blocks (Opaque Bottom Stack) ─────────────
    for (uint8_t l = 0; l < 3; ++l) {
        uint16_t laneData = m_puzzleGrid[l];
        for (uint8_t r = 0; r < m_gridHeights[l]; ++r) {
            uint8_t cellType = (laneData >> (r * 3)) & 0x07;
            if (cellType == 0) continue;

            PAL::SFP16 gridX = LANE_X[l];
            PAL::SFP16 gridY = static_cast<PAL::SFP16>(GRID_BASE_Y - (r * GRID_BLOCK_SPACING));
            s_graphics->drawBlock(gridX, gridY, static_cast<PAL::SFP16>(1 << 8), cellType);
        }
    }

    // ── 3. Streaming Note Blocks Lookahead Engine (Opaque Entities) ──────────
    if (m_isStreamingMode) {
        uint16_t scanIdx = m_streamHead;
        const uint16_t limitIdx = m_streamTail;
        
        while (scanIdx != limitIdx) {
            uint16_t idx = scanIdx & RING_BUFFER_MASK;
            const Note& note = m_streamingNotes[idx];

            if (note.timeline > viewLookAhead) break;

            if (m_streamingNoteStates[idx].hitResult == 0) {
                int32_t delta = static_cast<int32_t>(note.timeline) - static_cast<int32_t>(m_localTrackAccumulator);
                PAL::SFP16 visualZ = static_cast<PAL::SFP16>((delta * 24) >> 8);
                PAL::SFP16 targetY = LANE_HIT_Y - (visualZ << 2) + (energy << 3);

                s_graphics->drawBlock(LANE_X[note.lane], targetY, visualZ, note.flags);
            }
            scanIdx++;
        }
    } else if (m_activeChart) {
        uint16_t scanIdx = m_readHead;
        const uint16_t noteCount = (m_activeChart->noteCount < MAX_NOTES_PER_CHART)
                                   ? m_activeChart->noteCount : MAX_NOTES_PER_CHART;

        while (scanIdx < noteCount && m_activeChart->notes[scanIdx].timeline <= viewLookAhead) {
            if (m_noteStates[scanIdx].hitResult == 0) {
                const Note& note = m_activeChart->notes[scanIdx];
                int32_t delta = static_cast<int32_t>(note.timeline) - static_cast<int32_t>(m_localTrackAccumulator);
                PAL::SFP16 visualZ = static_cast<PAL::SFP16>((delta * 24) >> 8);
                PAL::SFP16 targetY = LANE_HIT_Y - (visualZ << 2) + (energy << 3);

                s_graphics->drawBlock(LANE_X[note.lane], targetY, visualZ, note.flags);
            }
            scanIdx++;
        }
    }

    // ── 4. Particle Burst Engine Render Stream (Translucent) ─────────────────
    renderParticles();

    // ── 5. Standard Core HUD Interface Display (Punch-Through Overlay) ───────
    s_graphics->drawHUD(m_score, m_combo, m_missCount, 0);
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
    // PASS 1: Submit ALL Opaque Menu Elements (Solid Core Cubes)
    for (uint8_t i = 0; i < TOTAL_SONGS; ++i) {
        if (i == m_selectedSong) {
            const PAL::SFP16 xPos = static_cast<PAL::SFP16>((160 + (i * 160)) << 8);
            const PAL::SFP16 yPos = static_cast<PAL::SFP16>(240 << 8);
            const PAL::SFP16 zPos = static_cast<PAL::SFP16>(10  << 8);

            s_graphics->drawBlock(xPos, yPos, zPos, i);
        }
    }

    // PASS 2: Submit ALL Translucent Menu Overlays (Additive Blending Overlays)
    for (uint8_t i = 0; i < TOTAL_SONGS; ++i) {
        const PAL::SFP16 xPos = static_cast<PAL::SFP16>((160 + (i * 160)) << 8);
        const PAL::SFP16 yPos = static_cast<PAL::SFP16>(240 << 8);
        const PAL::SFP16 zPos = static_cast<PAL::SFP16>(10  << 8);

        if (i == m_selectedSong) {
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
