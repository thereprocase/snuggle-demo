# Snuggle GPU Optimization Log

## Baseline (gpu_rotation_test.cpp, v1)

- **GPU:** RTX 3080 Ti, GL 4.3, 2GB SSBO max
- **Test:** 35 small parts, 2mm voxels, rotated collision
- **Architecture:** One `glDispatchCompute` per pair per rotation
  - 595 pairs × 1 rotation = 595 dispatches per arrangement eval
  - 30.5ms per arrangement eval
  - 32.8 arrangements/sec

### Bottleneck Analysis
- Per-dispatch overhead dominates: ~0.05ms per dispatch, but the shader work is tiny per dispatch
- Each dispatch does NxA × NyA threads iterating over NzA — small grids (~20x20x15) mean very few threads
- GPU occupancy is terrible: we're launching 20×20 = 400 threads per dispatch on a GPU with 10,240 CUDA cores
- **Root cause: dispatch overhead >> actual compute**

## Optimization Ideas

1. **Batch all pairs into one dispatch** — encode pair indices in a buffer, one workgroup per pair
2. **Batch all arrangements** — evaluate entire population in one mega-dispatch
3. **Use shared memory** — load grid B into shared mem for reuse across threads
4. **Reduce readbacks** — accumulate results on GPU, read back once per generation
5. **Coalesce grids** — pack all part grids into one big SSBO with offset table

---

## Round 1: Batch all pairs into one dispatch

### Changes
- Packed all voxel grids into one SSBO (9 KB total for 35 parts)
- Grid metadata (offset, nx, ny, nz) in separate SSBO
- Per-pair work items (grid indices, placements, rotation) in another SSBO
- One dispatch per arrangement: 595 workgroups (one per pair), 256 threads each
- Threads within a workgroup split grid A's columns and use shared memory atomic for counting

### Results — 35 parts, 200 arrangements

| Method | Per Arrangement | Arrangements/sec | Notes |
|--------|----------------|-------------------|-------|
| **CPU (no rotation)** | 0.181 ms | 5,526 | VoxelGrid::collision_count, XY offset only |
| **GPU batched (with rotation)** | 0.106 ms | 9,393 | One dispatch per arrangement, readback each |
| **GPU batched deferred** | 0.096 ms | 10,419 | Skip per-arrangement readback |

### Analysis
- GPU is **1.7x faster** than CPU even though it's doing MORE work (rotated collision vs XY-only)
- GPU with rotation at 0.1ms/arrangement = **10,000 arrangements/sec**
- At pop=512, gen=100: 51,200 evaluations × 0.1ms = **5.1 seconds** per run
- Deferred readback saves ~10% — readback overhead is small
- **The grids are TINY (9KB total)** — this fits entirely in L1 cache on CPU too, which is why CPU is so fast
- GPU advantage will grow with larger parts / finer resolution where grids don't fit cache

### Bottleneck now
- CPU-side pair_work buffer construction (cos/sin per pair) takes measurable time
- `glBufferSubData` for uploading pair_work each arrangement adds latency
- Could pre-compute all arrangements' pair_work and upload once

## Round 2: Resolution scaling — where GPU wins

Same 35 parts, 200 arrangements, batched dispatch.

| Resolution | Grid Size | CPU (arr/sec) | GPU batched (arr/sec) | GPU deferred (arr/sec) | GPU speedup |
|-----------|-----------|---------------|----------------------|----------------------|-------------|
| 2.0mm | 9 KB | 5,526 | 9,393 | 10,419 | **1.7-1.9x** |
| 1.0mm | 58 KB | 1,356 | 6,372 | 4,712 | **3.5-4.7x** |
| 0.5mm | 408 KB | 255 | 623 | 1,112 | **2.4-4.4x** |

### Analysis
- GPU advantage grows with resolution: 1.7x at 2mm → 4.7x at 1mm
- At 0.5mm, grids blow out CPU cache (408KB > L1), GPU wins big on deferred readback
- **Deferred readback matters more at fine resolution** — at 0.5mm it's 2x faster than per-arrangement readback
- At 0.5mm deferred: 1,112 arr/sec → pop=512 × gen=100 in **46 seconds** — too slow for "button press" but viable for quality mode
- At 1.0mm batched: 6,372 arr/sec → pop=512 × gen=100 in **8 seconds** — usable
- At 2.0mm deferred: 10,419 arr/sec → pop=512 × gen=100 in **4.9 seconds** — good
- **Sweet spot for GPU: 1.0mm resolution with deferred readback**

### Key insight
- The GPU does rotated collision (cos/sin per voxel) while CPU does XY-offset only
- At equal work (if CPU also did rotation), GPU advantage would be even larger
- The batching eliminated dispatch overhead — now bottleneck is actual compute + upload

## Next optimizations to try
- Pre-compute all arrangements' pair_work on CPU and upload once (amortize upload)
- Use persistent mapped buffers instead of glBufferSubData
- Batch MULTIPLE arrangements per dispatch (one workgroup per pair×arrangement)
- Move the GA selection/crossover/mutation to GPU too (full GPU pipeline)
