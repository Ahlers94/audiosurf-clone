#include "GameEngine.h"
#include "charts/SongIndex.h"

namespace Engine {

// Zero-allocation lookup array to instantly resolve fading opacity variables
static const uint8_t kAlphaLUT[17] = {
    0, 16, 32, 48, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 224, 240, 255
};

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

    // Reset ship positioning variables (Lanes 0, 1, 2. Starting in Center: 1)
    m_shipLane = 1;

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
    m_shipLane  = 1; // Default back to center track
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

        // Center particle burst around the 3D-projected road tracks
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

    if (height >= GRID_MAX_ROWS) return true; // Cargo hold column overflowed!

    // Inject block value into targeted 3-bit slot position segment
    uint16_t shiftAmount = height * 3;
    m_puzzleGrid[lane] |= (static_cast<uint16_t>(blockType & 0x07) << shiftAmount);
    m_gridHeights[lane]++;

    // Scan matrix inventory stack configurations instantly for match groups
    if (blockType != 3) {
        checkAndResolveMatches();
    }

    return (m_gridHeights[lane] >= GRID_MAX_ROWS);
}

void GameEngine::checkAndResolveMatches() {
    bool foundMatch = false;
    uint8_t markedMasks[3] = {0, 0, 0};

    uint8_t unpackedGrid[3][6];
    for (uint8_t l = 0; l < 3; ++l) {
        uint16_t currentLaneData = m_puzzleGrid[l];
        for (uint8_t r = 0; r < m_gridHeights[l]; ++r) {
            unpackedGrid[l][r] = (currentLaneData >> (r * 3)) & 0x07;
        }
    }

    // 1. Scan Vertical Columns inside ship storage
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

    // 2. Scan Horizontal Inventory Rows
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

    // 3. Bitwise Column Compression & Combustion Points
    if (foundMatch) {
        uint8_t totalCleared = 0;
        for (uint8_t l = 0; l < 3; ++l) {
            if (markedMasks[l] == 0) continue;

            uint16_t newLaneVal = 0;
            uint8_t writeIdx = 0;
            for (uint8_t r = 0; r < m_gridHeights[l]; ++r) {
                if (markedMasks[l] & (1 << r)) {
                    ++totalCleared;
                    continue; // Purge collected block matrix element
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
// AUDIOSURF PERSPECTIVE COLLISION PASS WORKER (Evaluates Spatial Z Crossings)
// =============================================================================

FrameResult GameEngine::evaluateChart(PAL::FP16 trackPos, PAL::InputState pressed) {
    FrameResult result = {0, 0, 0, 0, 0};

    // ── STREAMING MODE OPERATION (Live CD-DA Audio Feed Buffer) ──────────────────
    if (m_isStreamingMode) {
        if (m_streamHead == m_streamTail) return result;

        // Purge notes that zipped past behind the camera threshold space completely
        if (trackPos > WINDOW_MISS) {
            const PAL::FP16 missFloor = static_cast<PAL::FP16>(trackPos - WINDOW_MISS);
            while (m_streamHead != m_streamTail) {
                uint16_t headIdx = m_streamHead & RING_BUFFER_MASK;
                if (m_streamingNotes[headIdx].timeline >= missFloor) break;
                
                if (m_streamingNoteStates[headIdx].hitResult == 0) {
                    m_streamingNoteStates[headIdx].hitResult = 3; // Miss
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

            // If block hasn't reached the collision horizon point yet, we halt loop pass
            if (note.timeline > trackPos) break;
            if (state.hitResult != 0) continue;

            // SPATIAL OVERLAP REACTION LAYER (Did the ship scoop it up at Z == 0?)
            if (m_shipLane == note.lane) {
                state.hitResult = 1; // Perfect Scoop
                ++result.perfectCount;
                spawnBurst(note.lane, BURST_COUNT);
                bool crashed = pushBlockToGrid(note.lane, note.flags); 
                if (crashed) ++result.holdDropCount;
            } else {
                state.hitResult = 3; // Swerved away / missed
                ++result.missCount;
            }
        }
        return result;
    }

    // ── FIXED STATIC ROM CHARTS PARSING LAYER ─────────────────────────────────
    if (!m_activeChart) return result;

    const uint16_t noteCount = (m_activeChart->noteCount < MAX_NOTES_PER_CHART)
                               ? m_activeChart->noteCount : MAX_NOTES_PER_CHART;
    if (m_readHead >= noteCount) return result;

    // Fast-forward processing loops past missed notes slipping past the ship base
    if (trackPos > WINDOW_MISS) {
        const PAL::FP16 missFloor = static_cast<PAL::FP16>(trackPos - WINDOW_MISS);
        while (m_readHead < noteCount) {
            const Note&  note  = m_activeChart->notes[m_readHead];
            NoteState&   state = m_noteStates[m_readHead];

            if (note.timeline >= missFloor) break;
            if (state.hitResult == 0) {
                state.hitResult = 3; // Missed completely
                ++result.missCount;
            }
            ++m_readHead;
        }
    }

    for (uint16_t i = m_readHead; i < noteCount; ++i) {
        const Note& note  = m_activeChart->notes[i];
        NoteState&  state = m_noteStates[i];

        // Break early if block object remains further back along the timeline horizon
        if (note.timeline > trackPos) break;
        if (state.hitResult != 0) continue;

        // SPATIAL COLLISION ASSESSMENT (Evaluate ship's track coordinate on impact)
        if (m_shipLane == note.lane) {
            state.hitResult = 1; // Scooped block successfully!
            ++result.perfectCount;
            spawnBurst(note.lane, BURST_COUNT);
            pushBlockToGrid(note.lane, note.flags);
        } else {
            state.hitResult = 3; // Avoided / missed block track slot position
            ++result.missCount;
        }
    }

    return result;
}

void GameEngine::updateGameplaySimulation(PAL::InputState pressed) {
    // --- DRIFT-RESISTANT MONOTONIC CLOCK SYNCHRONIZATION LAYER ---
#ifdef __DREAMCAST__
    m_syncTickCounter++;
    if ((m_syncTickCounter & 0x03) == 0) {
        uint16_t hardwarePos = s_audio->getTrackProgress();
        m_localTrackAccumulator = static_cast<PAL::FP16>(hardwarePos);
    } else {
        uint32_t predictedTrack = static_cast<uint32_t>(m_localTrackAccumulator) + 0x0440u;
        m_localTrackAccumulator = (predictedTrack > 0xFFFFu) ? static_cast<PAL::FP16>(0xFFFF) : static_cast<PAL::FP16>(predictedTrack);
    }
#else
    double audioCurrentTime = EM_ASM_DOUBLE({ return window._audioEl ? window._audioEl.currentTime : 0; });
    double audioDuration    = EM_ASM_DOUBLE({ return window._trackDur || 1; });

    if (audioCurrentTime > 0.0 && !s_audio->isPaused()) {
        uint32_t predictedTrack = static_cast<uint32_t>(m_localTrackAccumulator) + 0x0440u;
        uint32_t trueHardwareTrack = static_cast<uint32_t>((audioCurrentTime / audioDuration) * 65535.0);
        int32_t clockDrift = static_cast<int32_t>(predictedTrack) - static_cast<int32_t>(trueHardwareTrack);
        
        if (clockDrift > 0x0A00 || clockDrift < -0x0A00) {
            predictedTrack = (predictedTrack + trueHardwareTrack) >> 1; 
        }
        
        m_localTrackAccumulator = (predictedTrack > 0xFFFFu) ? static_cast<PAL::FP16>(0xFFFF) : static_cast<PAL::FP16>(predictedTrack);
    }
#endif

    if (m_localTrackAccumulator >= 0xFFFF) {
        s_audio->stop();
        m_currentState = EngineState::ResultsScreen;
        return;
    }

    // ── AUDIOSURF VEHICLE HANDLING CONTROLS INTERPRETATION LAYER ──────────────────
    // Left or Right updates ship column registry indexes instantly (Bounded 0 to 2)
    if (pressed & LANE_MASKS[0]) { // Digital/Button Left Mapping
        if (m_shipLane > 0) --m_shipLane;
    }
    if (pressed & LANE_MASKS[1]) { // Digital/Button Right Mapping
        if (m_shipLane < 2) ++m_shipLane;
    }

    // --- EXECUTE TICK SIMULATION STEP ---
    const FrameResult fr = evaluateChart(m_localTrackAccumulator, 0); // Disables button-timing inputs

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
            if (pressed & LANE_MASKS[0]) { 
                if (m_selectedSong > 0) --m_selectedSong;
            }
            if (pressed & LANE_MASKS[1]) { 
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

    // Set how far out along the perspective plane our camera visibility line reaches
    const uint32_t viewLookAheadRaw = static_cast<uint32_t>(m_localTrackAccumulator) + 0x2400u;
    const uint16_t viewLookAhead    = (viewLookAheadRaw > 0xFFFFu) ? 0xFFFFu : static_cast<uint16_t>(viewLookAheadRaw);

    // ── 1. Draw Rad Racer Style Vector Highway Corridor ─────────────────────────────
    // Generates the forward tapering projection floor vector lanes
    const PAL::SFP16 height = static_cast<PAL::SFP16>(energy << 7);
    s_graphics->drawVoxelHighway(
        static_cast<PAL::SFP16>(320 << 8), static_cast<PAL::SFP16>(200 << 8), // Horizon Center Vanishing Point
        static_cast<PAL::SFP16>(0   << 8), height,
        m_selectedSong
    );

    // ── 2. Draw Ship Vehicle Model (3D Polygon Matrix on 2D plane) ───────────
    // Render user ship gliding across lanes 0, 1, or 2 at bottom impact threshold
    PAL::SFP16 shipX = LANE_X[m_shipLane];
    s_graphics->drawShipVehicle(shipX, LANE_HIT_Y, m_shipLane);

    // ── 3. Draw Oncoming Block Stream (Pseudo-3D Projection Logic) ─────────────
    if (m_isStreamingMode) {
        uint16_t scanIdx = m_streamHead;
        const uint16_t limitIdx = m_streamTail;
        
        while (scanIdx != limitIdx) {
            uint16_t idx = scanIdx & RING_BUFFER_MASK;
            const Note& note = m_streamingNotes[idx];

            if (note.timeline > viewLookAhead) break;

            if (m_streamingNoteStates[idx].hitResult == 0) {
                // Compute Spatial Perspective scaling coefficients: Z = Timeline Delta
                int32_t zDistance = static_cast<int32_t>(note.timeline) - static_cast<int32_t>(m_localTrackAccumulator);
                if (zDistance > 0) {
                    // Perspective projection formula: Scale components down as distance increases
                    PAL::SFP16 visualScale = static_cast<PAL::SFP16>((0x100000 / (zDistance + 1)) >> 8);
                    PAL::SFP16 projectedX  = static_cast<PAL::SFP16>(320 + (((LANE_X[note.lane] - 320) * visualScale) >> 8));
                    PAL::SFP16 projectedY  = static_cast<PAL::SFP16>(200 + (((LANE_HIT_Y - 200) * visualScale) >> 8) + (energy << 2));

                    s_graphics->drawBlock3D(projectedX, projectedY, visualScale, note.flags);
                }
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
                int32_t zDistance = static_cast<int32_t>(note.timeline) - static_cast<int32_t>(m_localTrackAccumulator);
                
                if (zDistance > 0) {
                    // Standard division perspective layout simulation mapping
                    PAL::SFP16 visualScale = static_cast<PAL::SFP16>((0x100000 / (zDistance + 1)) >> 8);
                    PAL::SFP16 projectedX  = static_cast<PAL::SFP16>(320 + (((LANE_X[note.lane] - 320) * visualScale) >> 8));
                    PAL::SFP16 projectedY  = static_cast<PAL::SFP16>(200 + (((LANE_HIT_Y - 200) * visualScale) >> 8) + (energy << 2));

                    s_graphics->drawBlock3D(projectedX, projectedY, visualScale, note.flags);
                }
            }
            scanIdx++;
        }
    }

    // ── 4. Draw Match-3 Inventory HUD Grid (Cargo Storage Stack) ───────────────
    // Tightly drawn directly inside/adjacent to your ship HUD space
    for (uint8_t l = 0; l < 3; ++l) {
        uint16_t laneData = m_puzzleGrid[l];
        for (uint8_t r = 0; r < m_gridHeights[l]; ++r) {
            uint8_t cellType = (laneData >> (r * 3)) & 0x07;
            if (cellType == 0) continue;

            // Render inventory storage block elements stacked neatly inside HUD matrix
            PAL::SFP16 hudX = static_cast<PAL::SFP16>((500 + (l * GRID_BLOCK_SPACING)) << 8);
            PAL::SFP16 hudY = static_cast<PAL::SFP16>(GRID_BASE_Y - (r * GRID_BLOCK_SPACING));
            s_graphics->drawBlock(hudX, hudY, static_cast<PAL::SFP16>(1 << 8), cellType);
        }
    }

    // ── 5. Particle Burst Engine Render Stream ───────────────────────────────
    renderParticles();

    // ── 6. Standard Core HUD Interface Display ───────────────────────────────
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
