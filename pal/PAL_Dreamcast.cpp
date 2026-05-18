// =============================================================================
// PAL_Dreamcast.cpp
// Sega Dreamcast Hardware Implementation Layer (KallistiOS Architecture)
// =============================================================================

#include "PAL_Dreamcast.h"
#include <dc/pvr.h>
#include <dc/sound/sfx.h>
#include <dc/cdrom.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>

namespace PAL {

// =============================================================================
// GRAPHICS INTERFACE IMPLEMENTATION (PowerVR2 Hardware Direct)
// =============================================================================

bool DreamcastGraphics::init(uint16_t width, uint16_t height) {
    // Initialize the PowerVR graphics pipeline with standard performance settings
    pvr_init_params_t pvr_params = {
        { PVR_BIN_OPAQUE, PVR_BIN_UNUSED, PVR_BIN_TRANSLUCENT, PVR_BIN_UNUSED, PVR_BIN_UNUSED },
        512 * 1024 // 512KB Vertex Buffer allocation Allocation
    };
    
    if (pvr_init(&pvr_params) != 0) return false;
    
    m_width = width;
    m_height = height;
    return true;
}

void DreamcastGraphics::beginFrame() {
    pvr_wait_ready();
    pvr_scene_begin();
    
    // Begin submitting opaque primitives first
    pvr_list_begin(PVR_LIST_OP_POLY);
}

void DreamcastGraphics::endFrame() {
    pvr_scene_finish();
}

void DreamcastGraphics::drawBlock(SFP16 x, SFP16 y, SFP16 z, uint8_t styleIndex) {
    pvr_vertex_t vert;
    
    // Convert 8.8 fixed-point variables to local native floats for hardware processing
    float fx = static_cast<float>(x) / 256.0f;
    float fy = static_cast<float>(y) / 256.0f;
    float fz = static_cast<float>(z) / 256.0f;
    float size = 16.0f; // Native display block bounding box scale bounds

    // Setup an untextured raw color primitive descriptor polygon header
    pvr_poly_hdr_t hdr;
    pvr_poly_cxt_t cxt;
    pvr_poly_cxt_col(&cxt, PVR_LIST_OP_POLY);
    pvr_poly_compile(&hdr, &cxt);
    pvr_prim(&hdr, sizeof(hdr));

    uint32_t color = 0xFFFFFFFF;
    switch(styleIndex) {
        case 1: color = 0xFFFF0000; break; // Red Block
        case 2: color = 0xFF00FF00; break; // Green Block
        case 3: color = 0xFF0000FF; break; // Blue Block Obstacle
        default: color = 0xFFFFFF00; break;
    }

    // Submit Opaque Triangle Strip coordinates directly to the TA (Tile Accelerator)
    vert.flags = PVR_VERTEX_NORMAL;
    vert.x = fx - size; vert.y = fy + size; vert.z = fz; vert.argb = color; pvr_prim(&vert, sizeof(vert));
    vert.x = fx - size; vert.y = fy - size; vert.z = fz; vert.argb = color; pvr_prim(&vert, sizeof(vert));
    vert.x = fx + size; vert.y = fy + size; vert.z = fz; vert.argb = color; pvr_prim(&vert, sizeof(vert));
    vert.flags = PVR_VERTEX_EOL;
    vert.x = fx + size; vert.y = fy - size; vert.z = fz; vert.argb = color; pvr_prim(&vert, sizeof(vert));
}

void DreamcastGraphics::drawVoxelColumn(SFP16 x1, SFP16 x2, SFP16 y1, SFP16 y2, SFP16 z, uint8_t songId) {
    pvr_vertex_t vert;
    pvr_poly_hdr_t hdr;
    pvr_poly_cxt_t cxt;
    pvr_poly_cxt_col(&cxt, PVR_LIST_OP_POLY);
    pvr_poly_compile(&hdr, &cxt);
    pvr_prim(&hdr, sizeof(hdr));

    float fx1 = static_cast<float>(x1) / 256.0f;
    float fx2 = static_cast<float>(x2) / 256.0f;
    float fy1 = static_cast<float>(y1) / 256.0f;
    float fy2 = static_cast<float>(y2) / 256.0f;
    float fz  = static_cast<float>(z)  / 256.0f;

    uint32_t trackColor = 0xAA444444; // Solid Core Highway Foundation Colors

    vert.flags = PVR_VERTEX_NORMAL;
    vert.x = fx1; vert.y = fy2; vert.z = fz; vert.argb = trackColor; pvr_prim(&vert, sizeof(vert));
    vert.x = fx1; vert.y = fy1; vert.z = fz; vert.argb = trackColor; pvr_prim(&vert, sizeof(vert));
    vert.x = fx2; vert.y = fy2; vert.z = fz; vert.argb = trackColor; pvr_prim(&vert, sizeof(vert));
    vert.flags = PVR_VERTEX_EOL;
    vert.x = fx2; vert.y = fy1; vert.z = fz; vert.argb = trackColor; pvr_prim(&vert, sizeof(vert));
}

void DreamcastGraphics::drawParticle(SFP16 x, SFP16 y, SFP16 z, uint8_t colorIndex, uint8_t alpha) {
    // --- PIPELINE OVERRIDE: SWITCH LIST TYPE CONTEXT TO TRANSLUCENT ---
    static bool inTranslucentList = false;
    if (!inTranslucentList) {
        pvr_list_begin(PVR_LIST_TR_POLY);
        inTranslucentList = true;
    }

    pvr_vertex_t vert;
    pvr_poly_hdr_t hdr;
    pvr_poly_cxt_t cxt;
    
    // Additive alpha blending parameters initialized natively on the PVR2 system
    pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
    pvr_poly_compile(&hdr, &cxt);
    pvr_prim(&hdr, sizeof(hdr));

    float fx = static_cast<float>(x) / 256.0f;
    float fy = static_cast<float>(y) / 256.0f;
    float fz = static_cast<float>(z) / 256.0f;
    float radius = 4.0f;

    // Pack translucent parameters dynamically matching the look-up calculation array
    uint32_t baseColor = 0x00FFFFFF;
    if (colorIndex == 0)      baseColor = 0x00FF0000;
    else if (colorIndex == 1) baseColor = 0x0000FF00;
    else                      baseColor = 0x000000FF;

    uint32_t mixedAlphaColor = (static_cast<uint32_t>(alpha) << 24) | baseColor;

    vert.flags = PVR_VERTEX_NORMAL;
    vert.x = fx - radius; vert.y = fy + radius; vert.z = fz; vert.argb = mixedAlphaColor; pvr_prim(&vert, sizeof(vert));
    vert.x = fx - radius; vert.y = fy - radius; vert.z = fz; vert.argb = mixedAlphaColor; pvr_prim(&vert, sizeof(vert));
    vert.x = fx + radius; vert.y = fy + radius; vert.z = fz; vert.argb = mixedAlphaColor; pvr_prim(&vert, sizeof(vert));
    vert.flags = PVR_VERTEX_EOL;
    vert.x = fx + radius; vert.y = fy - radius; vert.z = fz; vert.argb = mixedAlphaColor; pvr_prim(&vert, sizeof(vert));
}

void DreamcastGraphics::drawVoxelColumnGlow(SFP16 x1, SFP16 x2, SFP16 y1, SFP16 y2, SFP16 z, uint8_t colorIndex, uint8_t alpha) {
    pvr_vertex_t vert;
    pvr_poly_hdr_t hdr;
    pvr_poly_cxt_t cxt;
    pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
    pvr_poly_compile(&hdr, &cxt);
    pvr_prim(&hdr, sizeof(hdr));

    float fx1 = static_cast<float>(x1) / 256.0f;
    float fx2 = static_cast<float>(x2) / 256.0f;
    float fy1 = static_cast<float>(y1) / 256.0f;
    float fy2 = static_cast<float>(y2) / 256.0f;
    float fz  = static_cast<float>(z)  / 256.0f;

    uint32_t glowColor = (static_cast<uint32_t>(alpha) << 24) | 0x0000FFFF;

    vert.flags = PVR_VERTEX_NORMAL;
    vert.x = fx1; vert.y = fy2; vert.z = fz; vert.argb = glowColor; pvr_prim(&vert, sizeof(vert));
    vert.x = fx1; vert.y = fy1; vert.z = fz; vert.argb = glowColor; pvr_prim(&vert, sizeof(vert));
    vert.x = fx2; vert.y = fy2; vert.z = fz; vert.argb = glowColor; pvr_prim(&vert, sizeof(vert));
    vert.flags = PVR_VERTEX_EOL;
    vert.x = fx2; vert.y = fy1; vert.z = fz; vert.argb = glowColor; pvr_prim(&vert, sizeof(vert));
}

void DreamcastGraphics::drawHUD(uint16_t score, uint16_t combo, uint8_t misses, uint8_t flags) {
    // HUD processing uses custom flat punch-through primitives on top of the scene
    // Direct hardware text rendering logic can be bound here using standard bios routines
}

// =============================================================================
// AUDIO INTERFACE IMPLEMENTATION (KallistiOS CDDA / Hardware AICA)
// =============================================================================

bool DreamcastAudio::init() {
    snd_init(); // Initialize the AICA sound processor chip parameters
    m_currentTrack = 0;
    m_isLooping = false;
    m_cachedProgress = 0;
    return true;
}

void DreamcastAudio::play(uint8_t songId) {
    m_currentTrack = songId + 2; // CDDA Tracks typically offset past target data sectors
    cdrom_cdda_play(m_currentTrack, m_currentTrack, 1, CDDA_TRACKS_LOOP);
    m_cachedProgress = 0;
}

void DreamcastAudio::stop() {
    cdrom_cdda_stop();
}

FP16 DreamcastAudio::getTrackProgress() {
    int status, sectorType;
    CDROM_STATUS(&status, &sectorType);

    // If CDDA is playing, extract track positions directly out of the sub-channel registers
    if (status == CD_STATUS_PLAYING) {
        uint32_t currentSectors = cdrom_cdda_get_each_pos();
        
        // Normalize linear scaling to fit standard engine scale ranges (0x0000 - 0xFFFF)
        uint32_t calculatedProgress = (currentSectors * 0x0440u) >> 4; 
        m_cachedProgress = static_cast<FP16>(calculatedProgress > 0xFFFFu ? 0xFFFFu : calculatedProgress);
    }
    return m_cachedProgress;
}

uint8_t DreamcastAudio::getEnergyLevel() {
    // Return mock energy values matching the audio step bounds if hardware envelopes aren't queried
    return 0x0F; 
}

// =============================================================================
// INPUT INTERFACE IMPLEMENTATION (Maple Bus Controller Scan Protocol)
// =============================================================================

bool DreamcastInput::init() {
    // Maple hardware components handle configuration implicitly inside basic KOS routines
    return true;
}

void DreamcastInput::poll() {
    maple_device_t* cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    m_currentRawState = 0;

    if (cont) {
        cont_state_t* state = (cont_state_t*)maple_dev_status(cont);
        if (state) {
            // Map the physical buttons cleanly over to the cross-platform framework
            if (state->buttons & CONT_DPAD_LEFT)  m_currentRawState |= static_cast<InputState>(InputAction::LaneLeft);
            if (state->buttons & CONT_DPAD_RIGHT) m_currentRawState |= static_cast<InputState>(InputAction::LaneRight);
            if (state->buttons & CONT_A)          m_currentRawState |= static_cast<InputState>(InputAction::Confirm);
        }
    }
}

InputState DreamcastInput::readPressedActions() {
    return m_currentRawState;
}

} // namespace PAL
