// =============================================================================
// PAL_Dreamcast.cpp
// Platform Abstraction Layer Implementation for Sega Dreamcast / KallistiOS
// =============================================================================

#include "PAL.h"
#include <kos.h>
#include <cstdio>

namespace Engine {
namespace PAL {

// ---------------------------------------------------------------------------
// Hardware Cores Lookup Tables (VRAM Native)
// ---------------------------------------------------------------------------
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
    0x7127, // C
    0x5D6D, // M
    0x7B24, // P
    0x794F  // S
};

// Inline Q8.8 Fixed-Point to Single-Precision Floating Point conversion
static inline float sfp16_to_float(SFP16 val) {
    return static_cast<float>(val) / 256.0f;
}

static inline float fp16_to_float(FP16 val) {
    return static_cast<float>(val) / 256.0f;
}

// ---------------------------------------------------------------------------
// Concrete Dreamcast Hardware Implementations
// ---------------------------------------------------------------------------

class DreamcastGraphics : public GraphicsInterface
{
public:
    DreamcastGraphics() : m_currentList(PVR_LIST_OP_POLY) {}
    virtual ~DreamcastGraphics() override = default;

    virtual bool init(uint16_t screenW, uint16_t screenH) override {
        pvr_init_params_t pvr_params = {
            { PVR_BIN_CXT_OP, PVR_BIN_CXT_NONE, PVR_BIN_CXT_TR, PVR_BIN_CXT_NONE, PVR_BIN_CXT_PT },
            16 * 1024 * 1024
        };
        
        if (pvr_init(&pvr_params) != 0) return false;

        // Pre-compile the global Opaque Context Header
        pvr_poly_cxt_t opaque_cxt;
        pvr_poly_cxt_col(&opaque_cxt, PVR_LIST_OP_POLY);
        opaque_cxt.gen.shading = PVR_SHADE_FLAT;
        opaque_cxt.gen.culling = PVR_CULL_CCW;
        pvr_poly_compile(&m_opaqueHdr, &opaque_cxt);

        // Pre-compile the global Translucent Context Header
        pvr_poly_cxt_t trans_cxt;
        pvr_poly_cxt_col(&trans_cxt, PVR_LIST_TR_POLY);
        trans_cxt.gen.shading = PVR_SHADE_FLAT;
        trans_cxt.gen.culling = PVR_CULL_CCW;
        trans_cxt.blend.src = PVR_BLEND_SRCALPHA;
        trans_cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
        pvr_poly_compile(&m_translucentHdr, &trans_cxt);

        // Pre-compile the global Punch-Through Context Header (Optimized HUD overlay)
        pvr_poly_cxt_t pt_cxt;
        pvr_poly_cxt_col(&pt_cxt, PVR_LIST_PT_POLY);
        pt_cxt.gen.shading = PVR_SHADE_FLAT;
        pt_cxt.gen.culling = PVR_CULL_NONE;
        pvr_poly_compile(&m_ptHdr, &pt_cxt);

        return true;
    }

    virtual void beginFrame() override {
        pvr_scene_begin();
        pvr_list_begin(PVR_LIST_OP_POLY);
        m_currentList = PVR_LIST_OP_POLY;
    }

    virtual void endFrame() override {
        pvr_list_end();
        pvr_scene_end();
    }

    virtual void shutdown() override {
        pvr_shutdown();
    }

    virtual void updateCamera(FP16 trackPos, FP16 laneX) override {
        // Optional camera transformation hook for raw hardware matrices
    }

    // ── NATIVE DETECT & SWITCH PIPELINES ──
    inline void ensureList(pvr_list_t targetList) {
        if (m_currentList != targetList) {
            pvr_list_end();
            pvr_list_begin(targetList);
            m_currentList = targetList;
        }
    }

    virtual void drawVoxelColumn(SFP16 xLeft, SFP16 xRight, SFP16 yBottom, SFP16 yTop, SFP16 z, uint8_t colorIndex) override {
        ensureList(PVR_LIST_OP_POLY);

        float xl = sfp16_to_float(xLeft);
        float xr = sfp16_to_float(xRight);
        float yb = sfp16_to_float(yBottom);
        float yt = sfp16_to_float(yTop);
        float fz = sfp16_to_float(z);

        pvr_prim(&m_opaqueHdr, sizeof(pvr_poly_hdr_t));

        pvr_vertex_t vert;
        vert.flags = PVR_VERTEX_NORMAL;
        vert.argb = kColorLUT[colorIndex & 0x07];
        vert.oargb = 0;

        vert.x = xl; vert.y = 480.0f - yb; vert.z = fz;
        pvr_prim(&vert, sizeof(vert));
        vert.x = xl; vert.y = 240.0f - yt; vert.z = fz + 50.0f;
        pvr_prim(&vert, sizeof(vert));
        vert.x = xr; vert.y = 480.0f - yb; vert.z = fz;
        pvr_prim(&vert, sizeof(vert));

        vert.flags = PVR_VERTEX_EOL;
        vert.x = xr; vert.y = 240.0f - yt; vert.z = fz + 50.0f;
        pvr_prim(&vert, sizeof(vert));
    }

    virtual void drawVoxelColumnGlow(SFP16 xLeft, SFP16 xRight, SFP16 yBottom, SFP16 yTop, SFP16 z, uint8_t colorIndex, uint8_t glowAlpha) override {
        ensureList(PVR_LIST_TR_POLY);

        float xl = sfp16_to_float(xLeft);
        float xr = sfp16_to_float(xRight);
        float yb = sfp16_to_float(yBottom);
        float yt = sfp16_to_float(yTop);
        float fz = sfp16_to_float(z);

        uint32_t argb = (kColorLUT[colorIndex & 0x07] & 0x00FFFFFF) | (static_cast<uint32_t>(glowAlpha) << 24);

        pvr_prim(&m_translucentHdr, sizeof(pvr_poly_hdr_t));

        pvr_vertex_t vert;
        vert.flags = PVR_VERTEX_NORMAL;
        vert.argb = argb;
        vert.oargb = 0;

        vert.x = xl; vert.y = yt; vert.z = fz;
        pvr_prim(&vert, sizeof(vert));
        vert.x = xl; vert.y = yb; vert.z = fz;
        pvr_prim(&vert, sizeof(vert));
        vert.x = xr; vert.y = yt; vert.z = fz;
        pvr_prim(&vert, sizeof(vert));

        vert.flags = PVR_VERTEX_EOL;
        vert.x = xr; vert.y = yb; vert.z = fz;
        pvr_prim(&vert, sizeof(vert));
    }

    virtual void drawBlock(SFP16 x, SFP16 y, SFP16 z, uint8_t colorIndex) override {
        // Blocks render transparently in the current state context block sequence
        pvr_poly_hdr_t* currentHdr = (m_currentList == PVR_LIST_TR_POLY) ? &m_translucentHdr : &m_opaqueHdr;
        
        float fx = sfp16_to_float(x);
        float fy = sfp16_to_float(y);
        float fz = sfp16_to_float(z) + 1.0f;
        float size = 16.0f / fz;

        pvr_prim(currentHdr, sizeof(pvr_poly_hdr_t));

        pvr_vertex_t vert;
        vert.flags = PVR_VERTEX_NORMAL;
        vert.argb = kColorLUT[colorIndex & 0x07];
        vert.oargb = 0;

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

    virtual void drawParticle(SFP16 x, SFP16 y, SFP16 z, uint8_t colorIndex, uint8_t alpha) override {
        ensureList(PVR_LIST_TR_POLY);

        float fx = sfp16_to_float(x);
        float fy = sfp16_to_float(y);
        float h_size = sfp16_to_float(z) * 0.5f; 
        uint32_t argb = (kColorLUT[colorIndex & 0x07] & 0x00FFFFFF) | (static_cast<uint32_t>(alpha) << 24);

        pvr_prim(&m_translucentHdr, sizeof(pvr_poly_hdr_t));

        pvr_vertex_t vert;
        vert.flags = PVR_VERTEX_NORMAL;
        vert.argb = argb;
        vert.oargb = 0;

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

    virtual void drawHUD(uint32_t score, uint8_t combo, uint8_t nx, uint8_t ny) override {
        ensureList(PVR_LIST_PT_POLY);
        pvr_prim(&m_ptHdr, sizeof(pvr_poly_hdr_t));

        char buf[16];
        
        // Render Score Header
        drawGlyph(20.0f, 20.0f, 'S', 0xFFFFFFFF);
        int len = std::snprintf(buf, sizeof(buf), "%05lu", score);
        for (int i = 0; i < len; ++i) {
            drawGlyph(32.0f + (i * 8.0f), 20.0f, buf[i], 0xFFFFFFFF);
        }

        // Render Combo Counter
        drawGlyph(20.0f, 36.0f, 'C', 0xFF00FF00);
        len = std::snprintf(buf, sizeof(buf), "%02u", combo);
        for (int i = 0; i < len; ++i) {
            drawGlyph(32.0f + (i * 8.0f), 36.0f, buf[i], 0xFF00FF00);
        }

        // Render Miss Trackers (Using custom passed parameters)
        drawGlyph(20.0f, 52.0f, 'M', 0xFFFF0000);
        len = std::snprintf(buf, sizeof(buf), "%02u", nx); 
        for (int i = 0; i < len; ++i) {
            drawGlyph(32.0f + (i * 8.0f), 52.0f, buf[i], 0xFFFF0000);
        }
    }

private:
    void drawGlyph(float x, float y, char c, uint32_t color) {
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

    pvr_poly_hdr_t m_opaqueHdr;
    pvr_poly_hdr_t m_translucentHdr;
    pvr_poly_hdr_t m_ptHdr;
    pvr_list_t     m_currentList;
};

class DreamcastAudio : public AudioInterface
{
public:
    virtual ~DreamcastAudio() override = default;
    virtual bool init() override { snd_init(); return true; }
    virtual void shutdown() override { snd_shutdown(); }
    virtual void setPaused(bool paused) override { if(paused) cdrom_cdda_pause(); }
    virtual void stop() override { cdrom_cdda_pause(); }

    virtual bool play(uint8_t songId) override {
        if (songId == 7) {
            cdrom_cdda_play(7, 7, 0, CDDA_TRACKS);
            return true;
        }
        return false;
    }

    virtual FP16 getTrackProgress() override {
        int sector = cdrom_cdda_get_each_frame();
        return static_cast<FP16>(sector & 0xFFFF);
    }

    virtual uint8_t getEnergyLevel() override { return 32; }
};

class DreamcastInput : public InputInterface
{
public:
    virtual ~DreamcastInput() override = default;
    virtual bool init() override { return true; }
    virtual void shutdown() override {}
    virtual void poll() override {}

    virtual InputState readPressedActions() override {
        maple_device_t* cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
        if (!cont) return 0;

        cont_state_t* state = (cont_state_t*)maple_dev_status(cont);
        if (!state) return 0;

        InputState pressed = 0;
        if (state->buttons & CONT_DPAD_LEFT)  pressed |= static_cast<InputState>(InputAction::LaneLeft);
        if (state->buttons & CONT_DPAD_DOWN)  pressed |= static_cast<InputState>(InputAction::LaneRight); // Map onto engine expectations
        if (state->buttons & CONT_DPAD_RIGHT) pressed |= static_cast<InputState>(InputAction::LaneUp);
        if (state->buttons & CONT_A)          pressed |= static_cast<InputState>(InputAction::Confirm);

        return pressed;
    }

    virtual InputState readHeldActions() override { return 0; }
    virtual bool isHeld(InputAction action) override { return false; }
};

// ---------------------------------------------------------------------------
// Static Allocation Factory Implementations
// ---------------------------------------------------------------------------

static DreamcastGraphics g_hardwareGraphics;
static DreamcastAudio    g_hardwareAudio;
static DreamcastInput    g_hardwareInput;

PlatformBundle createPlatform() {
    PlatformBundle bundle;
    bundle.graphics = &g_hardwareGraphics;
    bundle.audio    = &g_hardwareAudio;
    bundle.input    = &g_hardwareInput;
    return bundle;
}

void destroyPlatform(PlatformBundle& bundle) {
    if (bundle.graphics) bundle.graphics->shutdown();
    if (bundle.audio)    bundle.audio->shutdown();
    if (bundle.input)    bundle.input->shutdown();
    bundle.graphics = nullptr;
    bundle.audio    = nullptr;
    bundle.input    = nullptr;
}

} // namespace PAL
} // namespace Engine
