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
// CLOCK INTERFACE (Master Simulation Heartbeat)
// ---------------------------------------------------------------------------
class ClockInterface {
public:
    virtual ~ClockInterface() = default;
    
    // Returns the current master timeline position in FP16 (Q8.8).
    // This value is the absolute, monotonically increasing synchronization pulse.
    virtual FP16 getCurrentTime() = 0;
};

// ---------------------------------------------------------------------------
// HARDWARE INPUT INTERFACE ENUMERATIONS & TYPEDEFS
// ---------------------------------------------------------------------------
enum class InputAction : uint8_t {
    None      = 0,
    LaneLeft  = 1 << 0,
    LaneRight = 1 << 1,
    LaneUp    = 1 << 2,
    Confirm   = 1 << 3
};

using InputState = uint8_t;

// ---------------------------------------------------------------------------
// CONCRETE SUB-SYSTEM HARDWARE ABSTRACTION INTERFACES
// ---------------------------------------------------------------------------

class GraphicsInterface { /* ... existing ... */ };
class AudioInterface { /* ... existing ... */ };
class InputInterface { /* ... existing ... */ };

// ---------------------------------------------------------------------------
// FACTORY STRUCT PIPELINE LAYER
// ---------------------------------------------------------------------------
struct PlatformBundle {
    GraphicsInterface* graphics = nullptr;
    AudioInterface*    audio    = nullptr;
    InputInterface*    input    = nullptr;
    ClockInterface*    clock    = nullptr; // Integrated the Master Clock
};

// Lifecyle generation hooks implemented within platform-specific compilation units
PlatformBundle createPlatform();
void destroyPlatform(PlatformBundle& bundle);

} // namespace PAL
} // namespace Engine

#endif // PAL_H
