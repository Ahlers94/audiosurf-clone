// =============================================================================
// Web_Platform.cpp  —  WebAssembly / WebGL / Web Audio PAL stubs
//
// The symmetric partner to DC_Platform.cpp.
// Compiled only when building with Emscripten (em++).
//
// WebGL is accessed via Emscripten's OpenGL ES 2.0 compatibility layer.
// Web Audio API is reached via EM_JS blocks that bridge to JavaScript.
// Keyboard input uses emscripten_set_keydown/keyup_callback.
//
// Same interface, same contract — GameEngine.h is unchanged.
// =============================================================================

#ifndef __DREAMCAST__

#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLES2/gl2.h>

#include "../../pal/PAL.h"

namespace Engine {
namespace PAL {

// ---------------------------------------------------------------------------
// Neon palette as WebGL-friendly RGBA bytes (8 × 4 bytes = 32 bytes total).
// ---------------------------------------------------------------------------

struct RGBA { uint8_t r, g, b, a; };
static const RGBA s_palette[PALETTE_SIZE] = {
    {0,   255, 255, 255},  // 0: cyan
    {0,   255, 128, 255},  // 1: spring green
    {255, 0,   128, 255},  // 2: hot pink
    {255, 128, 0,   255},  // 3: amber
    {128, 0,   255, 255},  // 4: violet
    {0,   128, 255, 255},  // 5: azure
    {255, 255, 0,   255},  // 6: yellow
    {255, 255, 255, 255},  // 7: white
};

// ---------------------------------------------------------------------------
// Web_Graphics  (WebGL ES2 via Emscripten OpenGL shim)
// ---------------------------------------------------------------------------
// Minimal unlit colour shader — two uniforms: uColor, uMVP (identity for now).
// Geometry is submitted as interleaved float VBOs each frame.
// Triangle strips reuse vertices exactly as designed in the PAL contract.
// ---------------------------------------------------------------------------

static const char* s_vert_src = R"GLSL(
    attribute vec3 aPos;
    uniform mat4 uMVP;
    void main() { gl_Position = uMVP * vec4(aPos, 1.0); }
)GLSL";

static const char* s_frag_src = R"GLSL(
    precision mediump float;
    uniform vec4 uColor;
    void main() { gl_FragColor = uColor; }
)GLSL";

class Web_GraphicsImpl : public GraphicsInterface
{
public:
    bool init(uint16_t w, uint16_t h) override
    {
        EmscriptenWebGLContextAttributes attr;
        emscripten_webgl_init_context_attributes(&attr);
        attr.majorVersion = 2;  // WebGL 2 / GLES3 for additive blending support.
        ctx = emscripten_webgl_create_context("#canvas", &attr);
        emscripten_webgl_make_context_current(ctx);

        glViewport(0, 0, w, h);
        glEnable(GL_BLEND);
        // Additive blending: src*1 + dst*1 — the neon glow blend mode.
        // Switched per-pass: see drawVoxelColumnGlow.
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);

        prog = buildShader(s_vert_src, s_frag_src);
        aPos   = glGetAttribLocation (prog, "aPos");
        uMVP   = glGetUniformLocation(prog, "uMVP");
        uColor = glGetUniformLocation(prog, "uColor");

        glGenBuffers(1, &vbo);
        return true;
    }

    void beginFrame() override
    {
        glClearColor(0.0f, 0.0f, 0.02f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(prog);
        // Identity MVP for now; updateCamera() writes a real matrix.
        static const float I[16] = {
            1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1
        };
        glUniformMatrix4fv(uMVP, 1, GL_FALSE, I);
    }

    void endFrame() override { /* WebGL auto-presents */ }
    void shutdown() override { emscripten_webgl_destroy_context(ctx); }

    void drawVoxelColumn(SFP16 xL, SFP16 xR, SFP16 yB, SFP16 yT,
                         uint8_t ci) override
    {
        // Normal alpha blending for the solid pass.
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        submitQuad(xL, xR, yB, yT, ci, 255);
    }

    void drawVoxelColumnGlow(SFP16 xL, SFP16 xR, SFP16 yB, SFP16 yT,
                              uint8_t ci, uint8_t alpha) override
    {
        // Additive blending for neon glow — zero GPU cost for the "bloom".
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        submitQuad(xL, xR, yB, yT, ci, alpha);
    }

    void drawBlock(SFP16 x, SFP16 y, SFP16, uint8_t ci) override
    {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        SFP16 pad = 10;
        submitQuad(static_cast<SFP16>(x-pad), static_cast<SFP16>(x+pad),
                   static_cast<SFP16>(y-pad), static_cast<SFP16>(y+pad),
                   ci, 220);
    }

    void drawParticle(SFP16 x, SFP16 y, SFP16, uint8_t ci, uint8_t a) override
    {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        SFP16 pad = 3;
        submitQuad(static_cast<SFP16>(x-pad), static_cast<SFP16>(x+pad),
                   static_cast<SFP16>(y-pad), static_cast<SFP16>(y+pad),
                   ci, a);
    }

    void drawHUD(uint32_t score, uint8_t combo, uint8_t, uint8_t) override
    {
        // In Web build: call into JS via EM_ASM for canvas 2D text overlay.
        EM_ASM({
            const el = document.getElementById('hud');
            if (el) el.textContent = 'Score: ' + $0 + '  x' + $1;
        }, score, combo);
    }

    void updateCamera(FP16 trackPos, FP16 laneX) override
    {
        // Build perspective + view matrix in float here.
        // trackPos and laneX are converted once, right here, never before.
        float fTrack = static_cast<float>(trackPos) / 256.0f;
        float fLaneX = static_cast<float>(laneX)    / 256.0f;
        // ... build mvp matrix and glUniformMatrix4fv(uMVP, ...) ...
        (void)fTrack; (void)fLaneX; // full matrix build omitted for brevity
    }

private:
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = 0;
    GLuint prog = 0, vbo = 0;
    GLint  aPos = 0, uMVP = 0, uColor = 0;

    void submitQuad(SFP16 xL, SFP16 xR, SFP16 yB, SFP16 yT,
                    uint8_t ci, uint8_t alpha)
    {
        // Fixed→float ONLY inside the PAL, exactly once per vertex.
        float fl = static_cast<float>(xL) / 256.0f;
        float fr = static_cast<float>(xR) / 256.0f;
        float fb = static_cast<float>(yB) / 256.0f;
        float ft = static_cast<float>(yT) / 256.0f;

        const RGBA& c = s_palette[ci % PALETTE_SIZE];
        glUniform4f(uColor,
            c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, alpha / 255.0f);

        // Triangle strip: TL, TR, BL, BR
        float verts[12] = {
            fl, ft, 0,
            fr, ft, 0,
            fl, fb, 0,
            fr, fb, 0,
        };
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(aPos);
        glVertexAttribPointer(aPos, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    static GLuint buildShader(const char* vs, const char* fs)
    {
        auto compile = [](GLenum type, const char* src) {
            GLuint s = glCreateShader(type);
            glShaderSource(s, 1, &src, nullptr);
            glCompileShader(s);
            return s;
        };
        GLuint p = glCreateProgram();
        glAttachShader(p, compile(GL_VERTEX_SHADER,   vs));
        glAttachShader(p, compile(GL_FRAGMENT_SHADER, fs));
        glLinkProgram(p);
        return p;
    }
};

// ---------------------------------------------------------------------------
// Web_Audio  (Web Audio API via EM_JS bridge)
// ---------------------------------------------------------------------------

class Web_AudioImpl : public AudioInterface
{
public:
    bool init() override
    {
        EM_ASM({
            window._audioCtx  = new (window.AudioContext || window.webkitAudioContext)();
            window._analyser  = window._audioCtx.createAnalyser();
            window._analyser.fftSize = 256;
            window._freqData  = new Uint8Array(window._analyser.frequencyBinCount);
            window._audioEl   = document.getElementById('audio-src');
            window._trackDur  = 1;
            window._audioProgress = 0;
        });
        return true;
    }

    bool play(uint8_t songId) override
    {
        EM_ASM({
            // Song URLs keyed by index — populated by the HTML shell.
            const urls = window._songTable || [];
            if (urls[$0]) {
                window._audioEl.src = urls[$0];
                window._audioEl.play();
                window._audioEl.onloadedmetadata = function() {
                    window._trackDur = window._audioEl.duration || 1;
                };
            }
        }, songId);
        return true;
    }

    void setPaused(bool p) override
    {
        EM_ASM({ p ? window._audioEl.pause() : window._audioEl.play(); }, p ? 1 : 0);
    }

    void stop() override { EM_ASM({ window._audioEl.pause(); window._audioEl.currentTime=0; }); }

    FP16 getTrackProgress() override
    {
        // Read current time from JS and map to [0, 65535].
        // FP shortcut: integer multiply + shift, same formula as DC side.
        double t   = EM_ASM_DOUBLE({ return window._audioEl.currentTime || 0; });
        double dur = EM_ASM_DOUBLE({ return window._trackDur || 1; });
        uint32_t prog = static_cast<uint32_t>((t / dur) * 65535.0);
        return static_cast<FP16>(prog & 0xFFFF);
    }

    uint8_t getEnergyLevel() override
    {
        // Pull average energy from the Web Audio AnalyserNode.
        return static_cast<uint8_t>(EM_ASM_INT({
            if (!window._analyser) return 128;
            window._analyser.getByteFrequencyData(window._freqData);
            let sum = 0;
            for (let i = 0; i < window._freqData.length; ++i) sum += window._freqData[i];
            return (sum / window._freqData.length) | 0;
        }));
    }

    void shutdown() override { EM_ASM({ if(window._audioCtx) window._audioCtx.close(); }); }
};

// ---------------------------------------------------------------------------
// Web_Input  (Emscripten keyboard callbacks)
// ---------------------------------------------------------------------------

static uint8_t s_keyStateCurr = 0;
static uint8_t s_keyStatePrev = 0;

static EM_BOOL keyCallback(int eventType, const EmscriptenKeyboardEvent* e, void*)
{
    using A = InputAction;
    uint8_t bit = 0;
    if      (e->keyCode == 37 || e->keyCode == 65) bit = static_cast<uint8_t>(A::LaneLeft);
    else if (e->keyCode == 39 || e->keyCode == 68) bit = static_cast<uint8_t>(A::LaneRight);
    else if (e->keyCode == 27)                     bit = static_cast<uint8_t>(A::Pause);
    else if (e->keyCode == 13)                     bit = static_cast<uint8_t>(A::Confirm);
    else if (e->keyCode == 8)                      bit = static_cast<uint8_t>(A::Back);

    if (eventType == EMSCRIPTEN_EVENT_KEYDOWN) s_keyStateCurr |= bit;
    else                                        s_keyStateCurr &= ~bit;
    return EM_TRUE;
}

class Web_InputImpl : public InputInterface
{
public:
    bool init() override
    {
        emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW,
                                        nullptr, EM_TRUE, keyCallback);
        emscripten_set_keyup_callback  (EMSCRIPTEN_EVENT_TARGET_WINDOW,
                                        nullptr, EM_TRUE, keyCallback);
        return true;
    }

    void shutdown() override {}

    void poll() override
    {
        s_keyStatePrev = risingEdgeCapture;
        risingEdgeCapture = s_keyStateCurr;
    }

    InputState readActions() override
    {
        return risingEdgeCapture & ~s_keyStatePrev;
    }

    bool isHeld(InputAction a) override
    {
        return (s_keyStateCurr & static_cast<uint8_t>(a)) != 0;
    }

private:
    uint8_t risingEdgeCapture = 0;
};

// ---------------------------------------------------------------------------
// Static singleton storage (BSS segment — zero heap).
// ---------------------------------------------------------------------------

static Web_GraphicsImpl s_graphics;
static Web_AudioImpl    s_audio;
static Web_InputImpl    s_input;

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

// ---------------------------------------------------------------------------
// main.cpp (Web build)  —  Emscripten main loop via callback.
// ---------------------------------------------------------------------------

#include "../../GameEngine.h"

static Engine::GameEngine s_engine;

static void webMainLoop()
{
    s_engine.tick();
    s_engine.render();
    if (!s_engine.isRunning())
        emscripten_cancel_main_loop();
}

int main()
{
    Engine::PAL::PlatformBundle bundle = Engine::PAL::createPlatform();
    if (!s_engine.init(bundle, /*songId=*/0)) return 1;
    // 0 fps = let the browser drive at requestAnimationFrame rate (~60 Hz).
    emscripten_set_main_loop(webMainLoop, /*fps=*/0, /*simulate_infinite_loop=*/1);
    return 0;
}

#endif // !__DREAMCAST__
