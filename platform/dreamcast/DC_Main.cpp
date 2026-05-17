// =============================================================================
// DC_Main.cpp  —  Dreamcast KallistiOS entry point
//
// KallistiOS provides a standard int main() entry point.
// We drive the game loop locked to the VBL interrupt via vid_waitvbl(),
// which gives us a stable 60Hz cadence without a busy-wait on the SH-4.
// =============================================================================

#ifdef __DREAMCAST__

#include <kos.h>
#include "../../GameEngine.h"

// Static engine instance — lives in BSS, no heap.
static Engine::GameEngine s_engine;

KOS_INIT_FLAGS(INIT_DEFAULT | INIT_MALLOCSTATS);

int main(int, char**)
{
    Engine::PAL::PlatformBundle bundle = Engine::PAL::createPlatform();

    if (!s_engine.init(bundle, /*songId=*/0)) {
        dbglog(DBG_ERROR, "Engine init failed\n");
        return 1;
    }

    while (s_engine.isRunning()) {
        vid_waitvbl();      // Block until next vertical blank — free 60Hz tick.
        s_engine.tick();
        s_engine.render();
    }

    s_engine.shutdown();
    Engine::PAL::destroyPlatform(bundle);
    return 0;
}

#endif // __DREAMCAST__
