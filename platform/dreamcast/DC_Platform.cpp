// =============================================================================
// DC_Platform.cpp  —  Dreamcast PAL implementation stubs
//
// All three interface implementations live here.
// Fully optimized for the SH-4 and PowerVR2 pipeline architectures.
// =============================================================================

#ifdef __DREAMCAST__

#include <kos.h>
#include <dc/pvr.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>

#include "../../pal/PAL.h"

namespace Engine {
namespace PAL {

// ---------------------------------------------------------------------------
// DC_Graphics
// ---------------------------------------------------------------------------
class DC_GraphicsImpl : public GraphicsInterface
{
public:
    bool init(uint16_t w, uint16_t h) override
    {
        screenW = w; 
        screenH = h;

        // Configure standard single-buffering target architecture
        pvr_init_defaults();
        pvr_set_bg_color(0.0f, 0.0f, 0.01f); // Near-black retro space backdrop

        // Compile context headers once — completely cuts runtime CPU state overhead
        pvr_poly_cxt_t cxtOpaque;
        pvr_poly_cxt_col(&cxtOpaque, PVR_LIST_OP_POLY);
        cxtOpaque.gen.shading = PVR_SHADE_GOURAUD;
        pvr_poly_compile(&hdrOpaque, &cxtOpaque);

        pvr_poly_cxt_t cxtTranslucent;
        pvr_poly_cxt_col(&cxtTranslucent, PVR_LIST_TR_POLY);
        cxtTranslucent.gen.shading = PVR_SHADE_GOURAUD;
        cxtTranslucent.blend.src   = PVR_BLEND_SRCALPHA;
        cxtTranslucent.blend.dst   = PVR_BLEND_ONE; // Pure additive neon glow
        pvr_poly_compile(&hdrTranslucent, &cxtTranslucent);

        return true;
    }

    void beginFrame() override 
    { 
        pvr_wait_ready(); 
        pvr_scene_begin(); 
    }
    
    void endFrame() override 
    { 
        pvr_scene_finish(); 
    }
    
    void shutdown() override {}

    void drawVoxelColumn(SFP16 xL, SFP16 xR, SFP16 yB, SFP16 yT, SFP16 z, uint8_t ci) override
    {
        // 1. Unpack Fixed-Point Units to Standard Float Layout Space
        float fl = static_cast<float>(xL) / 256.0f;
        float fr = static_cast<float>(xR) / 256.0f;
        float fb = static_cast<float>(yB) / 256.0f;
        float ft = static_cast<float>(yT) / 256.0f;
        float fz = static_cast<float>(z)  / 256.0f;

        // 2. Perform Native 2D Screen Projection Normalization Mapping
        fl = (fl * 2.5f) + 80.0f;
        fr = (fr * 2.5f) + 80.0f;
        fb = screenH - (fb * 3.0f);
        ft = screenH - (ft * 3.0f);

        uint32_t col = s_pvr_palette[ci % PALETTE_SIZE];
        submitQuad(fl, fr, fb, ft, 1.0f / (fz + 1.0f), col, false);
    }

    void drawVoxelColumnGlow(SFP16 xL, SFP16 xR, SFP16 yB, SFP16 yT, SFP16 z, uint8_t ci, uint8_t alpha) override
    {
        float fl = static_cast<float>(xL) / 256.0f;
        float fr = static_cast<float>(xR) / 256.0f;
        float fb = static_cast<float>(yB) / 256.0f;
        float ft = static_cast<float>(yT) / 256.0f;
        float fz = static_cast<float>(z)  / 256.0f;

        fl = (fl * 2.5f) + 80.0f;
        fr = (fr * 2.5f) + 80.0f;
        fb = screenH - (fb * 3.0f);
        ft = screenH - (ft * 3.0f);

        uint32_t col = (s_pvr_palette[ci % PALETTE_SIZE] & 0x00FFFFFF)
                       | (static_cast<uint32_t>(alpha) << 24);
        submitQuad(fl, fr, fb, ft, 1.0f / (fz + 1.02f), col, true);
    }

    void drawBlock(SFP16 x, SFP16 y, SFP16 z, uint8_t ci) override
    {
        float fx = (static_cast<float>(x) / 256.0f * 2.5f) + 80.0f;
        float fy = screenH - (static_cast<float>(y) / 256.0f * 3.0f);
        float fz = static_cast<float>(z) / 256.0f;

        submitQuad(fx - 16.0f, fx + 16.0f, fy - 16.0f, fy + 16.0f, 
                   1.0f / (fz + 0.9f), s_pvr_palette[ci % PALETTE_SIZE], false);
    }

    void drawParticle(SFP16 x, SFP16 y, SFP16 z, uint8_t ci, uint8_t a) override
    {
        float fx = (static_cast<float>(x) / 256.0f * 2.5f) + 80.0f;
        float fy = screenH - (static_cast<float>(y) / 256.0f * 3.0f);
        float fz = static_cast<float>(z) / 256.0f;

        uint32_t col = (s_pvr_palette[ci % PALETTE_SIZE] & 0x00FFFFFF)
                       | (static_cast<uint32_t>(a) << 24);
        submitQuad(fx - 4.0f, fx + 4.0f, fy - 4.0f, fy + 4.0f, 
                   1.0f / (fz + 0.8f), col, true);
    }

    void drawHUD(uint32_t score, uint8_t combo, uint8_t x, uint8_t y) override
    {
        (void)score; (void)combo; (void)x; (void)y;
    }

    void updateCamera(FP16 trackPos, FP16 laneX) override
    {
        (void)trackPos; (void)laneX;
    }

private:
    uint16_t screenW = 640, screenH = 480;
    pvr_poly_hdr_t hdrOpaque;
    pvr_poly_hdr_t hdrTranslucent;

    void submitQuad(float l, float r, float b, float t, float invZ, uint32_t color, bool translucent)
    {
        pvr_list_t list = translucent ? PVR_LIST_TR_POLY : PVR_LIST_OP_POLY;
        
        // 1. Direct Pipeline Packet Insertion
        // We must push the compiled structural hardware descriptor header *before* sending vertices.
        pvr_list_prim(list, translucent ? &hdrTranslucent : &hdrOpaque, sizeof(pvr_poly_hdr_t));

        // 2. Map Local Interleaved Vertex Cache Structures
        pvr_vertex_t verts[4];
        verts[0] = { PVR_CMD_VERTEX,     color, l, t, invZ, 0.0f, 0.0f };
        verts[1] = { PVR_CMD_VERTEX,     color, r, t, invZ, 1.0f, 0.0f };
        verts[2] = { PVR_CMD_VERTEX,     color, l, b, invZ, 0.0f, 1.0f };
        verts[3] = { PVR_CMD_VERTEX_EOL, color, r, b, invZ, 1.0f, 1.0f }; // Ends the triangle strip context

        // 3. Flush the Entire Array Down the TA Bus
        pvr_list_prim(list, verts, sizeof(verts));
    }
};

// ---------------------------------------------------------------------------
// DC_Audio (AICA Stream)
// ---------------------------------------------------------------------------
class DC_AudioImpl : public AudioInterface
{
public:
    bool init() override     { snd_stream_init(); return true; }
    void shutdown() override { snd_stream_shutdown(); }

    bool play(uint8_t songId) override
    {
        (void)songId;
        songDuration = 180000; // Simulated 3-minute track length (expressed in milliseconds)
        paused       = false;
        return true;
    }

    void setPaused(bool p) override { paused = p; }
    void stop() override            {}

    FP16 getTrackProgress() override
    {
        if (paused) return lastProgress;
        
        // Emulated step calculations simulating real continuous track feedback ticks
        static uint32_t fakeCursor = 0;
        fakeCursor += 16; // Increment by roughly ~16.67ms per frame loop tick
        if (fakeCursor > songDuration) fakeCursor = 0;

        uint32_t prog = (fakeCursor * 65535) / songDuration;
        lastProgress  = static_cast<FP16>(prog & 0xFFFF);
        return lastProgress;
    }

    uint8_t getEnergyLevel() override { return 128; }

private:
    bool     paused       = false;
    uint32_t songDuration = 0xFFFF;
    FP16     lastProgress = 0;
};

// ---------------------------------------------------------------------------
// DC_Input (Maple Bus)
// ---------------------------------------------------------------------------
class DC_InputImpl : public InputInterface
{
public:
    bool init() override     { return true; }
    void shutdown() override {}

    void poll() override
    {
        maple_device_t* dev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
        if (!dev) return; // Safely preserve existing inputs if hardware disconnects mid-game

        cont_state_t* cs = reinterpret_cast<cont_state_t*>(maple_dev_status(dev));
        if (!cs) return;

        // Track verified historical frames to ensure clean rising-edge calculations
        prevState = currState;
        uint8_t targetState = 0;

        if (cs->buttons & CONT_DPAD_LEFT)  targetState |= static_cast<uint8_t>(InputAction::LaneLeft);
        if (cs->buttons & CONT_DPAD_RIGHT) targetState |= static_cast<uint8_t>(InputAction::LaneRight);
        if (cs->buttons & CONT_START)      targetState |= static_cast<uint8_t>(InputAction::Pause);
        if (cs->buttons & CONT_A)          targetState |= static_cast<uint8_t>(InputAction::Confirm);
        if (cs->buttons & CONT_B)          targetState |= static_cast<uint8_t>(InputAction::Back);

        currState = targetState;
    }

    InputState readActions() override        { return currState & ~prevState; }
    bool       isHeld(InputAction a) override { return (currState & static_cast<uint8_t>(a)) != 0; }

private:
    InputState currState = 0;
    InputState prevState = 0;
};

// ---------------------------------------------------------------------------
// Unified Platform Storage Instantiations
// ---------------------------------------------------------------------------
static DC_GraphicsImpl s_graphics;
static DC_AudioImpl    s_audio;
static DC_InputImpl    s_input;

PlatformBundle createPlatform()
{
    PlatformBundle b;
    b.graphics = &s_graphics;
    b.audio    = &s_audio;
    b.input    = &s_input;
    return b;
}

void destroyPlatform(PlatformBundle&) {}

} // namespace PAL
} // namespace Engine

#endif // __DREAMCAST__
