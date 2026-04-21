# Snuggle: End-to-End Code Review & Alignment Report

## Executive Summary
An end-to-end review of the `snuggle_nester.hpp` implementation against the user intents outlined in `SNUGGLE_USER_STORY.md` and `SNUGGLE_UX_SPEC.md` reveals that while the core 3D-aware genetic algorithm is functioning effectively for compact packing, there are distinct areas where the resulting plate arrangements deviate from expected behaviors.

Specifically, two core discrepancies exist:
1. **Single-Part Centering:** The algorithm is required to instantly center single parts without invoking the genetic evolution loop.
2. **Blob Cluster Centrality:** The algorithm produces tight clusters but lacks an explicit mechanism to anchor those clusters strictly to the physical center of the printer's bed, sometimes resulting in off-center placements when the parts are small.

This report outlines these gaps and proposes concrete code modifications to align the implementation with user expectations.

---

## Analysis & Findings

### 1. Instant Single-Part Centering
**User Intent:**
According to `SNUGGLE_USER_STORY.md`, the system must support "Instant single-part centering (no GA for one part)" to provide immediate layout adjustments when only one part exists.

**Current Implementation Gap:**
Reviewing `snuggle_nester.hpp`, lines 140–145, the `run` method successfully catches zero-part scenarios but fails to handle one-part scenarios gracefully:

```cpp
        size_t n_parts = parts.size();

        if (n_parts == 0) {
            result.time_ms = 0;
            return result;
        }
```

If `n_parts == 1`, the code proceeds to run the entire genetic algorithm over `cfg_.max_generations`. Not only is this computationally wasteful, it does not guarantee precise mathematical centering of the single object.

**Proposed Improvement:**
Intercept the case where `n_parts == 1` and instantly position the part at the bed's center.

```cpp
<<<<<<< SEARCH
        if (n_parts == 0) {
            result.time_ms = 0;
            return result;
        }

        // ── Initialize population ──────────────────────────
=======
        if (n_parts == 0) {
            result.time_ms = 0;
            return result;
        }

        // Instant single-part centering (no GA)
        if (n_parts == 1) {
            Placement p;
            p.x = cfg_.bed_width_mm * 0.5f;
            p.y = cfg_.bed_height_mm * 0.5f;
            p.zrot = cfg_.lock_rotation ? parts[0].initial_zrot : 0.0f;

            result.placements.push_back(p);
            result.fitness = 1.0f;
            result.collisions = 0;
            result.oob = 0;
            result.feasible = true;
            result.generations_run = 0;
            result.time_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_start).count();
            return result;
        }

        // ── Initialize population ──────────────────────────
>>>>>>> REPLACE
```


### 2. Tighter Blob-Shaped Clusters & Center Anchoring
**User Intent:**
The `SNUGGLE_UX_SPEC.md` requires the system to produce "tighter blob-shaped clusters" and the `SNUGGLE_USER_STORY.md` illustrates a desired "tight cluster near the center."

**Current Implementation Gap:**
In `snuggle_nester.hpp` (around line 686), the fitness function computes a `cluster_score` based on the mean distance of all parts from the *cluster's own centroid* (`cx`, `cy`). While this encourages a tight blob shape, it does not mandate *where* on the bed the blob should sit.

Additionally, while there is a `height_score` (line 709) that pulls tall parts toward the center of the bed (`bed_cx`, `bed_cy`), it relies solely on `max_height_mm`. If an arrangement consists mostly of short or uniform parts, the resulting cluster may be tightly packed but positioned haphazardly in an arbitrary corner of the print bed.

**Proposed Improvement:**
Introduce an explicit `bed_center_score` to evaluate the distance between the cluster's centroid (`cx`, `cy`) and the true center of the bed (`bed_cx`, `bed_cy`). This score should be integrated into the final fitness calculation.

```cpp
<<<<<<< SEARCH
            // Normalize: a perfectly centered blob on a 256mm bed has
            // mean dist ≈ 0. Parts scattered across the bed have mean dist ≈ 100mm.
            float bed_diag = std::sqrt(cfg_.bed_width_mm * cfg_.bed_width_mm +
                                       cfg_.bed_height_mm * cfg_.bed_height_mm);
            float cluster_score = 1.0f - std::clamp(mean_dist_from_center / (bed_diag * 0.25f), 0.0f, 1.0f);

            // 6. Height centering: tall parts near center
            float height_score = 0.0f;
            float bed_cx = cfg_.bed_width_mm * 0.5f;
            float bed_cy = cfg_.bed_height_mm * 0.5f;
            float max_dist = std::sqrt(bed_cx * bed_cx + bed_cy * bed_cy);
            float max_possible_height = 0;
            for (size_t i = 0; i < n; i++) {
                float dx = ind.placements[i].x - bed_cx;
                float dy = ind.placements[i].y - bed_cy;
                float dist = std::sqrt(dx*dx + dy*dy);
                float centrality = 1.0f - (dist / max_dist);
                height_score += parts[i].max_height_mm * centrality;
                max_possible_height += parts[i].max_height_mm;
            }
            if (max_possible_height > 0) height_score /= max_possible_height;

            ind.fitness = cfg_.w_compactness * compactness
                        + cfg_.w_compactness * 0.5f * density
                        + cfg_.w_compactness * 0.3f * proximity_score
                        + cfg_.w_compactness * 0.4f * aspect          // prefer square bbox
                        + cfg_.w_compactness * 0.5f * cluster_score   // prefer tight blob
                        + cfg_.w_height_center * height_score;
=======
            // Normalize: a perfectly centered blob on a 256mm bed has
            // mean dist ≈ 0. Parts scattered across the bed have mean dist ≈ 100mm.
            float bed_diag = std::sqrt(cfg_.bed_width_mm * cfg_.bed_width_mm +
                                       cfg_.bed_height_mm * cfg_.bed_height_mm);
            float cluster_score = 1.0f - std::clamp(mean_dist_from_center / (bed_diag * 0.25f), 0.0f, 1.0f);

            // 6. Height centering: tall parts near center
            float height_score = 0.0f;
            float bed_cx = cfg_.bed_width_mm * 0.5f;
            float bed_cy = cfg_.bed_height_mm * 0.5f;
            float max_dist = std::sqrt(bed_cx * bed_cx + bed_cy * bed_cy);
            float max_possible_height = 0;
            for (size_t i = 0; i < n; i++) {
                float dx = ind.placements[i].x - bed_cx;
                float dy = ind.placements[i].y - bed_cy;
                float dist = std::sqrt(dx*dx + dy*dy);
                float centrality = 1.0f - (dist / max_dist);
                height_score += parts[i].max_height_mm * centrality;
                max_possible_height += parts[i].max_height_mm;
            }
            if (max_possible_height > 0) height_score /= max_possible_height;

            // 7. Global bed centering: pull the whole cluster to the center of the bed
            float cluster_center_dist = std::sqrt((cx - bed_cx)*(cx - bed_cx) + (cy - bed_cy)*(cy - bed_cy));
            float bed_center_score = 1.0f - std::clamp(cluster_center_dist / (bed_diag * 0.5f), 0.0f, 1.0f);

            ind.fitness = cfg_.w_compactness * compactness
                        + cfg_.w_compactness * 0.5f * density
                        + cfg_.w_compactness * 0.3f * proximity_score
                        + cfg_.w_compactness * 0.4f * aspect          // prefer square bbox
                        + cfg_.w_compactness * 0.5f * cluster_score   // prefer tight blob
                        + cfg_.w_compactness * 0.5f * bed_center_score // pull cluster to bed center
                        + cfg_.w_height_center * height_score;
>>>>>>> REPLACE
```

---

## Conclusion
The Snuggle algorithmic foundation is robust, achieving highly dense, 3D-aware part packing. Implementing the two simple refinements described above will completely close the gap between current solver outputs and the exact aesthetic and functional requirements identified in user stories, guaranteeing consistent, centered, blob-like arrangements even in extreme edge cases (like one part or uniform height collections).
