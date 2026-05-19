# Audiosurf Clone — Cross-Platform Neon Voxel Engine

A lean *Audiosurf* / *Geometry Wars*-aesthetic clone targeting both **Sega Dreamcast** (KallistiOS) and **modern browsers** (Emscripten/WebAssembly), built around strict data-oriented design, fixed-point arithmetic, and a hard no-heap-allocation policy during gameplay.

---

## Directory Structure

```
src/
├── core/
│   ├── FixedPoint.h        # FP16 type aliases, Q8.8 helpers, bit-shift shortcuts
│   ├── GameTypes.h         # VoxelSegment, Block, Particle, Player structs
│   └── Managers.h          # TrackManager, BlockManager, ParticleManager (static pools)
├── pal/
│   └── PAL.h               # Abstract interfaces: GraphicsInterface, AudioInterface,
│                           #   InputInterface, PlatformBundle, createPlatform()
├── platform/
│   ├── dreamcast/
│   │   ├── DC_Platform.cpp # KallistiOS PVR / AICA / Maple implementations
│   │   └── DC_Main.cpp     # KOS main(), VBL-locked game loop
│   └── web/
│       └── Web_Platform.cpp# WebGL / Web Audio / Emscripten keyboard impls + main()
└── GameEngine.h            # Central engine: init(), tick(), render(), shutdown()
```

---

## Architecture at a Glance

```
┌─────────────────────────────────────────────────────┐
│                     GameEngine.h                     │
│  ┌───────────────┐  ┌──────────────┐  ┌──────────┐  │
│  │ TrackManager  │  │ BlockManager │  │ Particle │  │
│  │ (256 segs)    │  │ (128 blocks) │  │ Manager  │  │
│  │ static array  │  │ static array │  │ (512 pts)│  │
│  └───────────────┘  └──────────────┘  └──────────┘  │
│                    Player (value)                    │
│           Global trackPos: FP16 [0..65535]           │
│                          │                           │
│     FP16 tick() ─────────┤──── render()              │
│                          │          │                │
└──────────────────────────┼──────────┼────────────────┘
                           │          │   PAL boundary
              ┌────────────▼──────────▼────────────┐
              │         PlatformBundle              │
              │  GraphicsInterface*                  │
              │  AudioInterface*                     │
              │  InputInterface*                     │
              └──────┬──────────────────┬───────────┘
                     │                  │
        ┌────────────▼───┐     ┌────────▼──────────┐
        │  DC_Platform   │     │   Web_Platform     │
        │  KallistiOS    │     │   Emscripten       │
        │  PVR / AICA    │     │   WebGL / WebAudio │
        │  Maple Bus     │     │   Keyboard events  │
        └────────────────┘     └────────────────────┘
```

---

## Key Design Decisions

### 1. Fixed-Point Arithmetic (FixedPoint.h)

| Operation | FP Shortcut | Rationale |
|---|---|---|
| Block index from progress | `trackPos >> 8` | No division; power-of-2 boundary |
| Sub-block blend weight | `trackPos & 0xFF` | Free bitmask, no subtract |
| Advance one full segment | `trackPos + 256` | FP_ONE == 256 |
| Q8.8 multiply | `(a*b) >> 8` | Widened to 32-bit, then truncated |
| Lerp between segments | `fp_lerp(a,b,t)` | Shift-based, no divide |
| Glow tier from energy | `energy >> 4` | 16 tiers, free nibble extract |
| Particle alpha fade | `life << 1` | Linear map without multiply |

`float` appears **only** in the PAL implementation files — never in game logic.

### 2. Zero Heap Allocation

All pools are `static` value members in `GameEngine`:

| Pool | Type | Count | Approx. bytes |
|---|---|---|---|
| `TrackManager::segments` | `VoxelSegment[256]` | 256 | ~8 KB |
| `BlockManager::blocks` | `Block[128]` | 128 | ~640 B |
| `ParticleManager::particles` | `Particle[512]` | 512 | ~7 KB |
| `Player` | value | 1 | ~16 B |

Total game-state footprint: **≈ 16 KB** out of the Dreamcast's 16 MB.

### 3. PAL Firewall

`#ifdef __DREAMCAST__` exists in **exactly two files**:
- `DC_Platform.cpp` (guarded `#ifdef __DREAMCAST__`)
- `Web_Platform.cpp` (guarded `#ifndef __DREAMCAST__`)

No other file contains platform conditionals.

### 4. Neon Bloom Without a Shader

Two draw calls per voxel column per frame:

```
Pass 1 (glow): wider quad, additive alpha blend, alpha = energyLevel >> 1
Pass 2 (solid): tight inner quad, normal blend, fully opaque
```

The PVR's tile-based renderer composes these at zero CPU cost on Dreamcast.
WebGL uses `glBlendFunc(GL_SRC_ALPHA, GL_ONE)` for the same effect.

---

## Build Instructions

### Dreamcast (KallistiOS)

```bash
# Source your KOS environment
source /opt/toolchains/dc/kos/environ.sh

# Compile DC_Platform.cpp + DC_Main.cpp + headers
kos-cc -O2 -std=c++17 \
    src/platform/dreamcast/DC_Platform.cpp \
    src/platform/dreamcast/DC_Main.cpp \
    -I src/ \
    -o game.elf

# Wrap for burning to CD-R or running in lxdream
$KOS_BASE/utils/scramble/scramble game.elf 1ST_READ.BIN
```

### Web / Emscripten

```bash
em++ -O2 -std=c++17 \
    src/platform/web/Web_Platform.cpp \
    -I src/ \
    -s USE_WEBGL2=1 \
    -s FULL_ES3=1 \
    -s ALLOW_MEMORY_GROWTH=0 \
    -s INITIAL_MEMORY=33554432 \
    --shell-file shell.html \
    -o index.html
```

`ALLOW_MEMORY_GROWTH=0` enforces the static allocation contract at the linker level — any accidental heap allocation will hard-fault in development, never silently in production.
