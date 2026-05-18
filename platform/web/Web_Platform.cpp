// =============================================================================
// Web_Platform.cpp  —  WebAssembly / WebGL / Web Audio PAL stubs
// =============================================================================

#ifndef __DREAMCAST__

#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h> // Upgraded to real native GLES3 context headers

#include "../../pal/PAL.h"

namespace Engine {
namespace PAL {

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

static const char* s_vert_src = R"GLSL(#version 300 es
    in vec3 aPos;
    uniform mat4 uMVP;
    void main() { gl_Position = uMVP * vec4(aPos, 1.0); }
)GLSL";

static const char* s_frag_src = R"GLSL(#version 300 es
    precision mediump float;
    uniform vec4 uColor;
    out vec4 fragColor;
    void main() { fragColor = uColor; }
)GLSL";

// ---------------------------------------------------------------------------
// Web_Graphics
// ---------------------------------------------------------------------------
class Web_GraphicsImpl : public GraphicsInterface
{
public:
    bool init(uint16_t w, uint16_t h) override
    {
        EmscriptenWebGLContextAttributes attr;
        emscripten_webgl_init_context_attributes(&attr);
        attr.majorVersion = 2; // WebGL 2.0 Core Context Binding
        attr.minorVersion = 0;
        attr.alpha        = EM_FALSE;
        attr.depth        = EM_TRUE;

        ctx = emscripten_webgl_create_context("#canvas", &attr);
        if (!ctx) return false;
        
        emscripten_webgl_make_context_current(ctx);

        screenW = w; 
        screenH = h;
        glViewport(0, 0, w, h);
        glEnable(GL_BLEND);

        prog   = buildShader(s_vert_src, s_frag_src);
        aPos   = glGetAttribLocation(prog, "aPos");
        uMVP   = glGetUniformLocation(prog, "uMVP");
        uColor = glGetUniformLocation(prog, "uColor");

        glGenBuffers(1, &vbo);
        glGenVertexArrays(1, &vao);
        return true;
    }

    void beginFrame() override
    {
        glClearColor(0.0f, 0.0f, 0.01f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(prog);

        // 2D Orthographic Projection Matrix: Maps spatial bounds [0..640]x[0..480] to NDC [-1..1]
        // Left=0.0, Right=640.0, Bottom=0.0, Top=480.0, Near=-1.0, Far=1.0
        float orthoProjection[16] = {
            2.0f / 640.0f,  0.0f,           0.0f,  0.0f,
            0.0f,           2.0f / 480.0f,  0.0f,  0.0f,
            0.0f,           0.0f,          -1.0f,  0.0f,
           -1.0f,          -1.0f,           0.0f,  1.0f
        };
        glUniformMatrix4fv(uMVP, 1, GL_FALSE, orthoProjection);
    }

    void endFrame() override {}
    void shutdown() override 
    {
        glDeleteBuffers(1, &vbo);
        glDeleteVertexArrays(1, &vao);
        glDeleteProgram(prog);
        emscripten_webgl_destroy_context(ctx);
    }

    void drawVoxelColumn(SFP16 xL, SFP16 xR, SFP16 yB, SFP16 yT, SFP16 z, uint8_t ci) override
    {
        // Normal alpha blending pass for the solid track geometry
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        submitQuad(xL, xR, yB, yT, z, ci, 255);
    }

    void drawVoxelColumnGlow(SFP16 xL, SFP16 xR, SFP16 yB, SFP16 yT, SFP16 z, uint8_t ci, uint8_t alpha) override
    {
        // Additive blending pass for the bloom overlay
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        submitQuad(xL, xR, yB, yT, z, ci, alpha);
    }

    void drawBlock(SFP16 x, SFP16 y, SFP16 z, uint8_t ci) override
    {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        SFP16 pad = 16 << 8; // Coordinate system padding matching fixed point scaling structures
        submitQuad(static_cast<SFP16>(x - pad), static_cast<SFP16>(x + pad),
                   static_cast<SFP16>(y - pad), static_cast<SFP16>(y + pad), z, ci, 220);
    }

    void drawParticle(SFP16 x, SFP16 y, SFP16 z, uint8_t ci, uint8_t a) override
    {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        SFP16 pad = 4 << 8;
        submitQuad(static_cast<SFP16>(x - pad), static_cast<SFP16>(x + pad),
                   static_cast<SFP16>(y - pad), static_cast<SFP16>(y + pad), z, ci, a);
    }

    void drawHUD(uint32_t score, uint8_t combo, uint8_t, uint8_t) override
    {
        EM_ASM({
            const el = document.getElementById('hud');
            if (el) el.textContent = 'SCORE: ' + $0 + '  COMBO: x' + $1;
        }, score, combo);
    }

    void updateCamera(FP16 trackPos, FP16 laneX) override
    {
        (void)trackPos; (void)laneX;
    }

private:
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = 0;
    GLuint prog = 0, vbo = 0, vao = 0;
    GLint  aPos = 0, uMVP = 0, uColor = 0;
    uint16_t screenW = 640, screenH = 480;

    void submitQuad(SFP16 xL, SFP16 xR, SFP16 yB, SFP16 yT, SFP16 z, uint8_t ci, uint8_t alpha)
    {
        // Unpack coordinates from standard Q8.8 fixed-point layouts
        float fl = (static_cast<float>(xL) / 256.0f * 2.5f) + 80.0f;
        float fr = (static_cast<float>(xR) / 256.0f * 2.5f) + 80.0f;
        float fb = (static_cast<float>(yB) / 256.0f * 3.0f);
        float ft = (static_cast<float>(yT) / 256.0f * 3.0f);
        float fz = static_cast<float>(z)  / 256.0f;

        const RGBA& c = s_palette[ci % PALETTE_SIZE];
        glUniform4f(uColor, c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, alpha / 255.0f);

        float verts[12] = {
            fl, ft, -fz,
            fr, ft, -fz,
            fl, fb, -fz,
            fr, fb, -fz,
        };

        glBindVertexArray(vao);
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
// Web_Audio
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

            // Connect your HTML5 element directly to your Web Audio node pipeline graph
            window._source = window._audioCtx.createMediaElementSource(window._audioEl);
            window._source.connect(window._analyser);
            window._analyser.connect(window._audioCtx.destination);
        });
        return true;
    }

    bool play(uint8_t songId) override
    {
        EM_ASM({
            const urls = window._songTable || [];
            if (urls[$0]) {
                window._audioEl.src = urls[$0];
                
                // Keep audio context synchronized with active browser windows
                if (window._audioCtx.state === 'suspended') {
                    const unlock = () => {
                        window._audioCtx.resume().then(() => {
                            window._audioEl.play();
                        });
                        document.removeEventListener('keydown', unlock);
                    };
                    document.addEventListener('keydown', unlock);
                } else {
                    window._audioEl.play();
                }

                window._audioEl.onloadedmetadata = function() {
                    window._trackDur = window._audioEl.duration || 1;
                };
            }
        }, songId);
        return true;
    }

    void setPaused(bool p) override
    {
        EM_ASM({ 
            if ($0) {
                window._audioEl.pause(); 
            } else {
                if (window._audioCtx.state === 'suspended') {
                    window._audioCtx.resume();
                }
                window._audioEl.play();
            }
        }, p ? 1 : 0);
    }

    void stop() override { EM_ASM({ window._audioEl.pause(); window._audioEl.currentTime = 0; }); }

    FP16 getTrackProgress() override
    {
        double t   = EM_ASM_DOUBLE({ return window._audioEl.currentTime || 0; });
        double dur = EM_ASM_DOUBLE({ return window._trackDur || 1; });
        uint32_t prog = static_cast<uint32_t>((t / dur) * 65535.0);
        return static_cast<FP16>(prog & 0xFFFF);
    }

    uint8_t getEnergyLevel() override
    {
        return static_cast<uint8_t>(EM_ASM_INT({
            if (!window._analyser || window._audioCtx.state !== 'running') return 128;
            window._analyser.getByteFrequencyData(window._freqData);
            let sum = 0;
            for (let i = 0; i < window._freqData.length; ++i) sum += window._freqData[i];
            return (sum / window._freqData.length) | 0;
        }));
    }

    void shutdown() override { EM_ASM({ if(window._audioCtx) window._audioCtx.close(); }); }
};

// ---------------------------------------------------------------------------
// Web_Input
// ---------------------------------------------------------------------------
static volatile uint8_t s_keyStateAtomic = 0; // Guarded register against asynchronous racing updates
static uint8_t s_keyStateCurr = 0;
static uint8_t s_keyStatePrev = 0;

static EM_BOOL keyCallback(int eventType, const EmscriptenKeyboardEvent* e, void*)
{
    using A = InputAction;
    uint8_t bit = 0;
    if      (e->keyCode == 37 || e->keyCode == 65) bit = static_cast<uint8_t>(A::LaneLeft);
    else if (e->keyCode == 39 || e->keyCode == 68) bit = static_cast<uint8_t>(A::LaneRight);
    else if (e->keyCode == 27)                    bit = static_cast<uint8_t>(A::Pause);
    else if (e->keyCode == 13)                    bit = static_cast<uint8_t>(A::Confirm);
    else if (e->keyCode == 8)                     bit = static_cast<uint8_t>(A::Back);

    if (eventType == EMSCRIPTEN_EVENT_KEYDOWN) {
        s_keyStateAtomic |= bit;
    } else {
        s_keyStateAtomic &= ~bit;
    }
    return EM_TRUE;
}

class Web_InputImpl : public InputInterface
{
public:
    bool init() override
    {
        emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, keyCallback);
        emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, keyCallback);
        return true;
    }

    void shutdown() override {}

    void poll() override
    {
        s_keyStatePrev = s_keyStateCurr;
        s_keyStateCurr = s_keyStateAtomic; // Atomically lock intermediate frame register state
    }

    InputState readActions() override        { return s_keyStateCurr & ~s_keyStatePrev; }
    bool       isHeld(InputAction a) override { return (s_keyStateCurr & static_cast<uint8_t>(a)) != 0; }
};

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
// main.cpp
// ---------------------------------------------------------------------------
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
    emscripten_set_main_loop(webMainLoop, 0, 1);
    return 0;
}

#endif // !__DREAMCAST__
