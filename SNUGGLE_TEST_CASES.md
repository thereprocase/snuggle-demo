# Snuggle Algorithm — Test Case Design Guide

## Test Goals

**Goal 1: No collisions.** No two parts may intersect at any point in 3D space. Voxel grids of all placed parts, when ANDed together pairwise, must produce zero overlapping solid voxels. Validated by exact mesh intersection check after voxel pass.

**Goal 2: Bed-lock invariant.** Every part's base (its lowest Z extent as oriented by the user) must remain at Z=0 (the bed surface) throughout the entire algorithm. No part may be lifted, tilted, or flipped. Only XY translation and Z-rotation are permitted.

**Goal 3: Bed containment.** Every voxel of every part must fall within the legal bed volume (including respect for keepout zones). No part may hang off any edge of the bed. Tested against a 256x256mm bed by default.

---

## Recommended Test Case Patterns

When setting up your own test cases, aim for these five categories. Use your own STL files — parts from real print projects work best.

### Pattern 1: "Many Small Parts" (6-10 parts, all under 50mm)

**What to pick:** Small cylindrical or boxy parts — caps, clips, spacers, connectors, small brackets. Parts you'd normally grid out on a plate.

**Why it tests well:** High part count with similar footprints. The baseline 2D arrange will grid them. Snuggle should match or slightly beat it. This is the "don't regress" sanity check.

**Expected behavior:** Tight cluster near bed center. Minimal nesting opportunity — these are small and simple. Mostly tests that the GA converges quickly and doesn't produce collisions.

---

### Pattern 2: "One Project, Mixed Sizes" (4-6 parts, 1 large + several small)

**What to pick:** Parts from a single real project — one large enclosure/housing/adapter plus its associated small hardware (spacers, plates, covers, brackets).

**Why it tests well:** The large part dominates the layout. Small parts should tuck into available space around (or eventually inside) it. Tests real-world "print all the parts for this thing" scenarios.

**Expected behavior:** Large part centered or placed first. Small parts packed efficiently around it. Mirror-image parts (left/right housings) should interlock if geometry allows.

---

### Pattern 3: "Complex Geometry" (4-8 parts with interesting shapes)

**What to pick:** Cylinders, rings, tubes, pistons, L-brackets, C-clamps — parts with open centers, concavities, or interlocking potential. This is where 3D nesting should shine.

**Why it tests well:** Rings have open centers. Cylinders have concave interiors. Pins and small rods can fit inside larger parts. This is the dream scenario for Snuggle — if a pin doesn't end up near or inside a ring, the algorithm is missing opportunities.

**Expected behavior:** Measurably more compact than 2D convex hull arrangement. Small parts placed adjacent to (or eventually inside) larger concavities.

---

### Pattern 4: "Flat/Planar Parts" (5-8 parts, mostly flat)

**What to pick:** Pegboard hooks, wall-mount brackets, tool holders, panels — parts that are essentially 2D with some Z features (hooks, tabs, mounting pegs on the back).

**Why it tests well:** These parts have limited nesting opportunity because they're mostly flat. The 3D advantage is small. Tests that Snuggle doesn't regress — it should be at least as good as 2D arrange on easy cases.

**Expected behavior:** Compact packing similar to 2D arrange. No dramatic improvement but no regression either.

---

### Pattern 5: "Extreme Size Mismatch" (3-4 parts, 2 very large + 2 very small)

**What to pick:** Two large shell halves (enclosure, case) plus two tiny parts (clips, buttons, caps). The large parts should be close to bed-filling individually.

**Why it tests well:** Tests bed utilization limits. If the large parts barely fit side-by-side, the small parts need to go *somewhere* — ideally tucked against or between them. Also tests graceful handling when parts genuinely don't fit (multi-plate spillover).

**Expected behavior:** Large parts placed, small parts packed in remaining space. If large parts overflow the bed, the algorithm should still produce a valid arrangement for what fits.

---

## Validation Checklist (per test case)

For each test case, validate:

- [ ] **Zero collisions:** Pairwise voxel AND produces no overlapping solid voxels
- [ ] **Bed-lock held:** All parts base Z == 0, no tilt/flip applied
- [ ] **Bed containment:** All part voxels within bed volume mask (256x256mm default)
- [ ] **Compactness measured:** Total bounding area of arrangement vs. sum of individual footprints
- [ ] **Comparison baseline:** Same parts arranged with current 2D auto-arrange for A/B comparison

## Test Parts Directory Structure

Organize your test STL files like this:

```
test-parts/
  small/           # Parts under 50mm in any dimension
    part1.stl
    part2.stl
    ...
  mixed/           # One large + several smaller parts from a project
    large_part.stl
    medium_part1.stl
    small_part1.stl
    ...
  complex/         # Cylinders, rings, brackets — nesting-friendly shapes
    cylinder.stl
    ring1.stl
    pin.stl
    ...
  large/           # Parts over 150mm — for extreme size mismatch tests
    big_part.stl
    ...
```

Set the `SNUGGLE_TEST_PARTS` environment variable to point at this directory, or pass it as the first command-line argument to any test executable.
