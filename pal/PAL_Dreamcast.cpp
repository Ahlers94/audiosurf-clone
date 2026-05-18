// =============================================================================
// PAL_Dreamcast.cpp (Continued)
// =============================================================================

// Global LUT: block type/colorIndex → 32-bit packed PVR colors (ARGB)
static const uint32_t kColorLUT[8] = {
    0xFF00FF00, // 0: Green
    0xFFFF0000, // 1: Red
    0xFF0000FF, // 2: Blue
    0xFFFFFF00, // 3: Yellow (Obstacle)
    0xFFFF00FF, // 4: Magenta
    0xFF00FFFF, // 5: Cyan
    0xFFFFFFFF, // 6: White
    0xFF808080  // 7: Grey
};

// Simple 3x5 Pixel Font Data for HUD (Numbers 0-9, 'C', 'M', 'P', 'S')
// Each glyph fits in a 15-bit integer (5 rows x 3 bits)
static const uint16_t kFontLUT[14] = {
    0x7B6F, // 0
    0x2C9E, // 1
    0x73E7, // 2
    0x73CF, // 3
    0x5BE9, // 4
    0x79CF, // 5
    0x79EF, // 6
    0x7249, // 7
    0x7BEF, // 8
    0x7BCF, // 9
    0x7127, // C (Combo)
    0x5D6D, // M (Miss)
    0x7B24, // P (Perfect)
    0x794F  // S (Score)
};

// Inline helper for converting SFP16 fixed-point (1/256) to standard floating-point
static inline float sfp16_to_float(SFP16 val) {
    return static_cast<float>(val) / 256.0f;
}

// ---------------------------------------------------------------------------
// Graphics Implementation
// ---------------------------------------------------------------------------

bool Graphics::init(int width, int height) {
    // Initialize PVR subsystem with default video configuration
    pvr_init_params_t pvr_params = {
        { PVR_BIN_CXT_OP, PVR_BIN_CXT_NONE, PVR_BIN_CXT_TR, PVR_BIN_CXT_NONE, PVR_BIN_CXT_PT },
        16 * 1024 * 1024
    };
    
    if (pvr_init(&pvr_params) != 0) return false;

    // Compile Opaque Header (Flat shaded, CCW culling, no textures)
    pvr_poly_cxt_t opaque_cxt;
    pvr_poly_cxt_col(&opaque_cxt, PVR_LIST_OP_POLY);
    opaque_cxt.gen.shading = PVR_SHADE_FLAT;
    opaque_cxt.gen.culling = PVR_CULL_CCW;
    pvr_poly_compile(&s_opaque_hdr, &opaque_cxt);

    // Compile Translucent Header (Flat shaded, CCW culling, alpha blending)
    pvr_poly_cxt_t trans_cxt;
    pvr_poly_cxt_col(&trans_cxt, PVR_LIST_TR_POLY);
    trans_cxt.gen.shading = PVR_SHADE_FLAT;
    trans_cxt.gen.culling = PVR_CULL_CCW;
    trans_cxt.blend.src = PVR_BLEND_SRCALPHA;
    trans_cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
    pvr_poly_compile(&s_translucent_hdr, &trans_cxt);

    s_in_translucent = false;
    return true;
}

void Graphics::beginFrame() {
    pvr_scene_begin();
    pvr_list_begin(PVR_LIST_OP_POLY);
    s_in_translucent = false;
}

void Graphics::endFrame() {
    // Close whatever list segment we happen to still be handling natively
    pvr_list_end();
    pvr_scene_end();
}

// Intercept handles state changes inside the game engine execution thread safely
void switchToOpaqueList() {
    pvr_list_end();
    pvr_list_begin(PVR_LIST_OP_POLY);
    s_in_translucent = false;
}

void switchToTranslucentList() {
    pvr_list_end();
    pvr_list_begin(PVR_LIST_TR_POLY);
    s_in_translucent = true;
}

void switchToPunchThroughList() {
    pvr_list_end();
    pvr_list_begin(PVR_LIST_PT_POLY);
    s_in_translucent = false; 
}

// ── DRAW PARTICLE (Translucent Billboard Quad) ───────────────────────────────
void Graphics::drawParticle(SFP16 x, SFP16 y, SFP16 size, uint8_t color, uint8_t alpha) {
    float fx = sfp16_to_float(x);
    float fy = sfp16_to_float(y);
    float h_size = sfp16_to_float(size) * 0.5f;

    // Resolve color and override alpha parameter bits
    uint32_t argb = (kColorLUT[color & 0x07] & 0x00FFFFFF) | (static_cast<uint32_t>(alpha) << 24);

    pvr_vertex_t vert;
    vert.flags = PVR_VERTEX_NORMAL;
    vert.argb = argb;
    vert.oargb = 0;

    // Submit a standard screen-space facing billboard quad
    vert.x = fx - h_size; vert.y = fy + h_size; vert.z = 10.0f;
    pvr_prim(&vert, sizeof(vert));

    vert.x = fx - h_size; vert.y = fy - h_size; vert.z = 10.0f;
    pvr_prim(&vert, sizeof(vert));

    vert.x = fx + h_size; vert.y = fy + h_size; vert.z = 10.0f;
    pvr_prim(&vert, sizeof(vert));

    vert.flags = PVR_VERTEX_EOL;
    vert.x = fx + h_size; vert.y = fy - h_size; vert.z = 10.0f;
    pvr_prim(&vert, sizeof(vert));
}

// ── DRAW BLOCK (Unrolled 3D Polygon Cube Strip) ─────────────────────────────
void Graphics::drawBlock(SFP16 x, SFP16 y, SFP16 z, uint8_t type) {
    float fx = sfp16_to_float(x);
    float fy = sfp16_to_float(y);
    float fz = sfp16_to_float(z) + 1.0f; // Shift depth so near plane scales nicely
    
    float size = 16.0f / fz; // Basic perspective divide shortcut
    uint32_t color = kColorLUT[type & 0x07];

    // Direct Header Submission
    pvr_poly_hdr_t* target_hdr = s_in_translucent ? &s_translucent_hdr : &s_opaque_hdr;
    pvr_prim(target_hdr, sizeof(pvr_poly_hdr_t));

    pvr_vertex_t vert;
    vert.flags = PVR_VERTEX_NORMAL;
    vert.argb = color;
    vert.oargb = 0;

    // Draw Front Facing Quad Strip
    vert.x = fx - size; vert.y = fy + size; vert.z = fz;
    pvr_prim(&vert, sizeof(vert));
    vert.x = fx - size; vert.y = fy - size; vert.z = fz;
    pvr_prim(&vert, sizeof(vert));
    vert.x = fx + size; vert.y = fy + size; vert.z = fz;
    pvr_prim(&vert, sizeof(vert));
    
    vert.flags = PVR_VERTEX_EOL;
    vert.x = fx + size; vert.y = fy - size; vert.z = fz;
    pvr_prim(&vert, sizeof(vert));
}

// ── DRAW VOXEL HIGHWAY CORRIDOR STRIP ────────────────────────────────────────
void Graphics::drawVoxelColumn(SFP16 x1, SFP16 x2, SFP16 y1, SFP16 y2, SFP16 z, uint8_t songId) {
    float fx1 = sfp16_to_float(x1);
    float fx2 = sfp16_to_float(x2);
    float fy1 = sfp16_to_float(y1);
    float fy2 = sfp16_to_float(y2);
    float fz  = sfp16_to_float(z);

    uint32_t color = kColorLUT[songId & 0x07];

    pvr_prim(&s_opaque_hdr, sizeof(pvr_poly_hdr_t));

    pvr_vertex_t vert;
    vert.flags = PVR_VERTEX_NORMAL;
    vert.argb = color;
    vert.oargb = 0;

    // Map long flat 3D horizon highway floor boards
    vert.x = fx1; vert.y = 480.0f - fy1; vert.z = fz;
    pvr_prim(&vert, sizeof(vert));
    vert.x = fx1; vert.y = 240.0f - fy2; vert.z = fz + 50.0f;
    pvr_prim(&vert, sizeof(vert));
    vert.x = fx2; vert.y = 480.0f - fy1; vert.z = fz;
    pvr_prim(&vert, sizeof(vert));

    vert.flags = PVR_VERTEX_EOL;
    vert.x = fx2; vert.y = 240.0f - fy2; vert.z = fz + 50.0f;
    pvr_prim(&vert, sizeof(vert));
}

void Graphics::drawVoxelColumnGlow(SFP16 x1, SFP16 x2, SFP16 y1, SFP16 y2, SFP16 z, uint8_t songId, uint8_t alpha) {
    // Intercept redirects code to translucent rendering properties automatically
    bool elementsState = s_in_translucent;
    if (!elementsState) pvr_list_begin(PVR_LIST_TR_POLY);

    float fx1 = sfp16_to_float(x1);
    float fx2 = sfp16_to_float(x2);
    float fy1 = sfp16_to_float(y1);
    float fy2 = sfp16_to_float(y2);
    float fz  = sfp16_to_float(z);

    uint32_t argb = (kColorLUT[songId & 0x07] & 0x00FFFFFF) | (static_cast<uint32_t>(alpha) << 24);

    pvr_prim(&s_translucent_hdr, sizeof(pvr_poly_hdr_t));

    pvr_vertex_t vert;
    vert.flags = PVR_VERTEX_NORMAL;
    vert.argb = argb;
    vert.oargb = 0;

    vert.x = fx1; vert.y = fy2; vert.z = fz;
    pvr_prim(&vert, sizeof(vert));
    vert.x = fx1; vert.y = fy1; vert.z = fz;
    pvr_prim(&vert, sizeof(vert));
    vert.x = fx2; vert.y = fy2; vert.z = fz;
    pvr_prim(&vert, sizeof(vert));

    vert.flags = PVR_VERTEX_EOL;
    vert.x = fx2; vert.y = fy1; vert.z = fz;
    pvr_prim(&vert, sizeof(vert));

    if (!elementsState) pvr_list_begin(PVR_LIST_OP_POLY);
}

// Helper to unpack 3x5 font arrays onto screen using direct primitive drawing
static void draw_glyph(float x, float y, char c, uint32_t color) {
    uint8_t idx = 0;
    if (c >= '0' && c <= '9') idx = c - '0';
    else if (c == 'C') idx = 10;
    else if (c == 'M') idx = 11;
    else if (c == 'P') idx = 12;
    else if (c == 'S') idx = 13;
    else return;

    uint16_t glyph = kFontLUT[idx];
    float pixel_size = 2.0f;

    for (int row = 0; row < 5; ++row) {
        for (int col = 0; col < 3; ++col) {
            // Check bit representation inside the uint16 structure matrix
            if (glyph & (1 << (14 - (row * 3 + col)))) {
                float px = x + (col * pixel_size);
                float py = y + (row * pixel_size);

                pvr_vertex_t vert;
                vert.flags = PVR_VERTEX_NORMAL;
                vert.argb = color;
                vert.oargb = 0;

                vert.x = px; vert.y = py + pixel_size; vert.z = 1.0f;
                pvr_prim(&vert, sizeof(vert));
                vert.x = px; vert.y = py; vert.z = 1.0f;
                pvr_prim(&vert, sizeof(vert));
                vert.x = px + pixel_size; vert.y = py + pixel_size; vert.z = 1.0f;
                pvr_prim(&vert, sizeof(vert));

                vert.flags = PVR_VERTEX_EOL;
                vert.x = px + pixel_size; vert.y = py; vert.z = 1.0f;
                pvr_prim(&vert, sizeof(vert));
            }
        }
    }
}

void Graphics::drawHUD(uint16_t score, uint16_t combo, uint16_t miss, uint8_t flags) {
    // Compile basic transparent header placeholder mapping structure
    pvr_poly_hdr_t hud_hdr;
    pvr_poly_cxt_t hud_cxt;
    pvr_poly_cxt_col(&hud_cxt, PVR_LIST_PT_POLY);
    pvr_poly_compile(&hud_hdr, &hud_cxt);
    pvr_prim(&hud_hdr, sizeof(hud_hdr));

    // Simple buffer extraction string routine bypass logic
    char buf[16];
    
    // Draw Score: "S 00000"
    draw_glyph(20.0f, 20.0f, 'S', 0xFFFFFFFF);
    int len = snprintf(buf, sizeof(buf), "%05d", score);
    for (int i = 0; i < len; ++i) {
        draw_glyph(32.0f + (i * 8.0f), 20.0f, buf[i], 0xFFFFFFFF);
    }

    // Draw Combo: "C 00"
    draw_glyph(20.0f, 36.0f, 'C', 0xFF00FF00);
    len = snprintf(buf, sizeof(buf), "%02d", combo);
    for (int i = 0; i < len; ++i) {
        draw_glyph(32.0f + (i * 8.0f), 36.0f, buf[i], 0xFF00FF00);
    }

    // Draw Misses: "M 00"
    draw_glyph(20.0f, 52.0f, 'M', 0xFFFF0000);
    len = snprintf(buf, sizeof(buf), "%02d", miss);
    for (int i = 0; i < len; ++i) {
        draw_glyph(32.0f + (i * 8.0f), 52.0f, buf[i], 0xFFFF0000);
    }
}

// ---------------------------------------------------------------------------
// Audio Implementation (AICA CDDA streaming layer bypass)
// ---------------------------------------------------------------------------

bool Audio::init() {
    snd_init();
    return true;
}

void Audio::play(uint8_t songId) {
    // Track ID 7 maps natively to live analog fallback streaming track loops
    if (songId == 7) {
        cdrom_cdda_play(7, 7, 0, CDDA_TRACKS);
    }
}

void Audio::stop() {
    cdrom_cdda_pause();
}

uint16_t Audio::getTrackProgress() {
    // Read play position tracking values from CDDA sectors
    int sector = cdrom_cdda_get_each_frame();
    return static_cast<uint16_t>(sector & 0xFFFF);
}

uint8_t Audio::getEnergyLevel() {
    // Hardware monitoring abstraction layer placeholder stub values
    return 32;
}

// ---------------------------------------------------------------------------
// Input Implementation (Maple Controller Architecture Bus Interface)
// ---------------------------------------------------------------------------

bool Input::init() {
    return true;
}

void Input::poll() {
    // KallistiOS handles driver updates automatically via system loop triggers
}

InputState Input::readPressedActions() {
    maple_device_t* cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    if (!cont) return 0;

    cont_state_t* state = (cont_state_t*)maple_dev_status(cont);
    if (!state) return 0;

    InputState pressed = 0;

    // Map D-Pad buttons onto Lane Trigger masks
    if (state->buttons & CONT_DPAD_LEFT)  pressed |= (1 << 0);
    if (state->buttons & CONT_DPAD_DOWN)  pressed |= (1 << 1);
    if (state->buttons & CONT_DPAD_RIGHT) pressed |= (1 << 2);
    
    // Map A Button onto Confirm Actions
    if (state->buttons & CONT_A) {
        pressed |= static_cast<InputState>(InputAction::Confirm);
    }

    return pressed;
}

} // namespace PAL
