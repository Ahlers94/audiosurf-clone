#pragma once
#include "../FixedPoint.h"

namespace Engine::PAL {

class ClockInterface {
public:
    virtual ~ClockInterface() = default;
    
    // Returns the current master timeline position in FP16 (Q8.8).
    // This value is absolute and monotonically increasing.
    virtual FP16 getCurrentTime() = 0;
};

} // namespace Engine::PAL
