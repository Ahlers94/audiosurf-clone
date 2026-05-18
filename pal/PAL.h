// =============================================================================
// PAL.h
// Platform Abstraction Layer Core Interface Typings & Factory Architecture
// =============================================================================

#ifndef PAL_H
#define PAL_H

#include <cstdint>

namespace Engine {
namespace PAL {

// ---------------------------------------------------------------------------
// FIXED-POINT TYPES (Q8.8 Arithmetic Structures)
// ---------------------------------------------------------------------------
using FP16  = uint16_t; // Unsigned 8.8 Fixed-Point (Range: 0.0 to 255.996)
using SFP16 = int16_t;  // Signed 8.8 Fixed-Point   (Range: -128.0 to 127.996)

// ---------------------------------------------------------------------------
// HARDWARE INPUT INTERFACE ENUMERATIONS & TYPEDEFS
// ---------------------------------------------------------------------------
enum class InputAction : uint8_t {
    None      = 0,
    LaneLeft  = 1 << 0,
    LaneRight = 1 << 1,
    LaneUp    = 1 << 2, // Used for auxiliary lane tracking
    Confirm   = 1 << 3
};

// Bitmask type for handling simultaneous button presses on the hardware bus
using InputState = uint8_t;

// ---------------------------------------------------------------------------
// CONCRETE SUB-SYSTEM HARDWARE ABSTRACTION INTERFACES
// ---------------------------------------------------------------------------

class GraphicsInterface {
public:
    virtual ~GraphicsInterface() = default;

    virtual bool init(uint16_t screenW, uint16_t screenH) = 0;
    virtual void beginFrame() = 0;
    virtual void endFrame() = 0;
    virtual void shutdown() = 0;

    // Camera hooks for potential hardware transformation matrices
    virtual void updateCamera(FP16 trackPos, FP16 laneX) = 0;

    // High-performance geometry primitives mapped to the hardware Tile Accelerator
    virtual void drawVoxelColumn(SFP16 xLeft, SFP16 xRight, SFP16 yBottom, SFP16 yTop, SFP16 z, uint8_t colorIndex) = 0;
    virtual void drawVoxelColumnGlow(SFP16 xLeft, SFP16 xRight, SFP16 yBottom, SFP16 yTop, SFP16 z, uint8_t colorIndex, uint8_t glowAlpha) = 0;
    virtual void drawBlock(SFP16 x, SFP16 y, SFP16 z, uint8_t colorIndex) = 0;
    virtual void drawParticle(SFP16 x, SFP16 y, SFP16 z, uint8_t colorIndex, uint8_t alpha) = 0;

    // HUD text/vector punch-through compiler overlay layer
    virtual void drawHUD(uint32_t score, uint8_t combo, uint8_t nx, uint8_t ny) = 0;
};

class AudioInterface {
public:
    virtual ~AudioInterface() = default;

    virtual bool init() = 0;
    virtual void shutdown() = 0;
    virtual void setPaused(bool paused) = 0;
    virtual void stop() = 0;
    
    // Track streaming mechanics
    virtual bool play(uint8_t songId) = 0;
    virtual FP16 getTrackProgress() = 0;
    virtual uint8_t getEnergyLevel() = 0; // Frequency envelope tracking for visual bounces
};

class InputInterface {
public:
    virtual ~InputInterface() = default;

    virtual bool init() = 0;
    virtual void shutdown() = 0;
    virtual void poll() = 0;

    // Real-time button execution queries
    virtual InputState readPressedActions() = 0;
    virtual InputState readHeldActions() = 0;
    virtual bool isHeld(InputAction action) = 0;
};

// ---------------------------------------------------------------------------
// FACTORY STRUCT PIPELINE LAYER
// ---------------------------------------------------------------------------
struct PlatformBundle {
    GraphicsInterface* graphics = nullptr;
    AudioInterface* audio    = nullptr;
    InputInterface* input    = nullptr;
};

// Lifecyle generation hooks implemented within platform-specific compilation units
PlatformBundle createPlatform();
void destroyPlatform(PlatformBundle& bundle);

} // namespace PAL
} // namespace Engine

#endif // PAL_H
