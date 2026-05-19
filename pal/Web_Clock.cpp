#include "Clock.h"
#include <emscripten/emscripten.h>

namespace Engine::PAL {

class Web_Clock : public ClockInterface {
public:
    FP16 getCurrentTime() override {
        // emscripten_get_now() returns milliseconds as a double.
        // We map this to our 0x0000-0xFFFF timeline (assuming a 
        // ~182-second song max for a 16-bit timeline range).
        double ms = emscripten_get_now();
        
        // Example scaling: 1ms = X ticks. 
        // Adjust the divisor based on your target song length.
        return static_cast<FP16>((static_cast<uint32_t>(ms) / 5) & 0xFFFF);
    }
};

} // namespace Engine::PAL
