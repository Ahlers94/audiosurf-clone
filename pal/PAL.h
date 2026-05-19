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
// FIXED-POINT TYPES
// Updated to 32-bit to support larger coordinate ranges and prevent 
// narrowing/overflow during platform-specific arithmetic.
// ---------------------------------------------------------------------------
using FP16  = uint32_t; // Unsigned 16.16 Fixed-Point (Effective range)
using SFP16 = int32_t;  // Signed 16.16 Fixed-Point   (Effective range)

#define PALETTE_SIZE 8

// ---------------------------------------------------------------------------
// CLOCK INTERFACE
// ---------------------------------------------------------------------------
class ClockInterface {
public:
    virtual ~ClockInterface() = default;
    virtual FP16 getCurrentTime() = 0;
};

// ---------------------------------------------------------------------------
// INPUT INTERFACE
// ---------------------------------------------------------------------------
enum class InputAction : uint8_t {
    None      = 0,
    LaneLeft  = 1 << 0,
    LaneRight = 1 << 1,
    LaneUp    = 1 << 2,
    Confirm   = 1 << 3,
    Pause     = 1 << 4,
    Back      = 1 << 5
};

using InputState = uint8_t;

class InputInterface {
public:
    virtual ~InputInterface() = default;
    virtual bool init() = 0;
    virtual void shutdown() = 0;
    virtual void poll() = 0;
    virtual InputState readPressedActions() = 0;
    virtual InputState readHeldActions() = 0;
    virtual bool isHeld(InputAction a) = 0;
};

// ---------------------------------------------------------------------------
// GRAPHICS INTERFACE
// ---------------------------------------------------------------------------
class GraphicsInterface {
public:
    virtual ~GraphicsInterface() = default;
    virtual bool init(uint16_t w, uint16_t h) = 0;
    virtual void beginFrame() = 0;
    virtual void endFrame() = 0;
    virtual void shutdown() = 0;
    virtual void drawVoxelColumn(SFP16 xL, SFP16 xR, SFP16 yB, SFP16 yT, SFP16 z, uint8_t ci) = 0;
    virtual void drawVoxelColumnGlow(SFP16 xL, SFP16 xR, SFP16 yB, SFP16 yT, SFP16 z, uint8_t ci, uint8_t alpha) = 0;
    virtual void drawBlock(SFP16 x, SFP16 y, SFP16 z, uint8_t ci) = 0;
    virtual void drawParticle(SFP16 x, SFP16 y, SFP16 z, uint8_t ci, uint8_t a) = 0;
    virtual void drawHUD(uint32_t score, uint8_t combo, uint8_t miss, uint8_t extra) = 0;
    virtual void updateCamera(FP16 trackPos, FP16 laneX) = 0;
};

// ---------------------------------------------------------------------------
// AUDIO INTERFACE
// ---------------------------------------------------------------------------
class AudioInterface {
public:
    virtual ~AudioInterface() = default;
    virtual bool init() = 0;
    virtual bool play(uint8_t songId) = 0;
    virtual void setPaused(bool p) = 0;
    virtual void stop() = 0;
    virtual FP16 getTrackProgress() = 0;
    virtual uint8_t getEnergyLevel() = 0;
    virtual void shutdown() = 0;
    virtual bool isPaused() const = 0;
};

// ---------------------------------------------------------------------------
// FACTORY STRUCT PIPELINE LAYER
// ---------------------------------------------------------------------------
struct PlatformBundle {
    GraphicsInterface* graphics = nullptr;
    AudioInterface*    audio    = nullptr;
    InputInterface*    input    = nullptr;
    ClockInterface*    clock    = nullptr;
};

PlatformBundle createPlatform();
void destroyPlatform(PlatformBundle& bundle);

} // namespace PAL
} // namespace Engine

#endif // PAL_H
