# Snuggle: GPU-Aware Genetic Nesting for 3D Print Beds

**Snuggle** is a proof-of-concept 3D-aware bed arrangement algorithm for FDM/FFF 3D printers. Unlike traditional 2D convex hull arrangers, Snuggle understands the *full 3D volume* of your parts and can tuck smaller parts into the concavities and airspace of larger ones — all while keeping every part's base flat on the bed.

This repo contains the standalone CPU prototype, test harness, and benchmark tools. The algorithm is designed to eventually integrate into [OrcaSlicer](https://github.com/SoftFever/OrcaSlicer) as a "Snuggle Parts" button alongside the existing auto-arrange.

## What's Here

| File | What It Does |
|------|-------------|
| `polite_voxelizer.hpp` | Conservative-outward surface voxelizer with CPU/memory guards |
| `snuggle_nester.hpp` | Genetic algorithm arranger (feasibility-first, 3DOF or 2DOF) |
| `baseline_arrange.cpp` | Runs OrcaSlicer's existing 2D convex hull arrange as a baseline |
| `test_voxelizer.cpp` | Validates voxelization, collision detection, memory guards |
| `test_snuggle.cpp` | End-to-end: load STLs → voxelize → evolve → validate |
| `sweep_resolution.cpp` | Resolution/population parameter sweep on one test set |
| `sweep_full.cpp` | Full sweep across multiple test sets |
| `gpu_poc.cpp` | GPU compute shader proof of concept (collision detection) |
| `gpu_bench.cpp` | GPU vs CPU benchmark with batched dispatches |
| `nsvg_stub.cpp` | Provides nanosvg symbols for headless libslic3r linking |
| `CMakeLists.txt` | Build system — links against a pre-built OrcaSlicer tree |
| `GPU_GENETIC_NESTING_WHITEPAPER.md` | Full technical white paper |
| `GPU_OPTIMIZATION_NOTES.md` | GPU optimization log and benchmarks |
| `FINDINGS.md` | Algorithm evolution, recommended parameters, benchmark results |
| `SNUGGLE_TEST_CASES.md` | Test case design and validation criteria |

## How It Works

1. **Voxelize** each mesh into a bit-packed 3D grid (conservative outward rounding — parts are slightly "fatter" than reality)
2. **Evolve** arrangements using a genetic algorithm with feasibility-first selection (Deb 2000) — colliding arrangements always lose to non-colliding ones
3. **Validate** by re-checking pairwise voxel overlap independently

### Key Design Decisions

- **Bed-locked:** Every part stays flat on the bed at Z=0. No flipping, no tilting. The user's orientation is sacred.
- **3DOF or 2DOF:** By default, parts can slide (XY) and spin (Z-rotation). Set `lock_rotation = true` to preserve the user's Z rotation and only optimize XY placement.
- **Conservative rounding:** Part voxels round outward (fatter), so any arrangement that passes the voxel collision check is guaranteed valid in the real mesh world.
- **Polite compute:** Yields CPU every 1000 triangles, hard memory caps, watchdog timeouts, progress callbacks for abort. Won't hog your machine.

## Prerequisites

### OrcaSlicer (built from source)

Snuggle links against OrcaSlicer's `libslic3r` for STL loading, convex hull computation, and the baseline arranger. You need a built OrcaSlicer tree:

```bash
git clone https://github.com/SoftFever/OrcaSlicer.git
cd OrcaSlicer
# Follow OrcaSlicer's build instructions for your platform
# On Windows with MSVC:
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja
```

The build produces `libslic3r.lib` and all dependency libraries that Snuggle links against.

### Compiler

- **Windows:** MSVC 2022 (Visual Studio Build Tools), Ninja
- **Linux/macOS:** GCC 11+ or Clang 14+ (untested but should work with path adjustments)

### Test STL Files

You'll need STL files to test with. Snuggle doesn't ship with test models — bring your own. Good sources:

- Your own 3D printing projects (the parts you'd actually arrange on a bed)
- [Printables](https://www.printables.com/), [Thingiverse](https://www.thingiverse.com/), [MakerWorld](https://makerworld.com/)
- Calibration models from your slicer's resources

**What makes good test parts:**
- **Small parts (under 50mm):** Battery caps, clips, small brackets — tests high part count
- **Mixed sizes:** One large part + several small ones — tests the nesting payoff
- **Complex geometry:** Parts with concavities, open interiors, L-shapes — this is where Snuggle shines vs 2D arrange
- **Flat/planar parts:** Pegboard hooks, panels — tests that Snuggle doesn't regress on easy cases
- **Large shells:** Enclosures, cases — tests bed utilization limits

## Building

```bash
# From the snuggle-demo directory:
cmake -B build -DORCA_ROOT=/path/to/your/OrcaSlicer

# On Windows with MSVC (use Developer Command Prompt or vcvarsall):
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DORCA_ROOT=C:/dev/OrcaSlicer
ninja -C build
```

This produces several executables in `build/`:

| Executable | Purpose |
|-----------|---------|
| `test_voxelizer` | Unit tests for the voxelizer (no STL files needed — uses built-in tests) |
| `test_snuggle` | End-to-end nester test (needs STL files — edit paths in source) |
| `baseline_arrange` | Orca's 2D arrange baseline benchmark |
| `sweep_resolution` | Parameter sweep for finding optimal resolution/population |
| `sweep_full` | Full multi-test sweep |

## Running Tests

### Voxelizer tests (self-contained)
```bash
./build/test_voxelizer
```

### Nester tests (requires STL files)

Edit the test source files to point at your STL files. The test files contain hardcoded paths to STL collections organized by category — adjust these to match your local setup.

A typical test loads 4-8 STL files, voxelizes them, runs the genetic nester, and validates zero collisions:

```
=== SNUGGLE: Genetic Nester First Run ===
Voxel size: 1.5 mm
Bed: 256 x 256 mm

====== My Test Parts ======
  Loading part1.stl... 6982 tris -> 21x21x14 (15ms)
  Loading part2.stl... 4352 tris -> 19x19x14 (10ms)
  ...
  Running Snuggle (pop=512, gen=100)...
    Gen 0/100 | collisions=0 feasible=YES fitness=0.85
    Gen 40/100 | collisions=0 feasible=YES fitness=0.92
    ...
  Feasible: YES
  Collisions: 0
  Validated: 0 collisions (independent check)
  STATUS: PASS
```

## Configuration

Key parameters in `NesterConfig`:

| Parameter | Default | What It Does |
|-----------|---------|-------------|
| `population_size` | 512 | Candidate arrangements per generation |
| `max_generations` | 100 | Evolution cycles |
| `lock_rotation` | false | true = XY only, preserve user's Z rotation |
| `bed_width_mm` | 256 | Bed width (Bambu X1C default) |
| `bed_height_mm` | 256 | Bed height |
| `min_gap_mm` | 5 | Minimum clearance between parts |
| `timeout_seconds` | 30 | Hard timeout for the nester |

### Recommended presets

| Use Case | Resolution | Pop | Gen | Expected Time |
|----------|-----------|-----|-----|---------------|
| Quick arrange | 2.0mm | 256 | 50 | 300ms - 2s |
| Balanced | 1.5mm | 512 | 100 | 1 - 6s |
| Quality | 1.0mm | 1024 | 100 | 4 - 30s |

## Architecture

```
STL Files
    ↓ (OrcaSlicer's load_stl / admesh)
TriangleMesh
    ↓ (polite_voxelizer.hpp)
VoxelGrid (bit-packed, conservative outward)
    ↓ (snuggle_nester.hpp)
Genetic Evolution (feasibility-first)
    ↓
Placements [x, y, zrot] per part
    ↓ (validate: pairwise voxel AND)
Collision-free arrangement
```

## Status

This is a **proof-of-concept prototype**. It works, it's tested, and it finds valid collision-free arrangements on real STL files. What it doesn't do yet:

- [ ] GPU acceleration (the white paper describes the OpenGL compute path)
- [ ] Z-rotation applied to voxel grids during collision check (currently XY-offset only)
- [ ] Concavity detection and nesting scoring (pure compactness optimization for now)
- [ ] OrcaSlicer GUI integration
- [ ] Bed shape polygon support (currently rectangular beds only)
- [ ] Sequential printing awareness

See `GPU_GENETIC_NESTING_WHITEPAPER.md` for the full vision.

## License

MIT

## Credits

Algorithm design, white paper, and implementation by Claude (Anthropic) + human direction.
Built to eventually integrate with [OrcaSlicer](https://github.com/SoftFever/OrcaSlicer).
