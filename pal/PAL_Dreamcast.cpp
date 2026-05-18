// =============================================================================
// PAL_Dreamcast.cpp
// Platform Abstraction Layer — Sega Dreamcast / KallistiOS / PowerVR2 (CLX2)
//
// Architecture:
//   - Zero heap allocation: all state in static storage
//   - Fixed-point SFP16 → float conversion: val / 256.0f
//   - Flat-shaded, untextured polygon voxels via PVR immediate-mode pipeline
//   - Opaque geometry on PVR_LIST_OP_POLY; translucent FX on PVR_LIST_TR_POLY
//   - All cube faces fully unrolled as CCW quad-strips (back-face culled)
//   - HUD rendered as flat 2D screen-space quads using a 3×5 pixel-font LUT
// =============================================================================

#include "GameEngine.h"
#include <kos.h>

namespace PAL {

// ---------------------------------------------------------------------------
// Module-private state — no heap, all static
// ---------------------------------------------------------------------------

static pvr_poly_hdr_t s_opaque_hdr;
static pvr_poly_hdr_t s_translucent_hdr;

// Tracks whether we're currently inside a translucent list segment so
// drawBlock() and drawParticle() can interleave safely.
static bool s_in_translucent = false;

// Global LUT: block type → 32-bit packed
