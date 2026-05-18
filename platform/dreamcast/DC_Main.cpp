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
    // Pipeline Cadence: Compute -> Build Scene Lists -> Wait For VBL -> Hardware Register Swap
    while (s_engine.isRunning()) {
        
        // Force the Maple bus driver to harvest peripheral inputs immediately before physics compilation
        maple_poll();

        // Compute simulation state parameters (timelines, judgment checks, matrix shifts)
        // during the display's active raster generation pass.
        s_engine.tick();

        // Process rendering loops and submit geometry blocks to the PowerVR2 vertex buffers.
        // Doing this now maximizes our computation window while the TV beam scans pixels.
        s_engine.render();

        // Halt SH-4 execution threads until the cathode-ray beam hits the vertical blank interval.
        // This naturally throttles engine cycles to a rock-solid 60Hz.
        vid_waitvbl();

        // Post-VBL Hardware Page-Flip:
        // Signal the platform abstraction layer to close the scene queues and execute the 
        // final register swap (e.g., pvr_scene_finish) instantly inside the safe blanking window.
        if (bundle.graphics) {
            bundle.graphics->endFrame(); 
        }
    }

    // 4. CLEAN SUBSYSTEM TEARDOWN UNSPOOLING
    dbglog(DBG_INFO, "[DREAMSURF] Execution loop terminated. Unwinding components...\n");
    s_engine.shutdown();
    Engine::PAL::destroyPlatform(bundle);

    return 0;
}

#endif // __DREAMCAST__
