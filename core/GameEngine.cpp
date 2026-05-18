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

    return true;
}

void GameEngine::loadChart(const NoteChart* chart) {
    m_activeChart = chart;
    m_readHead    = 0;

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
}

// =============================================================================
// ZERO-ALLOCATION LOW-LATENCY PARTICLE STRUCT MECHANICS (Simulation Modification Pass)
// =============================================================================

void GameEngine::spawnBurst(uint8_t lane, uint8_t count) {
    if (lane >= 4) return;
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
// DUAL-PASS FORWARD-ONLY CHART VECTOR WORKER (Simulation Path)
// =============================================================================

FrameResult GameEngine::evaluateChart(PAL::FP16 trackPos, PAL::InputState pressed) {
    FrameResult result = {0, 0, 0, 0, 0};
    if (!m_activeChart) return result;

    const uint16_t noteCount = (m_activeChart->noteCount < MAX_NOTES_PER_CHART)
                               ? m_activeChart->noteCount : MAX_NOTES_PER_CHART;
    if (m_readHead >= noteCount) return result;

    // ─── PASS 1: FORCED-MISS HEAD REAPER ─────────────────────────────────────
    if (trackPos > WINDOW_MISS) {
        const PAL::FP16 missFloor = static_cast<PAL::FP16>(trackPos - WINDOW_MISS);
        
        while (m_readHead < noteCount) {
            const Note&  note  = m_activeChart->notes[m_readHead];
            NoteState&   state = m_noteStates[m_readHead];

            if (note.timeline >= missFloor) break;
            
            if (state.hitResult == 0) {
                state.hitResult = 3; // Miss flag assigned
                ++result.missCount;
            }
            ++m_readHead;
        }
    }

    // ─── PASS 2: WINDOW RANGE SCANNED LOOKAHEAD (Handles Multi-Lane Chords) ──
    const uint32_t  lookAheadRaw = static_cast<uint32_t>(trackPos) + WINDOW_GOOD;
    const PAL::FP16 lookAhead    = (lookAheadRaw > 0xFFFFu)
                                 ? static_cast<PAL::FP16>(0xFFFF) : static_cast<PAL::FP16>(lookAheadRaw);

    for (uint16_t i = m_readHead; i < noteCount; ++i) {
        const Note& note  = m_activeChart->notes[i];
        NoteState&  state = m_noteStates[i];

        if (note.timeline > lookAhead) break;

        // One AND bitmask check interface executes lane evaluations instantly
        const bool laneBit = (pressed & LANE_MASKS[note.lane]) != 0;
        const bool isHold  = (note.holdLength > 0);
        const PAL::FP16 delta = fp16AbsDelta(trackPos, note.timeline);

        // --- SECTION A: TAP NOTE EVAL REGISTER ---
        if (!isHold) {
            if (state.hitResult != 0) continue; 
            if (laneBit) {
                if (delta <= WINDOW_PERFECT) {
                    state.hitResult = 1;
                    ++result.perfectCount;
                    spawnBurst(note.lane, BURST_COUNT);
                } else if (delta <= WINDOW_GOOD) {
                    state.hitResult = 2;
                    ++result.goodCount;
                    spawnBurst(note.lane, BURST_COUNT / 2u);
                }
            }
            continue;
        }

        // --- SECTION B: SUSTAINED HOLD CONTINUOUS PROCESSING ---
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
                    state.hitResult = 3; // Miss register triggered
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

    const FrameResult fr = evaluateChart(trackPos, pressed);

    const uint8_t totalHits  = fr.perfectCount + fr.goodCount;
    const uint8_t totalMiss  = fr.missCount    + fr.holdDropCount;

    m_score     += static_cast<uint16_t>(fr.perfectCount * 300u) + static_cast<uint16_t>(fr.goodCount * 100u);
    m_combo      = (totalMiss > 0) ? 0u : static_cast<uint16_t>(m_combo + totalHits);
    m_missCount += totalMiss;

    // Step 4: Tick physics updates strictly on the simulation boundary line!
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
                loadChart(kSongTable[m_selectedSong]);
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
// STREAM RENDERING GENERATION PIPELINE (Strict Const Read-Only Boundary Execution)
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

        // Bitwise right shift optimized division elimination patch (Transforms /16 to >>4)
        const uint8_t alpha = static_cast<uint8_t>((static_cast<uint16_t>(m_particles[i].lifetime) * 255u) >> 4);

        s_graphics->drawParticle(
            m_particles[i].posX, m_particles[i].posY,
            static_cast<PAL::SFP16>(2 << 8), // Placed cleanly in front of highway layers
            m_particles[i].colorIndex, alpha
        );
    }
}

void GameEngine::renderGameplayScene() const {
    const PAL::FP16 trackPos = static_cast<PAL::FP16>(s_audio->getTrackProgress());

    // ── 1. Draw Static Highway Lane Footprint Corridor ───────────────────────
    const uint8_t energy = s_audio->getEnergyLevel();
    const PAL::SFP16 height = static_cast<PAL::SFP16>(energy << 7);
    s_graphics->drawVoxelColumn(
        static_cast<PAL::SFP16>(200 << 8), static_cast<PAL::SFP16>(440 << 8),
        static_cast<PAL::SFP16>(0   << 8), height,
        static_cast<PAL::SFP16>(20  << 8), m_selectedSong
    );

    // ── 2. Active Hold Lane Glow (Read-Only Scan) ────────────────────────────
    if (m_activeChart) {
        const uint16_t noteCount = (m_activeChart->noteCount < MAX_NOTES_PER_CHART)
                                   ? m_activeChart->noteCount : MAX_NOTES_PER_CHART;

        for (uint16_t i = m_readHead; i < noteCount; ++i) {
            if (m_noteStates[i].holdPhase != HoldState::Gating) continue;
            const uint8_t lane = m_activeChart->notes[i].lane;
            if (lane >= 4) continue;

            s_graphics->drawVoxelColumnGlow(
                static_cast<PAL::SFP16>(LANE_X[lane] - (8 << 8)),
                static_cast<PAL::SFP16>(LANE_X[lane] + (8 << 8)),
                static_cast<PAL::SFP16>(100 << 8), LANE_HIT_Y,
                static_cast<PAL::SFP16>(5 << 8), lane, 180
            );
        }

        // ── 3. Streaming Note Blocks Lookahead Engine (Visual Vector Restoration Patch) ──
        uint16_t scanIdx = m_readHead;
        while (scanIdx < noteCount && m_activeChart->notes[scanIdx].timeline <= (trackPos + 0x1C00)) {
            // Only draw notes that are pending evaluation
            if (m_noteStates[scanIdx].hitResult == 0) {
                const Note& note = m_activeChart->notes[scanIdx];

                int32_t delta = static_cast<int32_t>(note.timeline) - static_cast<int32_t>(trackPos);

                PAL::SFP16 visualZ = static_cast<PAL::SFP16>((delta * 24) >> 8);
                PAL::SFP16 targetY = LANE_HIT_Y - (visualZ << 2) + (energy << 3);

                s_graphics->drawBlock(LANE_X[note.lane], targetY, visualZ, note.lane);
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
