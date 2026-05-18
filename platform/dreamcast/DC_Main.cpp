// =============================================================================
// DC_Main.cpp  —  Dreamcast KallistiOS entry point
//
// Locks the game loop to the VBL interrupt via vid_waitvbl(), guaranteeing
// a stable 60Hz rendering cadence without inducing SH-4 processing stalls.
// =============================================================================

#ifdef __DREAMCAST__

#include <kos.h>
#include "../../GameEngine.h"

// Static engine instance — resides entirely within the BSS segment, zero heap.
static Engine::GameEngine s_engine;

// KallistiOS Initialisation Configuration:
// INIT_DEFAULT configures core ROM/kernel hooks, standard hardware components, 
// and map controllers. INIT_MALLOCSTATS profiles memory safety.
KOS_INIT_FLAGS(INIT_DEFAULT | INIT_MALLOCSTATS);

int main(int argc, char** argv)
{
    // 1. ALLOCATE AND BIND HARDWARE SUBSYSTEMS VIA PAL FACTORY
    // Subsystem hardware allocations and low-level component initialisations 
    // are completed within this call block.
    Engine::PAL::PlatformBundle bundle = Engine::PAL::createPlatform();

    // 2. BOOT GAME ENGINE SOFTWARE CORE
    // Registers interface pointers and clears static memory pools.
    if (!s_engine.init(bundle, /*songId=*/0)) {
        dbglog(DBG_ERROR, "[DREAMSURF] Critical Error: Core engine initialization failed.\n");
        Engine::PAL::destroyPlatform(bundle);
        return 1;
    }

    dbglog(DBG_INFO, "[DREAMSURF] Initialization successful. Entering 60Hz execution loop.\n");

    // 3. HARDWARE-SYNCHRONIZED GAME LOOP ENVIRONMENT
    while (s_engine.isRunning()) {
        // Run core simulation math calculations and build the upcoming graphic scene 
        // display lists during the display's active raster generation pass.
        s_engine.tick();

        // Halt SH-4 execution threads until the cathode-ray beam hits the vertical blank interval.
        // This naturally throttles engine cycles to a rock-solid 60Hz.
        vid_waitvbl();

        // Instantly flush the prepared display lists down the system bus to the PowerVR chip.
        s_engine.render();
    }

    // 4. CLEAN SUBSYSTEM TEARDOWN UNSPOOLING
    dbglog(DBG_INFO, "[DREAMSURF] Execution loop terminated. Unwinding components...\n");
    s_engine.shutdown();
    Engine::PAL::destroyPlatform(bundle);

    return 0;
}

#endif // __DREAMCAST__
