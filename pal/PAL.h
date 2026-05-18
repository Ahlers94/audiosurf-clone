// =============================================================================
// PAL.h  —  Platform Abstraction Layer
// Pure abstract C++ interfaces that firewall all game logic from hardware.
//
// RULE: Platform macros MUST NOT appear outside of an implementation file.
// =============================================================================

#pragma once
#include <cstdint>
#include "FixedPoint.h"
#include "GameTypes.h"

namespace Engine {
namespace PAL {

// ===========================================================================
// Colour helper — RGBA packed into 32 bits.
// ===========================================================================
struct Color32
{
    uint8_t r, g, b, a;

    /// Construct from a palette index. The PAL implementation owns the actual lookup table.
    static Color32 fromPalette(uint8_t index, uint8_t alpha = 255);
};

// ===========================================================================
// Input Actions & Type Definitions
// ===========================================================================
enum class InputAction : uint8_t
{
    None      = 0,
    LaneLeft  = (1 << 0),  ///< Shift one lane left.
    LaneRight = (1 << 1),  ///< Shift one lane right.
    Pause     = (1 << 2),  ///< Toggle pause menu.
    Confirm   = (1 << 3),  ///< Menu confirm / select.
    Back      = (1 << 4),  ///< Menu back / cancel.
};

/// Bitfield mask definition for clean input action evaluations.
using InputState = uint8_t;

// ===========================================================================
// GraphicsInterface
// ===========================================================================
class GraphicsInterface
{
protected:
    /// Protected non-virtual destructor blocks dynamic delete operations,
    /// completely stripping dynamic allocation overhead from the linked binary.
    ~GraphicsInterface() = default;

public:
    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------
    virtual bool init(uint16_t screenW, uint16_t screenH) = 0;
    virtual void beginFrame() = 0;
    virtual void endFrame() = 0;
    virtual void shutdown() = 0;

    // -----------------------------------------------------------------------
    // Track Geometry Rendering
    // -----------------------------------------------------------------------
    
    /// Draw one column voxel segment geometry using Q8.8 world coordinates.
    /// z represents the segment distance depth along the track timeline.
    virtual void drawVoxelColumn(
        SFP16 xLeft, SFP16 xRight,
        SFP16 yBottom, SFP16 yTop, SFP16 z,
        uint8_t colorIndex) = 0;

    /// Draw the wider semi-transparent glow backdrop using additive hardware alpha blending.
    virtual void drawVoxelColumnGlow(
        SFP16 xLeft, SFP16 xRight,
        SFP16 yBottom, SFP16 yTop, SFP16 z,
        uint8_t colorIndex,
        uint8_t glowAlpha) = 0;

    // -----------------------------------------------------------------------
    // Entities & Overlays
    // -----------------------------------------------------------------------
    virtual void drawBlock(SFP16 x, SFP16 y, SFP16 z, uint8_t colorIndex) = 0;
    virtual void drawParticle(SFP16 x, SFP16 y, SFP16 z, uint8_t colorIndex, uint8_t alpha) = 0;
    virtual void drawHUD(uint32_t score, uint8_t combo, uint8_t nx, uint8_t ny) = 0;
    virtual void updateCamera(FP16 trackPos, FP16 laneX) = 0;
};

// ===========================================================================
// AudioInterface
// ===========================================================================
class AudioInterface
{
protected:
    ~AudioInterface() = default;

public:
    virtual bool init() = 0;
    virtual bool play(uint8_t songId) = 0;
    virtual void setPaused(bool paused) = 0;
    virtual void stop() = 0;
    virtual FP16 getTrackProgress() = 0;
    virtual uint8_t getEnergyLevel() = 0;
    virtual void shutdown() = 0;
};

// ===========================================================================
// InputInterface
// ===========================================================================
class InputInterface
{
protected:
    ~InputInterface() = default;

public:
    virtual bool init() = 0;
    virtual void poll() = 0;
    virtual InputState readActions() = 0;
    virtual bool isHeld(InputAction action) = 0;
    virtual void shutdown() = 0;
};

// ===========================================================================
// Platform Bundle Aggregator
// ===========================================================================
struct PlatformBundle
{
    GraphicsInterface* graphics = nullptr;
    AudioInterface*    audio    = nullptr;
    InputInterface*    input    = nullptr;
};

// ===========================================================================
// Static Allocation Factory Declarations
// ===========================================================================
PlatformBundle createPlatform();
void destroyPlatform(PlatformBundle& bundle);

} // namespace PAL
} // namespace Engine
