# Snuggle: GPU-Accelerated Genetic Nesting for 3D Print Bed Optimization

### A White Paper on Brute-Force Evolutionary Mesh Packing Using OpenGL Compute

**Author:** Claude (Opus 4.6) + Human Direction
**Date:** 2026-03-30
**Status:** Conceptual / Pre-Implementation
**Target Platform:** Orca Slicer (fork of PrusaSlicer/BambuStudio)

---

## Abstract

Current 3D print bed arrangement algorithms operate on 2D convex hull projections — flattened shadows of complex geometry. They solve a 2D bin-packing problem that ignores the third dimension entirely. This means a perfectly concave bowl sitting next to a sphere will never be told to *nest the sphere inside the bowl*, even though any human would do exactly that in two seconds.

This paper proposes a system that treats bed arrangement as a **3D-aware mesh nesting problem**, solved via **massively parallel genetic evolution on the GPU**, leveraging the OpenGL compute pipeline already present in Orca Slicer. The approach is intentionally wasteful by traditional standards — burning thousands of GPU cores on millions of candidate arrangements per frame — but lands on human-intuitive results in under one second of wall-clock time.

**Critical constraint:** All objects remain locked to the bed surface at all times, exactly as the user placed them orientation-wise. No flipping, no tumbling, no lifting off the bed. The only degrees of freedom are **XY translation and Z-axis rotation**. The nesting magic comes from the fact that we evaluate the *full 3D volume* of each part at its bed-locked Z height — recognizing that a bowl's interior cavity is available real estate for a smaller part sitting at the same bed level. The goal is a single "Snuggle" button that produces arrangements a human brain would be proud of.

---

## 1. The Problem With Current Nesting

### 1.1 The 2D Lie

Every major slicer — Cura, PrusaSlicer, Orca, SuperSlicer — reduces parts to their **2D convex hull** (or at best, a concave hull) projected onto the XY plane. The arrangement engine then solves a variant of the 2D irregular bin-packing problem using:

- NFP (No-Fit Polygon) computation
- Bottom-left heuristics
- Simulated annealing (PrusaSlicer's `arrange()`)
- Or simple grid placement

**What this misses:**

| Scenario | Human Would... | Current Slicer Does... |
|----------|---------------|----------------------|
| Bowl + small cube | Put cube inside bowl | Places side-by-side, wastes 40% bed |
| Two L-brackets | Interlock them like puzzle pieces | Bounding-box gap between them |
| Cone + ring | Slide ring over cone base | Treats as two separate circles |
| Tall thin part + short wide part | Center tall part, surround with short | Random scatter |
| Organic sculpt with deep cavity | Nest small parts in the cavity | Ignores cavity entirely |

### 1.2 Why Nobody Has Fixed This

The real reason is computational: true 3D nesting with collision detection on arbitrary triangle meshes is an **NP-hard** problem. Even constrained to 3DOF (XY translation + Z-rotation, with all parts bed-locked at their user-defined orientation), the search space for N objects is astronomically large. Classical CPU approaches choke.

But GPUs don't care about astronomically large. GPUs *eat* astronomically large for breakfast.

---

## 2. Core Insight: GPUs Are Embarrassingly Good At This

### 2.1 The Financial Industry Precedent

Global financial institutions run **Monte Carlo simulations** across millions of scenarios per second on GPU clusters. They evaluate portfolio risk by brute-forcing millions of random market trajectories, scoring each one, and keeping the best. This is computationally "wasteful" — most trajectories are garbage — but the sheer throughput means the answer converges faster than any clever algorithm could.

**We propose the same philosophy for mesh nesting.**

Instead of being clever about placement, we:
1. Generate millions of random arrangements per second
2. Score each arrangement on the GPU
3. Evolve the best ones using genetic operators
4. Converge on human-intuitive results through sheer parallel brute force

### 2.2 Why OpenGL Compute (Not CUDA/Vulkan)

Orca Slicer already initializes an OpenGL context for its 3D viewport. This means:

- **Zero additional dependencies** — no CUDA toolkit, no Vulkan loader
- **Cross-platform by default** — works on AMD, Intel, NVIDIA, Apple (via MoltenVK/Metal translation for compute shaders via SPIRV-Cross, or GLSL ES on mobile)
- **OpenGL 4.3+ Compute Shaders** provide general-purpose GPU compute with shared memory, atomic operations, and indirect dispatch
- The existing mesh data (VBOs, index buffers) is **already on the GPU** — no upload cost
- Orca's `GLCanvas3D` and `GLVolume` infrastructure gives us mesh transforms for free

The key realization: **the meshes are already sitting in GPU memory as renderable geometry.** We just need to *think about them differently* — not as things to draw, but as things to collide, nest, and score.

---

## 3. Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                   [ SNUGGLE PARTS ]                          │
│                   (Single UI Action)                         │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│              PHASE 1: MESH ANALYSIS (GPU)                   │
│  ┌─────────────┐ ┌──────────────┐ ┌──────────────────────┐ │
│  │ Voxelize    │ │ Concavity    │ │ Height Profile       │ │
│  │ all meshes  │ │ Detection    │ │ Analysis             │ │
│  │ (compute)   │ │ (ray-march)  │ │ (column max-height)  │ │
│  └─────────────┘ └──────────────┘ └──────────────────────┘ │
│                         │                                    │
│                    Mesh Descriptors                          │
│            (voxel grid + concavity map + profile)            │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│           PHASE 2: GENETIC EVOLUTION (GPU)                  │
│                                                             │
│  Population: 4096 candidate arrangements                    │
│  Each arrangement: [x, y, z_rot] per object (bed-locked)   │
│                                                             │
│  ┌──────────┐    ┌──────────┐    ┌──────────┐              │
│  │ Evaluate │───▶│ Select   │───▶│ Breed +  │──┐           │
│  │ Fitness  │    │ Top 25%  │    │ Mutate   │  │           │
│  └──────────┘    └──────────┘    └──────────┘  │           │
│       ▲                                         │           │
│       └─────────────────────────────────────────┘           │
│                   (repeat ~200 generations)                  │
│                                                             │
│  All 4096 arrangements evaluated IN PARALLEL                │
│  Each evaluation: full 3D collision + nesting score         │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│          PHASE 3: REFINEMENT (GPU + CPU)                    │
│  ┌───────────────┐ ┌────────────────┐ ┌──────────────────┐ │
│  │ Local search  │ │ Verify bed-    │ │ Validate with    │ │
│  │ on top-8      │ │ lock + gap     │ │ exact mesh       │ │
│  │ candidates    │ │ enforcement    │ │ intersection     │ │
│  └───────────────┘ └────────────────┘ └──────────────────┘ │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
              Final Arrangement Applied
              to Orca Slicer Bed View
```

---

## 4. Phase 1: Mesh Analysis — Teaching the GPU to See Shape

### 4.0 Bed Volume Voxelization — The Arena

Before we touch a single part, we voxelize the **bed itself** into a 3D binary mask representing the legal placement volume. This is the arena everything must stay inside of.

Print beds are not always simple rectangles. Real-world beds include:

- **Rectangular beds** (most common, e.g., 256×256mm Bambu X1C)
- **Non-square rectangles** (e.g., 350×220mm Ender 5 Plus)
- **Corner keepout zones** (bed clips, purge buckets, camera mounts, ABL sensor clearance)
- **Irregular shapes** (delta printers have circular beds, some beds have notched corners for wiring)
- **User-defined exclusion zones** (Orca already supports these — areas the user marks as off-limits)

The bed volume is voxelized once at the same resolution as the part grids, producing a **bed mask**: a 3D voxel grid where `1` = legal placement zone, `0` = out of bounds or keepout.

```c
struct BedVolume {
    uint* voxelMask;           // 3D bit-packed grid: 1 = legal, 0 = forbidden
    ivec3 dims;                // Grid dimensions covering full bed + max part height
    vec3  physicalSize;        // Real-world mm dimensions
    float voxelSize;           // mm per voxel (e.g., 1.0mm at 256^2 bed = 256x256 grid)
};
```

**How it's built:**

1. Start with the printer's bed shape polygon (already defined in Orca's printer profile as `bed_shape` — a list of XY points defining the bed outline)
2. Extrude upward to the printer's max Z height to create the legal 3D volume
3. Subtract any keepout zones (also already defined in Orca's printer profile, or user-configured exclusion areas)
4. Voxelize the resulting volume via a compute shader — rasterize the bed polygon per Z-slice (it's the same polygon at every height, so really just a 2D rasterization stamped up the Z column)

**CRITICAL: Conservative Rounding Direction**

Voxelization is lossy — any triangle that partially occupies a voxel forces a binary decision: mark it solid or empty. The rounding direction is **opposite for bed vs. parts**, and this is what guarantees zero real-world clashes at any voxel resolution:

| What | Rounding | Effect | Why |
|------|----------|--------|-----|
| **Bed volume** | **Rounds inward** (shrinks) | Bed boundary voxels that are only partially inside the bed polygon are marked `0` (forbidden) | The legal arena is up to one voxel smaller than reality on every edge — parts can never be placed where they'd hang off the real bed |
| **Part meshes** | **Rounds outward** (expands) | Surface voxels that are only partially occupied by the mesh are marked `1` (solid) | Every part's voxel footprint is up to one voxel larger than reality in every direction — collision detection is conservative, parts are kept slightly farther apart than strictly necessary |

The combined effect: at the boundary, the bed is one voxel smaller and the parts are one voxel bigger. This means **any arrangement that passes voxel-based validation is guaranteed to be physically valid** in the real mesh world. We never have to worry about sub-voxel edge cases producing clashes. The exact-mesh collision check in Phase 3 becomes a formality, not a safety net.

At 1mm voxel resolution, this costs us at most 2mm of usable bed space around the perimeter and 2mm of clearance between parts — which is actually within the typical minimum gap setting anyway. Free safety.

```glsl
// bed_voxelize.comp
layout(local_size_x = 16, local_size_y = 16) in;

void main() {
    ivec2 xy = ivec2(gl_GlobalInvocationID.xy);
    vec2 worldPos = voxelToWorld(xy);

    // Point-in-polygon test against bed shape
    bool inBed = pointInPolygon(worldPos, bedShapeVertices, numBedVerts);

    // Subtract keepout zones
    for (int k = 0; k < numKeepouts; k++) {
        if (pointInPolygon(worldPos, keepoutVertices[k], keepoutVertCounts[k])) {
            inBed = false;
        }
    }

    // Stamp the result up the full Z column (bed shape is constant in Z)
    for (int z = 0; z < gridDimsZ; z++) {
        if (inBed) {
            atomicOr(bedMask[ivec3(xy, z)], 1);
        }
    }
}
```

**Cost:** Trivial — it's a 2D polygon rasterization. Sub-millisecond. Done once per printer profile change (cacheable).

The bed mask becomes a **hard constraint** used everywhere:
- **Fitness evaluation:** Any part voxel landing on a `0` in the bed mask = instant penalty (same weight as collision)
- **Mutation:** Random positions are generated only within the bed mask's bounding box (no wasted effort placing parts in outer space)
- **Initialization:** Population seeds only place objects within the bed mask's convex hull

This means Snuggle automatically respects circular delta beds, notched corners, clip keepouts, purge areas — anything. If Orca knows about it, Snuggle knows about it.

### 4.1 Part Voxelization

Each mesh is voxelized into a 3D binary grid at a resolution appropriate for nesting (typically 64x64x64 per object, or adaptive based on object size). This is a well-solved GPU problem:

**Approach: Conservative-Outward Surface Voxelization via Compute Shader**

Parts round **outward** — any voxel that is even partially occupied by the mesh surface is marked solid. This is the opposite of the bed volume's inward rounding, and it's what makes the whole system safe.

```
For each triangle in mesh:
    Compute axis-aligned bounding box of triangle
    For each voxel in AABB:
        Test triangle-voxel intersection (Schwarz-Seidel method)
        If intersecting: atomicOr(voxelGrid[voxelIndex], 1)
        // Note: partial intersection = SOLID (rounds outward)
```

Then flood-fill from outside to mark interior vs. exterior (also parallelizable on GPU using jump-flooding). Interior voxels are also marked solid — the entire enclosed volume is filled, not just the skin.

**Cost:** For a typical print plate of 10 objects at 64^3 resolution each = ~2.6M voxels total. At ~1 billion voxel-tests/second on a mid-range GPU, this completes in under 3ms.

The voxel grid gives us O(1) collision detection between any two objects at any relative position — just AND their translated grids and check for non-zero. And because every part is up to one voxel *fatter* than reality, any collision-free voxel arrangement is guaranteed collision-free in the real mesh world.

### 4.2 Concavity Detection

For each mesh, we compute a **concavity map** — a per-voxel score indicating how "inside a cavity" each empty voxel near the surface is. This is the secret sauce for nesting.

**Approach: Ambient Occlusion in Reverse**

Fire rays outward from each empty voxel near the surface. Count how many rays hit the same mesh before escaping. High hit-count = deep inside a concavity.

```glsl
// Compute shader pseudocode
layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

void main() {
    ivec3 voxel = ivec3(gl_GlobalInvocationID);
    if (voxelGrid[voxel] != EMPTY) return;
    if (!isNearSurface(voxel)) return;

    float concavity = 0.0;
    for (int ray = 0; ray < NUM_RAYS; ray++) {
        vec3 dir = fibonacciSphereDirection(ray, NUM_RAYS);
        // March outward, count self-intersections
        int hits = marchAndCount(voxel, dir, SAME_MESH);
        concavity += float(hits > 0) / float(NUM_RAYS);
    }
    concavityMap[voxel] = concavity;
}
```

Voxels with high concavity scores are **invitation zones** — places where other objects could nest.

### 4.3 Height Profile Analysis

For each mesh, compute its **height column map**: a 2D grid where each cell stores the min and max Z extent of the object in that XY column. This enables:

- Quick "tallness" scoring per region
- Stack-ability assessment (flat top = stackable, peaked top = centerpiece)
- Pyramid-packing heuristic (tall things center, short things surround)

```glsl
// For each XY column, scan Z and record min/max occupied Z
layout(local_size_x = 16, local_size_y = 16) in;

void main() {
    ivec2 col = ivec2(gl_GlobalInvocationID.xy);
    int minZ = MAX_Z, maxZ = 0;
    for (int z = 0; z < GRID_Z; z++) {
        if (voxelGrid[ivec3(col, z)] == SOLID) {
            minZ = min(minZ, z);
            maxZ = max(maxZ, z);
        }
    }
    heightMap[col] = ivec2(minZ, maxZ);
}
```

### 4.4 Mesh Descriptors

The output of Phase 1 is a **Mesh Descriptor** for each object, plus the shared **Bed Volume** that constrains all of them:

```c
struct MeshDescriptor {
    uint  voxelGridOffset;     // Offset into global voxel buffer
    ivec3 voxelDims;           // Dimensions of this object's voxel grid
    uint  concavityMapOffset;  // Offset into concavity buffer
    uint  heightMapOffset;     // Offset into height profile buffer
    float maxHeight;           // Tallest point (from bed surface up)
    float volume;              // Total solid voxels (for density packing)
    float baseZ;               // Always 0.0 — bed-locked invariant
    mat3  principalAxes;       // For smart rotation initialization
};

// Shared across all evaluations — the arena
struct BedVolume {
    uint* voxelMask;           // 3D bit-packed: 1 = legal, 0 = keepout/OOB
    ivec3 dims;                // Grid dimensions
    vec3  physicalSize;        // Real-world mm
    float voxelSize;           // mm per voxel
};
```

---

## 5. Phase 2: Genetic Evolution — Survival of the Snuggliest

### 5.1 Genome Encoding

Each **individual** in the population encodes a complete bed arrangement:

```c
struct Arrangement {
    // Per-object placement (3 DOF each — bed-locked)
    float x[MAX_OBJECTS];       // X position on bed
    float y[MAX_OBJECTS];       // Y position on bed
    float zRot[MAX_OBJECTS];    // Rotation about Z axis (0-360)
    // Z position is FIXED: each object's base sits on bed (Z=0)
    // X/Y tilt is FIXED: user's original orientation preserved
    // No flipping: the user placed it that way for a reason
};
```

**Population size:** 4096 arrangements (one per compute workgroup, or distributed across workgroups for larger evaluations).

**Why exactly 3DOF?**

This is a deliberate, non-negotiable design decision rooted in how 3D printing actually works:

- **XY translation:** Obviously needed — slide parts around the bed.
- **Z rotation:** Spin parts on the bed surface to find interlocking orientations. A U-bracket rotated 180° nests perfectly with another U-bracket. This is the key rotational DOF.
- **Z position: LOCKED at bed level.** Every object's bottom face (as the user oriented it) stays welded to Z=0. Period. The nesting opportunity exists *at this single Z plane* — where the 3D profiles of bed-touching parts overlap in available airspace above neighboring parts. A bowl's cavity is available because the bowl is sitting on the bed and the cavity opens upward. A small cube can sit *beside* the bowl at the same Z=0, with its body occupying the airspace inside the bowl's concavity. They share the bed plane. No stacking, no hovering.
- **X/Y tilt: LOCKED to user orientation.** The user placed each part in a printable orientation. We don't second-guess that. Overhangs, bridging, support strategy — those are the user's call. We just pack them tighter.
- **No Z-flip.** Flipping a part upside-down changes its printability characteristics entirely. That's an orientation decision, not an arrangement decision. Out of scope.

### 5.2 Fitness Function — The Heart of the Beast

Each arrangement is scored by a **composite fitness function** evaluated entirely on the GPU. This is where the magic happens.

```
FITNESS = w1 * COMPACTNESS
        + w2 * NESTING_DEPTH
        + w3 * HEIGHT_CENTERING
        + w4 * COLLISION_PENALTY
        + w5 * BED_BOUNDS_PENALTY
        + w6 * PRINT_QUALITY_SCORE
```

Note: No stability score needed — all objects are bed-locked by definition. They can't tip over because we never lift them off the bed.

#### 5.2.1 Compactness Score

Measures how tightly the arrangement fits within a minimal bounding rectangle on the XY plane. Computed as:

```
COMPACTNESS = 1.0 - (bounding_area / bed_area)
```

Tighter = better. But this alone produces the same results as 2D packing — the other terms are what make this revolutionary.

#### 5.2.2 Nesting Depth Score (THE SECRET SAUCE)

This is what no current slicer does. All objects sit on the bed at Z=0. But their 3D profiles extend *upward* — and those profiles have concavities, overhangs, and airspace. A bowl sitting on the bed has an open cavity above it. A small cube sitting on the bed *next to* (partially inside the XY footprint of) the bowl can occupy that cavity — as long as the cube's voxels don't intersect the bowl's voxels at any Z height.

The nesting score measures how effectively objects use each other's overhead airspace:

```glsl
float nestingScore = 0.0;
for each pair (i, j):
    // Both objects at Z=0 (bed-locked), only XY offset + Z-rotation differs
    // Transform object j's voxels into object i's local space
    for each solid voxel v in object j (at its bed-locked position):
        // Check if v falls within object i's concavity zone
        // (empty space that is "inside" object i's profile)
        if isInConcavityZone(v, concavityMap_i):
            nestingScore += concavityMap_i[v];
```

This is **not** stacking objects on top of each other. It's sliding them *beside* each other on the bed so their vertical profiles interlock. Think of it like a city skyline — a short building can sit in the shadow of a tall building's L-shaped footprint, and its roof occupies space that the tall building's concavity made available.

Examples at bed level:
- **Bowl + small cube:** Both on bed. Cube's XY position slides into the bowl's footprint. The cube is shorter than the bowl's walls, so it physically fits in the cavity. No collision.
- **Two L-brackets:** Both on bed. Rotated so they interlock like puzzle pieces, each one's arm reaching into the other's negative space.
- **Enclosure with a missing panel + small flat part:** The flat part slides into the XY region under the enclosure's overhang.

**GPU optimization:** This is a 3D overlap kernel — essentially the same operation as volumetric convolution, which GPUs do trillions of per second for neural networks. We're just repurposing that muscle for geometry.

#### 5.2.3 Height Centering Score

Penalizes tall objects at the edges, rewards them in the center. This produces the natural "pyramid" arrangement humans prefer:

```
HEIGHT_CENTERING = sum_over_objects(
    height[i] * (1.0 - distance_from_bed_center(x[i], y[i]) / max_distance)
)
```

Tall things gravitate to the center. Short things fill in around the edges. This also happens to be mechanically better — tall parts in the center are less likely to be knocked over by the print head's acceleration.

#### 5.2.4 Collision Penalty

Hard constraint. Any voxel overlap between solid regions of different objects = massive penalty. This ensures physically valid arrangements.

Evaluated via voxel grid AND operations — blisteringly fast on GPU:

```glsl
uint collision = 0;
for each voxel in combined grid:
    uint occupied = 0;
    for each object i:
        occupied += voxelGrid_i[transformedVoxel(voxel, arrangement, i)];
    if (occupied > 1) collision++;
return -LARGE_NUMBER * collision;
```

#### 5.2.5 Bed Volume Penalty

Any part voxel that lands outside the bed mask = penalty. This is **not** a simple AABB check — it's a full voxel-to-voxel lookup against the bed volume mask built in Phase 1 (Section 4.0). This means irregular bed shapes, corner keepouts, clip zones, purge areas, and user-defined exclusion regions are all automatically enforced.

```glsl
uint outOfBounds = 0;
for each object i:
    for each solid voxel v in object i (at its arranged position):
        ivec3 bedCoord = worldToBedGrid(v);
        if (bedMask[bedCoord] == 0) outOfBounds++;
return -LARGE_NUMBER * outOfBounds;
```

This penalty has the same weight as collision — parts outside the legal bed volume are just as invalid as parts intersecting each other. The bed mask is the arena. Nothing escapes the arena.

#### 5.2.6 Print Quality Score

Optional term that penalizes arrangements known to cause print quality issues:
- Objects too close together (poor cooling airflow)
- Tall objects upwind of short objects (heat radiation)
- Objects with overhangs facing each other (support interference)

This term can be disabled for "pack as tight as possible" mode.

### 5.3 Genetic Operators

All executed in compute shaders:

#### Selection: Tournament Selection
- Pick 4 random individuals from the population
- The fittest two become parents
- Entirely parallel — each workgroup runs its own tournament

#### Crossover: Uniform Crossover with Object Grouping
- For each object, randomly inherit placement from parent A or parent B
- But with a twist: objects that were *nested together* in a parent tend to be inherited as a group (preserving discovered nesting relationships)

```glsl
void crossover(inout Arrangement child, Arrangement parentA, Arrangement parentB) {
    // Build nesting adjacency from parentA
    for (int i = 0; i < numObjects; i++) {
        if (isNestedWith(i, lastInherited, parentA) && random() < 0.8) {
            // Inherit from same parent to preserve nesting group
            child.x[i] = parentA.x[i];
            // ...
        } else {
            // Random parent
            bool fromA = random() < 0.5;
            child.x[i] = fromA ? parentA.x[i] : parentB.x[i];
            // ...
        }
    }
}
```

#### Mutation: Multi-Scale Perturbation
Three mutation modes, randomly selected per-object per-generation:

1. **Nudge** (70%): Small XY displacement (±5mm), small rotation (±15°). Fine-tuning.
2. **Shuffle** (20%): Swap positions of two random objects. Explores macro-layout.
3. **Wildcard** (10%): Completely random new position/rotation for one object. Escape local optima.

#### Elitism
- Top 2% of population survives unchanged to next generation
- Prevents regression — best arrangements are never lost

### 5.4 Generation Loop

```
for gen in 0..200:
    // All 4096 arrangements evaluated in parallel
    evaluate_fitness<<<4096>>>(population, meshDescriptors, fitnessScores)

    // Sort by fitness (GPU radix sort)
    sort<<<>>>(population, fitnessScores)

    // Breed next generation
    breed<<<4096>>>(population, fitnessScores, newPopulation)

    // Adaptive mutation rate
    if (gen > 100 && improvement_rate < threshold):
        increase_mutation_rate()  // shake things up

    swap(population, newPopulation)
```

**Timing estimate:**
- 4096 arrangements × ~10 objects × 64^3 voxel collision checks = ~10 billion operations per generation
- Modern GPU at ~10 TFLOPS = ~1ms per generation
- 200 generations = ~200ms
- With overhead (memory barriers, sort, etc.): **~400-600ms total**

We're under one second. The button feels instant.

---

## 6. Phase 3: Refinement — Polish the Diamond

### 6.1 Local Search on Top Candidates

Take the top 8 arrangements from the genetic phase. Run a fine-grained local optimization:

- Reduce voxel resolution (go from 64^3 to 128^3 or use actual mesh)
- Gradient-free optimization (Nelder-Mead simplex) on each object's position
- ~50 iterations, still on GPU via compute shader
- Finds the last few millimeters of nesting improvement

### 6.2 Bed-Lock Verification

Objects are bed-locked *throughout the entire algorithm* — this is not a post-processing step. Z=0 for each object's base is an invariant, never a variable. This refinement step simply verifies the invariant held and enforces the minimum gap between objects:

```
For each object:
    Assert: object base Z == 0 (bed surface)
    For each neighbor within gap threshold:
        If gap < minimum_gap: nudge apart along separation vector
```

### 6.3 Exact Collision Validation

The voxel-based collision from Phase 2 is approximate. Before finalizing, validate with exact triangle-mesh intersection tests (GJK/EPA or similar). If collision is detected, apply minimum separation vector.

This step runs on CPU using the existing Orca Slicer collision detection — it only needs to check the single winning arrangement, so it's fast.

### 6.4 Final Output

Apply the winning arrangement transforms to the actual mesh objects in Orca's scene graph. The user sees their objects smoothly animate into position on the virtual bed.

---

## 7. Leveraging Orca Slicer's Existing OpenGL Infrastructure

### 7.1 What Already Exists

Orca Slicer (and its PrusaSlicer lineage) already has:

| Component | Location | How We Use It |
|-----------|----------|---------------|
| `GLCanvas3D` | `src/slic3r/GUI/GLCanvas3D.cpp` | Our compute shaders run in this context |
| `GLVolume` / `GLVolumeCollection` | `src/slic3r/GUI/3DScene.cpp` | Mesh data already in VBOs on GPU |
| `TriangleMesh` | `src/libslic3r/TriangleMesh.cpp` | Source geometry for voxelization |
| `arrange()` | `src/libslic3r/MinAreaBoundingBox.cpp` | We *replace* this with our system |
| `Model` / `ModelObject` | `src/libslic3r/Model.cpp` | Transform outputs applied here |
| OpenGL context init | `src/slic3r/GUI/OpenGLManager.cpp` | Ensure GL 4.3+ for compute shader support |

### 7.2 Integration Points

**Entry point:** Add a "Snuggle" button to the toolbar alongside the existing "Auto Arrange" button. Wire it to our `SnuggleNester::run()` method.

**Mesh upload:** Meshes are already in `GLVolume::indexed_vertex_array`. We can either:
- Read vertex data back and re-upload as SSBO (Shader Storage Buffer Object) for compute
- Or directly bind the existing VBO as SSBO (requires no copy — just a `glBindBufferBase` call)

**Result application:** Write final transforms back to `ModelInstance::set_offset()` and `ModelInstance::set_rotation()`, then trigger a canvas refresh.

### 7.3 Shader Pipeline

```
┌─────────────────────┐
│ voxelize.comp       │  Compute shader: mesh → voxel grid
├─────────────────────┤
│ concavity.comp      │  Compute shader: voxel grid → concavity map
├─────────────────────┤
│ heightmap.comp      │  Compute shader: voxel grid → height columns
├─────────────────────┤
│ fitness.comp        │  Compute shader: arrangement → fitness score
├─────────────────────┤
│ selection.comp      │  Compute shader: tournament selection
├─────────────────────┤
│ crossover.comp      │  Compute shader: breed two parents
├─────────────────────┤
│ mutation.comp       │  Compute shader: perturb offspring
├─────────────────────┤
│ sort.comp           │  Compute shader: radix sort by fitness
├─────────────────────┤
│ refine.comp         │  Compute shader: local optimization
└─────────────────────┘
```

All shaders operate on SSBOs containing the population, voxel grids, and fitness scores. Communication between phases uses `glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT)`.

---

## 8. Memory Budget

For a typical print job (10 objects, 64^3 voxel resolution):

| Buffer | Size | Notes |
|--------|------|-------|
| Bed volume mask | 256 × 256 × 384 / 8 bytes = 3 MB | Bit-packed, includes keepouts |
| Voxel grids (10 objects) | 10 × 64^3 / 8 bytes = 320 KB | Bit-packed |
| Concavity maps (10 objects) | 10 × 64^3 × 4 bytes = 10 MB | Float per voxel |
| Height maps (10 objects) | 10 × 64^2 × 8 bytes = 320 KB | min/max per column |
| Population (4096 arrangements × 10 objects × 3 floats) | 480 KB | Compact (3DOF: x, y, zRot) |
| Fitness scores (4096 × float) | 16 KB | Trivial |
| **Total** | **~15 MB** | Fits in any GPU |

Even at 128^3 resolution with 50 objects, we're under 500 MB — well within modern GPU VRAM.

---

## 9. Handling Edge Cases and Constraints

### 9.1 User Constraints

Users should be able to:
- **Lock objects** in place (excluded from arrangement, stays where the user put it)
- **Group objects** (must stay together as a unit, moved/rotated as one rigid body)
- **Set minimum gap** (configurable clearance between objects, default: 3mm)
- **Disable nesting** per-object (for objects that can't have things tucked into their footprint, e.g., vase mode prints)

Note: All objects are always bed-locked at their user-defined orientation. There is no "pin to bed" toggle — bed-lock is the only mode.

### 9.2 Non-Manifold Meshes

Real-world STL files are often non-manifold garbage. The voxelization approach is inherently robust to this — even broken meshes produce usable voxel grids. The flood-fill interior detection may fail for meshes with holes, but this just means we treat them as surface-only (conservative, safe).

### 9.3 Very Different Scales

If the plate has both a 2mm screw and a 200mm enclosure, adaptive voxel resolution is needed:
- Small objects get finer grids (higher resolution per mm)
- Large objects get coarser grids
- Collision detection handles mixed resolutions by up-sampling the coarser grid at query time

### 9.4 Too Many Objects

If N > 50, the genetic search space explodes. Mitigation:
- **Hierarchical nesting:** First cluster objects by size similarity, nest within clusters, then arrange clusters
- **Greedy seeding:** Use a fast greedy placement as the initial seed for 10% of the population, random for the rest
- **Island model:** Split population into sub-populations that evolve independently and exchange best individuals every 20 generations

---

## 10. Stretch Goals / Future Insanity

### 10.1 True Z-Stacking (Breaks Bed-Lock — Advanced Mode Only)

A future "aggressive" mode could allow objects to be placed *on top of* other objects. This **deliberately violates the bed-lock constraint** and should be a separate, clearly-labeled mode. It requires:
- Adding Z-translation as a 4th DOF
- Stability analysis (will the top object actually stay put during printing?)
- Support material awareness (stacked objects need support between them)
- Sequential printing mode compatibility
- User opt-in — never default behavior

### 10.2 Orientation Optimization (Breaks Orientation-Lock — Advanced Mode Only)

A separate pre-processing step could suggest optimal orientations *before* the nesting algorithm runs. This is a different problem: "how should I orient these parts?" vs. "how should I arrange these already-oriented parts?" The fitness function would gain a term for support material volume. But this is explicitly NOT part of the core nesting algorithm — the user's orientation choices are sacred.

### 10.3 Multi-Plate Optimization

When objects don't fit on one plate, automatically split across N plates while minimizing total plate count and balancing print times.

### 10.4 Machine Learning Warm Start

Train a lightweight neural network on successful arrangements to predict good initial placements. Use these as seeds for the genetic algorithm instead of random initialization. The GA still does the heavy lifting, but it starts from a much better position.

### 10.5 Real-Time Snuggle Preview

As the genetic algorithm evolves, render the current best arrangement in real-time. The user watches parts slide and snuggle into each other's curves like a time-lapse of natural selection. Pure dopamine.

---

## 11. Performance Targets

| Metric | Target | Rationale |
|--------|--------|-----------|
| Total wall-clock time | < 1 second | Must feel like a button press, not a computation |
| Objects supported | 2-50 | Covers 99% of real print jobs |
| GPU VRAM usage | < 500 MB | Works on integrated GPUs |
| Minimum GPU | OpenGL 4.3 | GTX 600-series / HD 7000-series (2012-era) |
| Improvement over 2D arrange | > 20% bed utilization | Measured on concave-heavy test set |
| Collision accuracy | Zero interpenetration | Validated by exact mesh intersection |

---

## 12. Why This Will Work

1. **The math is proven.** Genetic algorithms on GPUs have solved harder packing problems in logistics, CNC cutting, and semiconductor layout. 3D printing beds are comparatively small search spaces.

2. **The hardware is here.** Even a $100 GPU from 2018 has enough compute for this. The Nintendo Switch's Tegra X1 could probably do it.

3. **The software foundation exists.** Orca Slicer's OpenGL context, mesh handling, and arrangement infrastructure give us 80% of the scaffolding. We're adding a better engine to an existing car.

4. **Humans can verify instantly.** Unlike optimization problems where the answer is opaque, users can *look* at the arrangement and immediately judge "yeah, that's good" or "nah, try again." This makes iterative improvement natural.

5. **"Good enough" is good enough.** We don't need a mathematically optimal solution. We need an arrangement that a human looks at and says "I wouldn't have done it differently." The genetic approach naturally produces these human-satisfying results because the fitness function encodes human preferences.

---

## 13. Implementation Roadmap

### Sprint 1: Proof of Concept (1-2 weeks)
- Standalone OpenGL compute application (not integrated into Orca yet)
- Voxelization of STL files
- Basic genetic arrangement with collision-only fitness
- Render results in a simple viewer
- **Success metric:** Arranges 5 simple objects without collision

### Sprint 2: Nesting Intelligence (1-2 weeks)
- Concavity detection
- Nesting depth scoring in fitness function
- Height centering
- **Success metric:** Bowl + sphere test case → sphere snuggles inside bowl

### Sprint 3: Orca Integration (2-3 weeks)
- Hook into Orca's OpenGL context
- Read mesh data from existing GLVolumes
- Apply results to ModelInstance transforms
- Add `[Snuggle Parts]` button to UI
- **Success metric:** Works inside Orca on real print jobs

### Sprint 4: Polish and Edge Cases (1-2 weeks)
- User constraints (lock, pin, group, gap)
- Non-manifold mesh handling
- Adaptive resolution for mixed scales
- Performance optimization and profiling
- **Success metric:** Handles 20+ objects in < 1 second

---

## 14. Conclusion

The 3D printing community has accepted 2D arrangement as "good enough" for a decade. It isn't. Every user who has manually nested a small part inside a larger one's cavity knows this. Every user who has carefully centered their tallest print knows this. The slicer should know it too.

By repurposing the GPU as a massively parallel evolution engine — burning compute cycles with the reckless abundance of a Wall Street quant server — we can solve in under one second what would take a human minutes of careful manual placement. And we can do it on the *actual 3D geometry*, not on flattened shadows.

The meshes are already on the GPU. The GPU is already in the computer. The compute shaders are already in OpenGL. All that's missing is the audacity to throw a few billion calculations at a problem that everybody assumed was too hard.

It isn't too hard. It's too fun.

---

*"The best way to solve an NP-hard problem is to not be clever about it — just be fast."*

---

## Appendix A: Key References

- **GPU Voxelization:** Schwarz & Seidel, "Fast Parallel Surface and Solid Voxelization on GPUs" (2010)
- **Genetic Bin Packing:** Hopper & Turton, "An Empirical Investigation of Meta-heuristic and Heuristic Algorithms for a 2D Packing Problem" (2001)
- **NFP Computing on GPU:** Burke et al., "A New Bottom-Left-Fill Heuristic Algorithm for the Two-Dimensional Irregular Packing Problem" (2006)
- **OpenGL Compute Shaders:** Sellers et al., "OpenGL SuperBible" (7th Ed, Chapter 12)
- **Monte Carlo on GPU:** Joshi, "GPU Monte Carlo Methods for Financial Mathematics" (2010)
- **Orca Slicer Source:** github.com/SoftFever/OrcaSlicer

## Appendix B: Glossary

| Term | Definition |
|------|-----------|
| **Concavity Map** | Per-voxel score indicating depth inside a surface cavity |
| **DOF** | Degrees of Freedom — axes along which an object can move/rotate |
| **Fitness** | Score measuring how "good" an arrangement is |
| **GJK/EPA** | Gilbert-Johnson-Keerthi / Expanding Polytope Algorithm — exact collision detection |
| **NFP** | No-Fit Polygon — 2D representation of where one shape cannot be placed relative to another |
| **SSBO** | Shader Storage Buffer Object — large read/write GPU buffer accessible from compute shaders |
| **Voxel** | Volumetric pixel — a tiny cube in a 3D grid representing occupied/empty space |
| **Bed-Lock** | The invariant that every object's base remains at Z=0 in its user-defined orientation throughout the algorithm |
