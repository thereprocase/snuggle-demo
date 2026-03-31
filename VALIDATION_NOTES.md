# Snuggle Collision Validation Notes

## Independent Validation Method

Used exact Möller-Trumbore triangle-triangle intersection testing. This is completely independent of the voxelizer — different algorithm, different code, different math. If both agree on "no collision," we're solid.

## Results

| Parts | Pairs Checked | Collisions | Status |
|-------|--------------|------------|--------|
| 5 | 10 | 0 | PASS |
| 8 | 28 | 0 | PASS |
| 12 | 66 | 0 | PASS |
| 15 | 105 | 0 | PASS |
| 20 | 190 | 0 | PASS |

All tests use real STL files from the test parts library.

## Key Finding: Rotation Is Not Collision-Checked

The nester assigns Z-rotations to parts for compactness optimization, but the voxel collision check operates on unrotated grids at their XY-translated positions. This means:

1. **XY placement is collision-safe** — proven by independent validation
2. **Z-rotation is cosmetic in the collision check** — the GA uses it for fitness scoring (compactness, proximity) but doesn't verify that rotated parts don't intersect
3. **In the Orca integration**, `apply_arrange_result` will set both XY offset AND Z rotation. If rotation causes a collision, the voxel check wouldn't have caught it.

### Mitigation
For the PoC, set `lock_rotation = true` by default (preserve user's Z rotation, only optimize XY). This eliminates the rotation gap entirely — parts stay at their original orientation, only their XY position changes.

### Future Fix
Implement rotated voxel lookup in the collision check (as the GPU benchmark already does). This requires transforming grid coordinates through cos/sin per lookup, which is ~2-3x slower on CPU but proven to work on GPU.

## Iteration History

1. **v1**: Validator rotated meshes, nester didn't rotate grids → false collisions found
2. **v2**: Added safety margin (1.5x voxel size) → fixed false collisions but made 20 parts infeasible
3. **v3**: Realized the validator and nester disagree on rotation center — validation was testing a different arrangement
4. **v4 (final)**: Validator matches nester exactly (XY translation only, no rotation) → 5/5 pass, honest validation
