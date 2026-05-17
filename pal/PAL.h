// =============================================================================
// PAL.h  —  Platform Abstraction Layer
// Pure abstract C++ interfaces that firewall all game logic from hardware.
//
// RULE: The string "#ifdef __DREAMCAST__" (or any platform macro) MUST NOT
//       appear anywhere outside of a PAL *implementation* file.
//       Core engine code talks only to these interfaces — never to KallistiOS
//       or Emscripten/WebGL APIs directly.
//
// Implementations live in:
//   platform/dreamcast/DC_Graphics.h/.cpp
//   platform/dreamcast/DC_Audio.h/.cpp
//   platform/dreamcast/DC_Input.h/.cpp
//   platform/web/Web_Graphics.h/.cpp
//   platform/web/Web_Audio.h/.cpp
//   platform/web/Web_Input.h/.cpp
//
// The factory function  PAL::createPlatform()  is the ONLY call site that
// contains a compile-time platform branch.  It is defined in a single
// translation unit per build target.
// =============================================================================

#pragma once
#include <cstdint>
#include "FixedPoint.h"
#include "GameTypes.h"

namespace Engine {
namespace PAL {

// ===========================================================================
// Colour helper — RGBA packed into 32 bits.
// Passed across the PAL boundary so the renderer owns the colour pipeline.
// ===========================================================================

struct Color32
{
    uint8_t r, g, b, a;

    /// Construct from a palette index.  The engine passes an index;
    /// the PAL implementation owns the actual palette table.
    static Color32 fromPalette(uint8_t index, uint8_t alpha = 255);
};

// ===========================================================================
// GraphicsInterface
// ===========================================================================
//
// Owns the window/display, the render context, and all draw calls.
//
// Floating-point coordinates appear ONLY here — the rest of the engine
// works in FP16.  Each draw call receives FP16 world-space data and the
// implementation is responsible for converting to NDC / screen-space floats.
//
// The neon-bloom effect is achieved in two draw calls:
//   1. drawTrackStripGlow()  — wider, semi-transparent additive quad strip.
//   2. drawTrackStrip()      — tight solid inner geometry drawn over it.
// Both use hardware alpha blending so no off-screen framebuffer is needed.
// ===========================================================================

class GraphicsInterface
{
public:
    virtual ~GraphicsInterface() = default;

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    /// Create window / GL context / PVR context.  Called once at startup.
    virtual bool init(uint16_t screenW, uint16_t screenH) = 0;

    /// Begin a new frame — clear buffers, start display list (Dreamcast)
    /// or clear WebGL canvas.
    virtual void beginFrame() = 0;

    /// Flip / present the finished frame.
    virtual void endFrame() = 0;

    /// Teardown — release all platform resources.
    virtual void shutdown() = 0;

    // -----------------------------------------------------------------------
    // Track geometry — triangle-strip based voxel renderer
    // -----------------------------------------------------------------------

    /// Draw one column-wide voxel quad strip for a single segment column.
    /// xLeft, xRight, yBottom, yTop are Q8.8 world-space coordinates.
    /// The PAL converts them to floats internally.
    /// colorIndex selects from the platform's neon palette table.
    virtual void drawVoxelColumn(
        SFP16 xLeft, SFP16 xRight,
        SFP16 yBottom, SFP16 yTop,
        uint8_t colorIndex) = 0;

    /// Draw the wider semi-transparent glow backdrop for one voxel column.
    /// glowAlpha is [0, 255].  Implementation uses additive blending.
    virtual void drawVoxelColumnGlow(
        SFP16 xLeft, SFP16 xRight,
        SFP16 yBottom, SFP16 yTop,
        uint8_t colorIndex,
        uint8_t glowAlpha) = 0;

    // -----------------------------------------------------------------------
    // Blocks & Particles
    // -----------------------------------------------------------------------

    /// Draw a single collectible block at the given world position.
    virtual void drawBlock(SFP16 x, SFP16 y, SFP16 z, uint8_t colorIndex) = 0;

    /// Draw one particle spark (a small additive-blended quad).
    virtual void drawParticle(SFP16 x, SFP16 y, SFP16 z,
                              uint8_t colorIndex, uint8_t alpha) = 0;

    // -----------------------------------------------------------------------
    // HUD
    // -----------------------------------------------------------------------

    /// Draw a simple integer score string at normalised screen position.
    /// (nx, ny) in [0, 255] map to [0.0, 1.0] NDC — no floats in caller.
    virtual void drawHUD(uint32_t score, uint8_t combo,
                         uint8_t nx, uint8_t ny) = 0;

    // -----------------------------------------------------------------------
    // Camera
    // -----------------------------------------------------------------------

    /// Update the camera transform from the player's current fixed-point
    /// track position and lane offset.  Conversion to a float view matrix
    /// happens entirely inside the implementation.
    virtual void updateCamera(FP16 trackPos, FP16 laneX) = 0;
};

// ===========================================================================
// AudioInterface
// ===========================================================================
//
// Owns MP3 decode / streaming and exposes a pull-based beat/energy API.
//
// The engine never touches audio sample data.  It only:
//   a) triggers playback of a named track (a path or asset index),
//   b) polls for the normalised energy level at the current playback cursor.
//
// On Dreamcast the implementation drives the AICA via KallistiOS sndstream.
// On Web it wraps the Web Audio API AnalyserNode exposed through Emscripten.
// ===========================================================================

class AudioInterface
{
public:
    virtual ~AudioInterface() = default;

    /// Initialise audio subsystem.  Returns false on failure.
    virtual bool init() = 0;

    /// Load and begin playback of a song.
    /// `songId` is a small integer index into the asset table
    /// (avoids passing a string/path through the interface).
    virtual bool play(uint8_t songId) = 0;

    /// Pause / resume toggle.
    virtual void setPaused(bool paused) = 0;

    /// Stop playback and reset cursor.
    virtual void stop() = 0;

    /// Returns the current playback position as a FP16 progress value
    /// in [0, 65535], synchronised with the song duration.
    /// The engine reads this each tick and writes it to the global
    /// trackPos register — this is the clock source for the whole engine.
    virtual FP16 getTrackProgress() = 0;

    /// Returns instantaneous audio energy at the playback cursor.
    /// Range [0, 255].  The engine writes this into VoxelSegment.energyLevel
    /// as track segments are pre-generated ahead of the cursor.
    virtual uint8_t getEnergyLevel() = 0;

    /// Teardown.
    virtual void shutdown() = 0;
};

// ===========================================================================
// InputInterface
// ===========================================================================
//
// Abstracts all player input as discrete high-level actions.
// No platform-specific keycodes appear in game logic.
//
// On Dreamcast: reads Maple bus controller via KallistiOS maple_enum_dev().
// On Web:       reads keyboard events via Emscripten HTML5 API callbacks,
//               stored into a small state bitfield and polled here.
// ===========================================================================

/// Discrete actions the player can take.  Fits in a uint8_t bitmask.
enum class InputAction : uint8_t
{
    None        = 0,
    LaneLeft    = (1 << 0),  ///< Shift one lane left.
    LaneRight   = (1 << 1),  ///< Shift one lane right.
    Pause       = (1 << 2),  ///< Toggle pause menu.
    Confirm     = (1 << 3),  ///< Menu confirm / select.
    Back        = (1 << 4),  ///< Menu back / cancel.
};

/// Bitfield of simultaneously active InputActions.
using InputState = uint8_t;

class InputInterface
{
public:
    virtual ~InputInterface() = default;

    /// Initialise input subsystem (enumerate Maple devices, register
    /// JS event listeners, etc.).
    virtual bool init() = 0;

    /// Poll all input devices and update internal state.
    /// Must be called once per game tick before readActions().
    virtual void poll() = 0;

    /// Returns the set of actions that became active THIS tick only
    /// (rising-edge detection — not held state).
    virtual InputState readActions() = 0;

    /// Returns true if an action is currently held (for menus that
    /// support analog-style navigation by hold).
    virtual bool isHeld(InputAction action) = 0;

    /// Teardown.
    virtual void shutdown() = 0;
};

// ===========================================================================
// PlatformBundle
// ===========================================================================
// Aggregates all three interfaces into one struct so GameEngine holds a
// single dependency pointer.  Ownership is by the platform factory.

struct PlatformBundle
{
    GraphicsInterface* graphics = nullptr;
    AudioInterface*    audio    = nullptr;
    InputInterface*    input    = nullptr;
};

// ===========================================================================
// Factory — ONLY location of compile-time platform branching.
// Defined in platform/dreamcast/DC_Platform.cpp  OR
//           platform/web/Web_Platform.cpp
// ===========================================================================

/// Allocates and returns platform-specific implementations.
/// All three objects are allocated in a static storage pool inside
/// the implementation file — no heap allocation.
PlatformBundle createPlatform();

/// Tears down and invalidates a bundle returned by createPlatform().
void destroyPlatform(PlatformBundle& bundle);

} // namespace PAL
} // namespace Engine
