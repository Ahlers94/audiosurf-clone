// =============================================================================
// DC_Platform.cpp  —  Dreamcast PAL implementation stubs
//
// This is the ONLY file in the codebase that may contain:
//   #ifdef __DREAMCAST__
//   #include <kos.h>
//   #include <dc/pvr.h>
//   #include <dc/sound/stream.h>
//   etc.
//
// All three interface implementations live here.
// The factory function createPlatform() returns instances of these classes
// to the engine — which never knows what hardware it's running on.
//
// BUILD: Compiled only when the Dreamcast toolchain is active.
//        The Makefile/SH4 target selects this file; Web_Platform.cpp
//        is excluded from that build and vice-versa.
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
// Uses the PowerVR2 (PVR) hardware via KallistiOS pvr_* API.
// Triangle strips map directly to PVR polygon lists — ideal throughput.
//
// Neon glow: two PVR polygon lists per frame:
//   PT_NO_MODIFIER (opaque)  → solid inner geometry   (Pass 2)
//   PT_TR_MODIFIER (translucent, additive) → glow halo (Pass 1)
// Hardware blending handles alpha at zero CPU cost on the PVR tile engine.
// ---------------------------------------------------------------------------

/// Neon palette in PVR ARGB format — 8 entries matching PALETTE_SIZE.
static const uint32_t s_pvr_palette[PALETTE_SIZE] = {
    0xFF00FFFF,  // 0: cyan
    0xFF00FF80,  // 1: spring green
    0xFFFF0080,  // 2: hot pink
    0xFFFF8000,  // 3: amber
    0xFF8000FF,  // 4: violet
    0xFF0080FF,  // 5: azure
    0xFFFFFF00,  // 6: yellow
    0xFFFFFFFF,  // 7: white
};

class DC_GraphicsImpl : public GraphicsInterface
{
public:
    bool init(uint16_t w, uint16_t h) override
    {
        pvr_init_defaults();
        // Configure PVR for two polygon lists: opaque + translucent.
        pvr_set_bg_color(0.0f, 0.0f, 0.02f); // near-black background
        screenW = w; screenH = h;
        return true;
    }

    void beginFrame() override { pvr_wait_ready(); pvr_scene_begin(); }
    void endFrame()   override { pvr_scene_finish(); }
    void shutdown()   override { /* PVR teardown via KOS */ }

    void drawVoxelColumn(SFP16 xL, SFP16 xR, SFP16 yB, SFP16 yT,
                         uint8_t ci) override
    {
        // Convert Q8.8 → float for PVR vertices.
        // This is the ONLY place fixed→float conversion occurs.
        float fl = static_cast<float>(xL) / 256.0f;
        float fr = static_cast<float>(xR) / 256.0f;
        float fb = static_cast<float>(yB) / 256.0f;
        float ft = static_cast<float>(yT) / 256.0f;
        uint32_t col = s_pvr_palette[ci % PALETTE_SIZE];
        submitQuad(fl, fr, fb, ft, 0.5f, col, /*translucent=*/false);
    }

    void drawVoxelColumnGlow(SFP16 xL, SFP16 xR, SFP16 yB, SFP16 yT,
                              uint8_t ci, uint8_t alpha) override
    {
        float fl = static_cast<float>(xL) / 256.0f;
        float fr = static_cast<float>(xR) / 256.0f;
        float fb = static_cast<float>(yB) / 256.0f;
        float ft = static_cast<float>(yT) / 256.0f;
        uint32_t col = (s_pvr_palette[ci % PALETTE_SIZE] & 0x00FFFFFF)
                       | (static_cast<uint32_t>(alpha) << 24);
        submitQuad(fl, fr, fb, ft, 0.48f, col, /*translucent=*/true);
    }

    void drawBlock(SFP16 x, SFP16 y, SFP16, uint8_t ci) override
    {
        float fx = static_cast<float>(x) / 256.0f;
        float fy = static_cast<float>(y) / 256.0f;
        uint32_t col = s_pvr_palette[ci % PALETTE_SIZE];
        submitQuad(fx - 0.08f, fx + 0.08f, fy - 0.08f, fy + 0.08f,
                   0.45f, col, false);
    }

    void drawParticle(SFP16 x, SFP16 y, SFP16, uint8_t ci, uint8_t a) override
    {
        float fx = static_cast<float>(x) / 256.0f;
        float fy = static_cast<float>(y) / 256.0f;
        uint32_t col = (s_pvr_palette[ci % PALETTE_SIZE] & 0x00FFFFFF)
                       | (static_cast<uint32_t>(a) << 24);
        submitQuad(fx - 0.02f, fx + 0.02f, fy - 0.02f, fy + 0.02f,
                   0.44f, col, true);
    }

    void drawHUD(uint32_t score, uint8_t combo, uint8_t, uint8_t) override
    {
        // KOS bfont_draw_str() for score — omitted for brevity.
        (void)score; (void)combo;
    }

    void updateCamera(FP16 trackPos, FP16 laneX) override
    {
        // Build a simple lookat matrix in float and push to PVR matrix stack.
        // Float math ONLY inside the PAL — game logic never reaches here.
        (void)trackPos; (void)laneX; // matrix calculation omitted for brevity
    }

private:
    uint16_t screenW = 640, screenH = 480;

    void submitQuad(float l, float r, float b, float t, float z,
                    uint32_t color, bool translucent)
    {
        pvr_list_t list = translucent ? PVR_LIST_TR_POLY : PVR_LIST_OP_POLY;
        pvr_vertex_t verts[4];
        // Two-triangle quad via a 4-vertex strip (PVR_CMD_VERTEX_EOL on last).
        verts[0] = { PVR_CMD_VERTEX,     color, l, t, z, 0,0 };
        verts[1] = { PVR_CMD_VERTEX,     color, r, t, z, 1,0 };
        verts[2] = { PVR_CMD_VERTEX,     color, l, b, z, 0,1 };
        verts[3] = { PVR_CMD_VERTEX_EOL, color, r, b, z, 1,1 };
        pvr_list_prim(list, verts, sizeof(verts));
    }
};

// ---------------------------------------------------------------------------
// DC_Audio  (KallistiOS AICA / snd_stream)
// ---------------------------------------------------------------------------

class DC_AudioImpl : public AudioInterface
{
public:
    bool init()  override { snd_stream_init(); return true; }
    void shutdown() override { snd_stream_shutdown(); }

    bool play(uint8_t songId) override
    {
        (void)songId; // load from ROM FS in real implementation
        // snd_stream_start(stream_hnd, ...);
        songDuration = 0xFFFF; // filled from actual MP3 header
        return true;
    }

    void setPaused(bool p) override
    {
        // snd_stream_pause / resume
        paused = p;
    }

    void stop() override { /* snd_stream_stop */ }

    FP16 getTrackProgress() override
    {
        if (paused) return lastProgress;
        // Map playback cursor in ms → [0, 65535].
        // FP shortcut: (cursor << 16) / duration → multiply then shift,
        //              but we use 32-bit intermediate to avoid overflow.
        uint32_t cursor = /* snd_stream_get_position() */ 0; // stub
        uint32_t prog   = (static_cast<uint32_t>(cursor) * 0xFFFF) / songDuration;
        lastProgress = static_cast<FP16>(prog & 0xFFFF);
        return lastProgress;
    }

    uint8_t getEnergyLevel() override
    {
        // AICA DSP output or pre-baked beat data lookup — stub returns 128.
        return 128;
    }

private:
    bool     paused       = false;
    uint32_t songDuration = 0xFFFF;
    FP16     lastProgress = 0;
};

// ---------------------------------------------------------------------------
// DC_Input  (Maple Bus controller)
// ---------------------------------------------------------------------------

class DC_InputImpl : public InputInterface
{
public:
    bool init() override { return true; }
    void shutdown() override {}

    void poll() override
    {
        maple_device_t* dev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
        prevState   = currState;
        currState   = 0;
        if (!dev) return;
        cont_state_t* cs = reinterpret_cast<cont_state_t*>(maple_dev_status(dev));
        if (!cs) return;
        if (cs->buttons & CONT_DPAD_LEFT)  currState |= static_cast<uint8_t>(InputAction::LaneLeft);
        if (cs->buttons & CONT_DPAD_RIGHT) currState |= static_cast<uint8_t>(InputAction::LaneRight);
        if (cs->buttons & CONT_START)      currState |= static_cast<uint8_t>(InputAction::Pause);
        if (cs->buttons & CONT_A)          currState |= static_cast<uint8_t>(InputAction::Confirm);
        if (cs->buttons & CONT_B)          currState |= static_cast<uint8_t>(InputAction::Back);
    }

    InputState readActions() override
    {
        // Rising-edge: bits set this tick but NOT last tick.
        return currState & ~prevState;
    }

    bool isHeld(InputAction a) override
    {
        return (currState & static_cast<uint8_t>(a)) != 0;
    }

private:
    InputState currState = 0;
    InputState prevState = 0;
};

// ---------------------------------------------------------------------------
// Static storage for the three singletons — no heap.
// ---------------------------------------------------------------------------

static DC_GraphicsImpl s_graphics;
static DC_AudioImpl    s_audio;
static DC_InputImpl    s_input;

// ---------------------------------------------------------------------------
// Factory — THE ONLY #ifdef site in the entire codebase.
// ---------------------------------------------------------------------------

PlatformBundle createPlatform()
{
    PlatformBundle b;
    b.graphics = &s_graphics;
    b.audio    = &s_audio;
    b.input    = &s_input;
    return b;
}

void destroyPlatform(PlatformBundle&) { /* static storage, nothing to free */ }

} // namespace PAL
} // namespace Engine

#endif // __DREAMCAST__
