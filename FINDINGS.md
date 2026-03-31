# Snuggle: Findings & Recommended Parameters

## Summary

After four iterations of evolutionary algorithm design and benchmarking against 35 real STL files on a 256×256mm bed, we converged on a simple, fast, and effective genetic nester. The key lesson: **simple variation and strong selection pressure beat elaborate mechanisms every time.**

## Evolution of the Algorithm

| Version | What Changed | 30-part Bed% | 30-part Time | Notes |
|---------|-------------|-------------|-------------|-------|
| v1 | Random init, basic fitness | BROKEN (oob) | 1.4s | Couldn't fit 25+ parts |
| v2 | 4 seed strategies, push-apart mutation, proximity fitness, adaptive mutation | 54.4% | 1.3s | Fixed convergence, much tighter |
| v3 | Order-aware crossover, local search on elites, position blending, progenitor breeding | 38.9% | 8.2s | Best density, but 2x slower |
| v4 | Progenitor-based init, extinction events | 38.6% | 8.2s | Negligible gain — overbred |
| **v5 (final)** | **Trimmed sterile complexity from v3/v4** | **38.9%** | **4.9s** | **Same quality, half the time** |

### What worked (keep forever)
- **Feasibility-first selection** (Deb 2000): Infeasible individuals always lose to feasible ones. Non-negotiable.
- **Bottom-left seed**: The single biggest convergence improvement. Gives the GA a valid starting point.
- **Four seed strategies** (center-spiral, line, grid, bottom-left): Different packing archetypes as starting points.
- **Proximity fitness term**: Rewards parts that are close but not colliding. Creates gradient toward tight packing.
- **Density fitness term**: Ratio of part area to bounding area. Pulls the bounding box tight.
- **Adaptive mutation**: Stagnation detection cranks mutation range up to 3x. Resets on improvement.
- **Push-apart mutation**: When selected, moves a part away from its nearest neighbor. Helps escape local optima.
- **Order-aware crossover**: Child inherits fitter parent's layout, randomly substitutes 30% from other parent.
- **Local refinement on elites**: Every 10 generations, nudge each part toward the arrangement centroid. Accept if fitness improves.
- **Early exit on convergence**: Stop when no improvement for 3× the stagnation window. Saves time.

### What didn't work (removed)
- **Progenitor-based initialization**: Breeding the whole population from archetype parents. No improvement over just seeding 4 slots.
- **Extinction events**: Replacing bottom 30% with randoms when deeply stagnant. Never triggered in practice — early exit fires first.
- **5-direction local search**: Testing cardinal directions plus centroid. Centroid-only is just as good and 5x cheaper.
- **Position blending crossover**: Weighted average of parent positions. The order-aware crossover does the real work.
- **High seed percentage** (10%+): 4 seeds is enough. More just wastes population slots that could be random exploration.

## Recommended Parameters

### Default / Balanced (good for 2-20 parts)
```cpp
NesterConfig cfg;
cfg.population_size   = 512;
cfg.max_generations   = 100;
cfg.timeout_seconds   = 20.0;
cfg.min_gap_mm        = 5.0;
cfg.adaptive_mutation = true;
cfg.stagnation_window = 15;
// Voxel resolution: 2.0mm
```

### Fast (button-press feel, up to 30 parts)
```cpp
cfg.population_size   = 256;
cfg.max_generations   = 50;
cfg.timeout_seconds   = 10.0;
// Voxel resolution: 2.0mm
```

### Quality (squeeze every mm, willing to wait)
```cpp
cfg.population_size   = 512;
cfg.max_generations   = 100;
cfg.timeout_seconds   = 30.0;
// Voxel resolution: 1.0mm
```

## Fitness Function Weights

The fitness function (for feasible individuals only) is:

```
fitness = compactness          × 1.0    (minimize bounding area)
        + density              × 0.5    (fill bounding area tightly)
        + proximity            × 0.3    (parts close but not touching)
        + height_centering     × 0.3    (tall parts toward center)
```

These weights were found empirically. Compactness dominates because it directly measures what we care about — how small an area the arrangement occupies. Density and proximity are supporting terms that create gradient for the GA to follow when compactness alone doesn't differentiate.

## Mutation Rates

```
Nudge:     60%   (small XY + rotation perturbation, scaled by stagnation)
Shuffle:   20%   (swap two parts' positions)
Push-apart:10%   (move away from nearest neighbor)
Wildcard:  10%   (completely random new position)
```

Nudge range: ±10mm XY, ±30° rotation (scaled up to 3× by adaptive mutation).

## Scaling Performance (2mm voxels, 256×256mm bed)

| Parts | Fast (256/50) | Balanced (512/100) | Bed Utilization |
|-------|--------------|-------------------|-----------------|
| 8 | 190ms | 861ms | 15.2% |
| 15 | 265ms | 1.1s | 20.2% |
| 20 | 538ms | 2.1s | 24.4% |
| 25 | 747ms | 2.9s | 28.7% |
| 30 | 1.3s | 4.9s | 38.9% |

Zero collisions across all configurations. All arrangements feasible and within bed bounds.

## GPU Compute Results

Proven on RTX 3080 Ti with OpenGL 4.3 compute shaders:

| Test | Result |
|------|--------|
| Compute shader compilation | Clean |
| Collision detection accuracy | 358/360 exact GPU-CPU match (2 float rounding at 45°/135°) |
| Batched pairwise (35 parts, 200 arrangements) | 10,419 arr/sec at 2mm |
| GPU vs CPU speedup | 1.9× at 2mm, 4.7× at 1mm, 4.4× at 0.5mm |
| Rotated collision (1° increments, 360°) | Validated against CPU reference |

GPU advantage grows with resolution because scattered voxel reads exceed CPU cache at finer grids.

## Architecture (what's in the box)

```
polite_voxelizer.hpp    — 380 lines, header-only
  Conservative-outward surface voxelizer
  Bit-packed 3D grids, triangle-AABB SAT intersection
  Yields every 1000 tris, hard memory cap, watchdog-abortable

snuggle_nester.hpp      — 699 lines, header-only
  Genetic algorithm with feasibility-first selection
  4 seed strategies, adaptive mutation, local refinement
  3DOF (XY + rotation) or 2DOF (XY only, lock_rotation=true)

gpu_poc.cpp             — GPU compute proof of concept
  Hidden-window GL 4.3 context, SSBO-based collision
  20,324 collision checks/sec

gpu_bench.cpp           — GPU vs CPU benchmark
  Batched compute shader, packed grid SSBOs
  Resolution scaling comparison
```

## What's Next

The algorithm works. The GPU pipeline works. What remains for production:

1. **Concavity detection**: The white paper's "secret sauce" — detecting cavities and scoring nesting depth. Currently Snuggle optimizes compactness but doesn't exploit concavities.
2. **Rotated voxel collision on CPU**: The CPU nester currently does XY-offset collision only. Adding rotation would match the GPU path.
3. **OrcaSlicer GUI integration**: A toggle or button to trigger Snuggle mode. The PoC hardcodes `use_snuggle=true`.
4. **Bed shape polygons**: Currently rectangular beds only. Orca supports arbitrary bed shapes.
5. **Sequential printing awareness**: Disable nesting when sequential mode is active.
