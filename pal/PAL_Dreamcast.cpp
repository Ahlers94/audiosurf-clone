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

// ---------------------------------------------------------------------------
// Native SFP16 Conversion Helpers
// ---------------------------------------------------------------------------
static inline float sfp16_to_float(SFP16 val) {
    return static_cast<float>(val) / 256.0f;
}

// =============================================================================
// GRAPHICS INTERFACE IMPLEMENTATION (PowerVR2 Hardware Direct)
// =============================================================================

bool DreamcastGraphics::init(uint16_t width, uint16_t height) {
    // Correctly initialize all necessary hardware bins for sorting
    pvr_init_params_t pvr_params = {
        { PVR_BIN_CXT_OP, PVR_BIN_CXT_NONE, PVR_BIN_CXT_TR, PVR_BIN_CXT_NONE, PVR_BIN_CXT_PT },
        16 * 1024 * 1024 // Grant proper scratchpad VRAM pool space
    };
    
    if (pvr_init(&pvr_params) != 0) return false;

    m_width = width;
    m_height = height;
    m_currentList = PVR_LIST_OP_POLY;
    m_headerSubmitted = false;

    // ── Pre-Compile Opaque Geometric State Header ──
    pvr_poly_cxt_t opaque_cxt;
    pvr_poly_cxt_col(&opaque_cxt, PVR_LIST_OP_POLY);
    opaque_cxt.gen.shading = PVR_SHADE_FLAT;
    opaque_cxt.gen.culling = PVR_CULL_CCW;
    pvr_poly_compile(&m_opaqueHdr, &opaque_cxt);

    // ── Pre-Compile Translucent/Alpha State Header ──
    pvr_poly_cxt_t trans_cxt;
    pvr_poly_cxt_col(&trans_cxt, PVR_LIST_TR_POLY);
    trans_cxt.gen.shading = PVR_SHADE_FLAT;
    trans_cxt.gen.culling = PVR_CULL_CCW;
    trans_cxt.blend.src = PVR_BLEND_SRCALPHA;
    trans_cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
    pvr_poly_compile(&m_translucentHdr, &trans_cxt);

    return true;
}

void DreamcastGraphics::beginFrame() {
    pvr_scene_begin();
    pvr_list_begin(PVR_LIST_OP_POLY);
    m_currentList = PVR_LIST_OP_POLY;
    m_headerSubmitted = false; // Fresh strip sequence state
}

void DreamcastGraphics::endFrame() {
    pvr_list_end(); // Close whatever pipeline was handling active vertices
    pvr_scene_end();
}

// Internal inline method to cleanly cycle states without breaking frame pacing
inline void DreamcastGraphics::ensureList(pvr_list_t targetList, pvr_poly_hdr_t* targetHdr) {
    if (m_currentList != targetList) {
        pvr_list_end();
        pvr_list_begin(targetList);
        m_currentList = targetList;
        pvr_prim(targetHdr, sizeof(pvr_poly_hdr_t));
        m_headerSubmitted = true;
    } else if (!m_headerSubmitted) {
        pvr_prim(targetHdr, sizeof(pvr_poly_hdr_t));
        m_headerSubmitted = true;
    }
}

void DreamcastGraphics::drawBlock(SFP16 x, SFP16 y, SFP16 z, uint8_t styleIndex) {
    ensureList(PVR_LIST_OP_POLY, &m_opaqueHdr);

    float fx = sfp16_to_float(x);
    float fy = sfp16_to_float(y);
    float fz = sfp16_to_float(z);
    float size = 16.0f;

    uint32_t color = 0xFFFFFF00;
    switch(styleIndex) {
        case 1:  color = 0xFFFF0000; break;
        case 2:  color = 0xFF00FF00; break;
        case 3:  color = 0xFF0000FF; break;
        default: break;
    }

    pvr_vertex_t vert;
    vert.oargb = 0;
    vert.argb = color;

    vert.flags = PVR_VERTEX_NORMAL;
    vert.x = fx - size; vert.y = fy + size; vert.z = fz; pvr_prim(&vert, sizeof(vert));
    vert.x = fx - size; vert.y = fy - size; vert.z = fz; pvr_prim(&vert, sizeof(vert));
    vert.x = fx + size; vert.y = fy + size; vert.z = fz; pvr_prim(&vert, sizeof(vert));
    
    vert.flags = PVR_VERTEX_EOL;
    vert.x = fx + size; vert.y = fy - size; vert.z = fz; pvr_prim(&vert, sizeof(vert));
}

void DreamcastGraphics::drawVoxelColumn(SFP16 x1, SFP16 x2, SFP16 y1, SFP16 y2, SFP16 z, uint8_t songId) {
    ensureList(PVR_LIST_OP_POLY, &m_opaqueHdr);

    float fx1 = sfp16_to_float(x1);
    float fx2 = sfp16_to_float(x2);
    float fy1 = sfp16_to_float(y1);
    float fy2 = sfp16_to_float(y2);
    float fz  = sfp16_to_float(z);

    uint32_t trackColor = 0xAA444444;

    pvr_vertex_t vert;
    vert.oargb = 0;
    vert.argb = trackColor;

    vert.flags = PVR_VERTEX_NORMAL;
    vert.x = fx1; vert.y = fy2; vert.z = fz; pvr_prim(&vert, sizeof(vert));
    vert.x = fx1; vert.y = fy1; vert.z = fz; pvr_prim(&vert, sizeof(vert));
    vert.x = fx2; vert.y = fy2; vert.z = fz; pvr_prim(&vert, sizeof(vert));
    
    vert.flags = PVR_VERTEX_EOL;
    vert.x = fx2; vert.y = fy1; vert.z = fz; pvr_prim(&vert, sizeof(vert));
}

void DreamcastGraphics::drawParticle(SFP16 x, SFP16 y, SFP16 z, uint8_t colorIndex, uint8_t alpha) {
    ensureList(PVR_LIST_TR_POLY, &m_translucentHdr);

    float fx = sfp16_to_float(x);
    float fy = sfp16_to_float(y);
    float fz = sfp16_to_float(z);
    float radius = 4.0f;

    uint32_t baseColor = 0x00FFFFFF;
    if (colorIndex == 0)      baseColor = 0x00FF0000;
    else if (colorIndex == 1) baseColor = 0x0000FF00;
    else                      baseColor = 0x000000FF;
    
    uint32_t mixedAlphaColor = (static_cast<uint32_t>(alpha) << 24) | baseColor;

    pvr_vertex_t vert;
    vert.oargb = 0;
    vert.argb = mixedAlphaColor;

    vert.flags = PVR_VERTEX_NORMAL;
    vert.x = fx - radius; vert.y = fy + radius; vert.z = fz; pvr_prim(&vert, sizeof(vert));
    vert.x = fx - radius; vert.y = fy - radius; vert.z = fz; pvr_prim(&vert, sizeof(vert));
    vert.x = fx + radius; vert.y = fy + radius; vert.z = fz; pvr_prim(&vert, sizeof(vert));
    
    vert.flags = PVR_VERTEX_EOL;
    vert.x = fx + radius; vert.y = fy - radius; vert.z = fz; pvr_prim(&vert, sizeof(vert));
}

void DreamcastGraphics::drawVoxelColumnGlow(SFP16 x1, SFP16 x2, SFP16 y1, SFP16 y2, SFP16 z, uint8_t colorIndex, uint8_t alpha) {
    ensureList(PVR_LIST_TR_POLY, &m_translucentHdr);

    float fx1 = sfp16_to_float(x1);
    float fx2 = sfp16_to_float(x2);
    float fy1 = sfp16_to_float(y1);
    float fy2 = sfp16_to_float(y2);
    float fz  = sfp16_to_float(z);

    uint32_t glowColor = (static_cast<uint32_t>(alpha) << 24) | 0x0000FFFF;

    pvr_vertex_t vert;
    vert.oargb = 0;
    vert.argb = glowColor;

    vert.flags = PVR_VERTEX_NORMAL;
    vert.x = fx1; vert.y = fy2; vert.z = fz; pvr_prim(&vert, sizeof(vert));
    vert.x = fx1; vert.y = fy1; vert.z = fz; pvr_prim(&vert, sizeof(vert));
    vert.x = fx2; vert.y = fy2; vert.z = fz; pvr_prim(&vert, sizeof(vert));
    
    vert.flags = PVR_VERTEX_EOL;
    vert.x = fx2; vert.y = fy1; vert.z = fz; pvr_prim(&vert, sizeof(vert));
}

void DreamcastGraphics::drawHUD(uint16_t score, uint16_t combo, uint8_t misses, uint8_t flags) {
    // Bound using bios font metrics or custom static texture arrays later.
}

// =============================================================================
// AUDIO INTERFACE IMPLEMENTATION (KallistiOS CDDA / Hardware AICA)
// =============================================================================

bool DreamcastAudio::init() {
    snd_init(); 
    m_currentTrack = 0;
    m_cachedProgress = 0;
    return true;
}

void DreamcastAudio::play(uint8_t songId) {
    m_currentTrack = songId + 2; 
    cdrom_cdda_play(m_currentTrack, m_currentTrack, 1, CDDA_TRACKS_LOOP);
    m_cachedProgress = 0;
}

void DreamcastAudio::stop() {
    cdrom_cdda_stop();
}

FP16 DreamcastAudio::getTrackProgress() {
    int status, sectorType;
    CDROM_STATUS(&status, &sectorType);
    
    if (status == CD_STATUS_PLAYING) {
        uint32_t currentSectors = cdrom_cdda_get_each_pos();
        // Safe 8.8 scaling bounds scaling to stay within interface types cleanly
        uint32_t calculatedProgress = (currentSectors * 256) / 75; 
        m_cachedProgress = static_cast<FP16>(calculatedProgress > 0xFFFFu ? 0xFFFFu : calculatedProgress);
    }
    return m_cachedProgress;
}

uint8_t DreamcastAudio::getEnergyLevel() {
    return 0x0F; 
}

// =============================================================================
// INPUT INTERFACE IMPLEMENTATION (Maple Bus Controller Scan Protocol)
// =============================================================================

bool DreamcastInput::init() {
    return true;
}

void DreamcastInput::poll() {
    maple_device_t* cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    m_currentRawState = 0;
    
    if (cont) {
        cont_state_t* state = (cont_state_t*)maple_dev_status(cont);
        if (state) {
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
