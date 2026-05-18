#include "GameEngine.h"
#include "charts/SongIndex.h" // Provides linkage to: extern const NoteChart* kSongTable[TOTAL_SONGS];

namespace Engine {

// =============================================================================
// MOTOROLA/SH-4 OPTIMIZED FIXED-POINT MATH REGISTER UTILITIES
// =============================================================================

PAL::FP16 GameEngine::fp16AbsDelta(PAL::FP16 a, PAL::FP16 b) {
    // Branchless delta execution to guarantee pipeline performance boundaries
    int32_t d = static_cast<int32_t>(a) - static_cast<int32_t>(b);
    if (d < 0) d = -d;
    return static_cast<PAL::FP16>(d & 0xFFFF);
}

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

    // Flush the particle array buffer cells cleanly
    for (uint8_t i = 0; i < MAX_PARTICLES; ++i) {
        m_particles[i].lifetime = 0;
    }

    // Initialize Puzzle Board State Matrix to Empty
    resetPuzzleGrid();

    m_isRunning = true; // Secure explicit operational state state loop boundary
    return true;
}

void GameEngine::loadChart(const NoteChart* chart) {
    m_activeChart = chart;
    m_readHead    = 0;
    m_isStreamingMode = (chart == nullptr); // Nullptr means we are processing a live CD-DA audio stream

    // Reset ring buffer bounds for live procedural tracking
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
    for (uint8_t lane = 0; lane < 3; ++lane) {
        m_gridHeights[lane] = 0;
        for (uint8_t row = 0; row < GRID_MAX_ROWS; ++row) {
            m_puzzleGrid[lane][row] = 0; // 0 = Empty cell
        }
    }
}

// =============================================================================
// ZERO-ALLOCATION LOW-LATENCY PARTICLE STRUCT MECHANICS
// =============================================================================

void GameEngine::spawnBurst(uint8_t lane, uint8_t count) {
    if (lane >= 3) return; // Restrict strictly to valid puzzle board lane arrays
    uint8_t spawned = 0;

    for (uint8_t i = 0; i < MAX_PARTICLES && spawned < count; ++i) {
        if (m_particles[i].lifetime != 0) continue;

        // Fanned horizontal coordinate positioning calculated purely via shifts
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
// PUZZLE MATCHING ENGINE CORE (Zero-Allocation Array-Backed Matrix Logic)
// =============================================================================

bool GameEngine::pushBlockToGrid(uint8_t lane, uint8_t blockType) {
    if (lane >= 3) return false;

    // Handle Grey Obstacles clogging up the grid architecture directly
    if (blockType == 3) {
        if (m_gridHeights[lane] < GRID_MAX_ROWS) {
            m_puzzleGrid[lane][m_gridHeights[lane]] = 3;
            ++m_gridHeights[lane];
        }
        return (m_gridHeights[lane] >= GRID_MAX_ROWS); // Returns true if overfilled
    }

    // Normal Color Block Stack Placement
    if (m_gridHeights[lane] < GRID_MAX_ROWS) {
        m_puzzleGrid[lane][m_gridHeights[lane]] = blockType;
        ++m_gridHeights[lane];
        
        // Scan matrix for Match-3 instances instantly upon column cell adjustment
        checkAndResolveMatches();
    }

    return (m_gridHeights[lane] >= GRID_MAX_ROWS);
}

void GameEngine::checkAndResolveMatches() {
    bool foundMatch = false;
    bool marked[3][6] = { {false} };

    // 1. Scan Vertical Stacks for 3-in-a-row
    for (uint8_t l = 0; l < 3; ++l) {
        if (m_gridHeights[l] < 3) continue;
        for (uint8_t r = 0; r <= m_gridHeights[l] - 3; ++r) {
            uint8_t color = m_puzzleGrid[l][r];
            if (color == 0 || color == 3) continue; // Skip empty and grey blocks
            if (m_puzzleGrid[l][r+1] == color && m_puzzleGrid[l][r+2] == color) {
                marked[l][r] = true;   marked[l][r+1] = true; marked[l][r+2] = true;
                foundMatch = true;
            }
        }
    }

    // 2. Scan Horizontal Lanes for 3-in-a-row
    for (uint8_t r = 0; r < GRID_MAX_ROWS; ++r) {
        uint8_t color = m_puzzleGrid[0][r];
        if (color == 0 || color == 3) continue;
        if (m_puzzleGrid[1][r] == color && m_puzzleGrid[2][r] == color) {
            marked[0][r] = true; marked[1][r] = true; marked[2][r] = true;
            foundMatch = true;
        }
    }

    // 3. Clear Blocks and Compress Stacks Downward (Shift Elimination)
    if (foundMatch) {
        uint8_t clearedCount = 0;
        for (uint8_t l = 0; l < 3; ++l) {
            uint8_t writeIdx = 0;
            for (uint8_t r = 0; r < m_gridHeights[l]; ++r) {
                if (marked[l][r]) {
                    ++clearedCount;
                    continue; // Erase cell via skip omission
                }
                m_puzzleGrid[l][writeIdx] = m_puzzleGrid[l][r];
                ++writeIdx;
            }
            // Clear out leftover top cells
            for (uint8_t r = writeIdx; r < m_gridHeights[l]; ++r) {
                m_puzzleGrid[l][r] = 0;
            }
            m_gridHeights[l] = writeIdx;
        }

        // Apply scoring mechanics directly into the frame counters
        m_score += static_cast<uint16_t>(clearedCount * 500u * (m_combo > 0 ? (m_combo >> 2) + 1 : 1));
        spawnBurst(1, clearedCount * 2); // Explode center stage particles on clear
    }
}

// =============================================================================
// HYBRID MODE FORWARD CHART VECTOR WORKER (Supports Pointer & Ring Buffers)
// =============================================================================

FrameResult GameEngine::evaluateChart(PAL::FP16 trackPos, PAL::InputState pressed) {
    FrameResult result = {0, 0, 0, 0, 0};

    if (m_isStreamingMode) {
        // ─────────────────────────────────────────────────────────────────────
        // STREAMING MODE PATHWAY: PROCESS RING BUFFER NOTES
        // ─────────────────────────────────────────────────────────────────────
        if (m_streamHead == m_streamTail) return result;

        // Pass 1: Reaper check for out-of-bounds streaming blocks
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

        // Pass 2: Lookahead Evaluation scan through Active Ring elements
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
                    
                    // Route to Match-3 matrix board layout
                    bool crashed = pushBlockToGrid(note.lane, note.flags); 
                    if (crashed) ++result.holdDropCount; // Overfill penalization
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

    // ─────────────────────────────────────────────────────────────────────
    // STATIC ALBUM PATHWAY: CLASSIC FORWARD CHART VECTOR SCANNER
    // ─────────────────────────────────────────────────────────────────────
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
    const PAL::FP16 lookAhead    = (lookAheadRaw > 0xFFFFu)
                                  ? static_cast<PAL::FP16>(0xFFFF) : static_cast<PAL::FP16>(lookAheadRaw);

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
                    pushBlockToGrid(note.lane, note.flags); // Process match-3 mechanics
                } else if (delta <= WINDOW_GOOD) {
                    state.hitResult = 2;
                    ++result.goodCount;
                    spawnBurst(note.lane, BURST_COUNT / 2u);
                    pushBlockToGrid(note.lane, note.flags);
                }
            }
            continue;
        }

        // Sustain Hold Evaluation Blocks
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
                const PAL::FP16 holdEnd   = (holdEndRaw > 0xFFFFu)
                                            ? static_cast<PAL::FP16>(0xFFFF) : static_cast<PAL::FP16>(holdEndRaw);

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
    const PAL::FP16 trackPos = static_cast<PAL::FP16>(s_audio->getTrackProgress());

    if (trackPos >= 0xFFFF) {
        s_audio->stop();
        m_currentState = EngineState::ResultsScreen;
        return;
    }

    // Call our unified, hybrid calculation loop
    const FrameResult fr = evaluateChart(trackPos, pressed);

    const uint8_t totalHits  = fr.perfectCount + fr.goodCount;
    const uint8_t totalMiss  = fr.missCount    + fr.holdDropCount;

    m_score     += static_cast<uint16_t>(fr.perfectCount * 300u) + static_cast<uint16_t>(fr.goodCount * 100u);
    m_combo      = (totalMiss > 0) ? 0u : static_cast<uint16_t>(m_combo + totalHits);
    m_missCount += totalMiss;

    // Tick layout particle updates safely on simulation borders
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
                
                // If checking an official album track, load the normal chart list.
                // If selecting an external CD mode, pass nullptr to trigger streaming.
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

        const uint8_t alpha = static_cast<uint8_t>((static_cast<uint16_t>(m_particles[i].lifetime) * 255u) >> 4);

        s_graphics->drawParticle(
            m_particles[i].posX, m_particles[i].posY,
            static_cast<PAL::SFP16>(2 << 8), 
            m_particles[i].colorIndex, alpha
        );
    }
}

void GameEngine::renderGameplayScene() const {
    const PAL::FP16 trackPos = static_cast<PAL::FP16>(s_audio->getTrackProgress());
    const uint8_t energy = s_audio->getEnergyLevel();

    // Promote rendering timeline bounds to uint32 to prevent 16-bit wrap-around clipping
    const uint32_t viewLookAheadRaw = static_cast<uint32_t>(trackPos) + 0x1C00u;
    const uint16_t viewLookAhead    = (viewLookAheadRaw > 0xFFFFu) ? 0xFFFFu : static_cast<uint16_t>(viewLookAheadRaw);

    // ── 1. Draw Voxel Highway Footprint Corridor ─────────────────────────────
    const PAL::SFP16 height = static_cast<PAL::SFP16>(energy << 7);
    s_graphics->drawVoxelColumn(
        static_cast<PAL::SFP16>(200 << 8), static_cast<PAL::SFP16>(440 << 8),
        static_cast<PAL::SFP16>(0   << 8), height,
        static_cast<PAL::SFP16>(20  << 8), m_selectedSong
    );

    // ── 2. Draw Match-3 Matrix Grid Blocks (Bottom HUD Stack Display) ────────
    for (uint8_t l = 0; l < 3; ++l) {
        for (uint8_t r = 0; r < m_gridHeights[l]; ++r) {
            uint8_t cellType = m_puzzleGrid[l][r];
            if (cellType == 0) continue;

            // Render block layers stacked upwards from the bottom base board coordinates
            PAL::SFP16 gridX = LANE_X[l];
            PAL::SFP16 gridY = static_cast<PAL::SFP16>(GRID_BASE_Y - (r * GRID_BLOCK_SPACING));
            s_graphics->drawBlock(gridX, gridY, static_cast<PAL::SFP16>(1 << 8), cellType);
        }
    }

    // ── 3. Streaming Note Blocks Lookahead Engine ────────────────────────────
    if (m_isStreamingMode) {
        uint16_t scanIdx = m_streamHead;
        const uint16_t limitIdx = m_streamTail;
        
        while (scanIdx != limitIdx) {
            uint16_t idx = scanIdx & RING_BUFFER_MASK;
            const Note& note = m_streamingNotes[idx];

            if (note.timeline > viewLookAhead) break;

            if (m_streamingNoteStates[idx].hitResult == 0) {
                int32_t delta = static_cast<int32_t>(note.timeline) - static_cast<int32_t>(trackPos);
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
                int32_t delta = static_cast<int32_t>(note.timeline) - static_cast<int32_t>(trackPos);
                PAL::SFP16 visualZ = static_cast<PAL::SFP16>((delta * 24) >> 8);
                PAL::SFP16 targetY = LANE_HIT_Y - (visualZ << 2) + (energy << 3);

                s_graphics->drawBlock(LANE_X[note.lane], targetY, visualZ, note.flags);
            }
            scanIdx++;
        }
    }

    // ── 4. Particle Burst Engine Render Stream ───────────────────────────────
    renderParticles();

    // ── 5. Standard Core HUD Interface Display ───────────────────────────────
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
