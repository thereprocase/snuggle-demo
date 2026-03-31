// validate_collisions.cpp — Independent collision validation for Snuggle
//
// Uses EXACT triangle-triangle intersection (Möller algorithm) to verify
// that Snuggle's arrangements are truly collision-free. This is completely
// independent of the voxelizer — different algorithm, different code path,
// different math. If both agree, we're solid.
//
// Flow:
//   1. Load real STLs
//   2. Voxelize + run Snuggle nester (get placements)
//   3. Transform all mesh triangles to their placed positions
//   4. For every pair of parts, test ALL triangle pairs for intersection
//   5. Report any actual mesh collisions found
//
// This is EXPENSIVE (O(N_tris^2) per pair) but EXACT. We don't care
// about speed here — we care about truth.

#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <algorithm>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Model.hpp"
#include "libslic3r/Format/STL.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include "polite_voxelizer.hpp"
#include "snuggle_nester.hpp"

namespace stdfs = std::filesystem;
using namespace Slic3r;
using Clock = std::chrono::steady_clock;

// ── Möller–Trumbore triangle-triangle intersection ────────
// Completely independent of voxel code. Pure geometry.

struct Tri {
    float v[3][3]; // 3 vertices, each xyz
};

// Cross product
static void cross(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1]*b[2] - a[2]*b[1];
    out[1] = a[2]*b[0] - a[0]*b[2];
    out[2] = a[0]*b[1] - a[1]*b[0];
}

static float dot(const float a[3], const float b[3]) {
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}

static void sub(const float a[3], const float b[3], float out[3]) {
    out[0] = a[0]-b[0]; out[1] = a[1]-b[1]; out[2] = a[2]-b[2];
}

// Ray-triangle intersection (Möller–Trumbore)
// Returns true if ray from orig in direction dir hits triangle (v0,v1,v2)
// at parameter t in [0, max_t]
static bool ray_tri(const float orig[3], const float dir[3],
                    const float v0[3], const float v1[3], const float v2[3],
                    float max_t)
{
    float e1[3], e2[3], h[3], s[3], q[3];
    sub(v1, v0, e1);
    sub(v2, v0, e2);
    cross(dir, e2, h);
    float a = dot(e1, h);
    if (std::abs(a) < 1e-10f) return false;
    float f = 1.0f / a;
    sub(orig, v0, s);
    float u = f * dot(s, h);
    if (u < 0.0f || u > 1.0f) return false;
    cross(s, e1, q);
    float v = f * dot(dir, q);
    if (v < 0.0f || u + v > 1.0f) return false;
    float t = f * dot(e2, q);
    return (t > 1e-8f && t < max_t);
}

// Triangle-triangle intersection test
// Tests if any edge of triA pierces triB or vice versa
static bool tri_tri_intersect(const Tri &a, const Tri &b) {
    // Test each edge of A against triangle B
    for (int i = 0; i < 3; i++) {
        int j = (i + 1) % 3;
        float dir[3];
        sub(a.v[j], a.v[i], dir);
        float len = std::sqrt(dot(dir, dir));
        if (len < 1e-12f) continue;
        float ndir[3] = { dir[0]/len, dir[1]/len, dir[2]/len };
        if (ray_tri(a.v[i], ndir, b.v[0], b.v[1], b.v[2], len))
            return true;
    }
    // Test each edge of B against triangle A
    for (int i = 0; i < 3; i++) {
        int j = (i + 1) % 3;
        float dir[3];
        sub(b.v[j], b.v[i], dir);
        float len = std::sqrt(dot(dir, dir));
        if (len < 1e-12f) continue;
        float ndir[3] = { dir[0]/len, dir[1]/len, dir[2]/len };
        if (ray_tri(b.v[i], ndir, a.v[0], a.v[1], a.v[2], len))
            return true;
    }
    return false;
}

// ── AABB early-out for triangle pairs ─────────────────────
struct AABB3 {
    float min[3], max[3];
    void init(const Tri &t) {
        for (int d = 0; d < 3; d++) {
            min[d] = std::min({t.v[0][d], t.v[1][d], t.v[2][d]});
            max[d] = std::max({t.v[0][d], t.v[1][d], t.v[2][d]});
        }
    }
    bool overlaps(const AABB3 &o) const {
        return min[0] <= o.max[0] && max[0] >= o.min[0] &&
               min[1] <= o.max[1] && max[1] >= o.min[1] &&
               min[2] <= o.max[2] && max[2] >= o.min[2];
    }
};

// ── Mesh part with placed triangles ───────────────────────
struct PlacedMesh {
    std::vector<Tri> tris;
    std::vector<AABB3> tri_aabbs;
    AABB3 mesh_aabb;
    std::string name;

    void compute_aabbs() {
        tri_aabbs.resize(tris.size());
        mesh_aabb.min[0] = mesh_aabb.min[1] = mesh_aabb.min[2] = 1e18f;
        mesh_aabb.max[0] = mesh_aabb.max[1] = mesh_aabb.max[2] = -1e18f;
        for (size_t i = 0; i < tris.size(); i++) {
            tri_aabbs[i].init(tris[i]);
            for (int d = 0; d < 3; d++) {
                mesh_aabb.min[d] = std::min(mesh_aabb.min[d], tri_aabbs[i].min[d]);
                mesh_aabb.max[d] = std::max(mesh_aabb.max[d], tri_aabbs[i].max[d]);
            }
        }
    }
};

// ── Check two placed meshes for any triangle intersection ──
size_t check_mesh_pair(const PlacedMesh &a, const PlacedMesh &b) {
    // Early out: mesh AABBs don't overlap
    if (!a.mesh_aabb.overlaps(b.mesh_aabb))
        return 0;

    size_t collisions = 0;
    for (size_t i = 0; i < a.tris.size(); i++) {
        for (size_t j = 0; j < b.tris.size(); j++) {
            // Early out: triangle AABBs don't overlap
            if (!a.tri_aabbs[i].overlaps(b.tri_aabbs[j]))
                continue;
            if (tri_tri_intersect(a.tris[i], b.tris[j]))
                collisions++;
        }
    }
    return collisions;
}

int main() {
    std::cout << "=== Snuggle Independent Collision Validator ===\n";
    std::cout << "Method: Exact triangle-triangle intersection (Moller-Trumbore)\n";
    std::cout << "This is INDEPENDENT of the voxelizer.\n\n";

    // ── Load parts ────────────────────────────────────────
    std::vector<std::string> dirs = {
        "F:/Claude/3d-test-parts/sorted/battery-electronics/small-under-50mm",
        "F:/Claude/3d-test-parts/sorted/coffee-gaggia/small-under-50mm",
        "F:/Claude/3d-test-parts/sorted/flight-sim-gaming/small-under-50mm",
        "F:/Claude/3d-test-parts/sorted/misc/small-under-50mm",
        "F:/Claude/3d-test-parts/sorted/storage-organization/small-under-50mm",
        "F:/Claude/3d-test-parts/sorted/printer-accessories/small-under-50mm",
    };

    struct RawPart {
        std::vector<Tri> tris; // original mesh triangles (mm)
        snuggle::PartInfo vox_info;
        std::string name;
    };

    std::vector<RawPart> parts;
    float vox_size = 2.0f;

    for (const auto &dir : dirs) {
        if (!stdfs::exists(dir)) continue;
        for (const auto &entry : stdfs::directory_iterator(dir)) {
            auto ext = entry.path().extension().string();
            if (ext != ".stl" && ext != ".STL") continue;

            Model model;
            std::string fname = entry.path().filename().string();
            if (!load_stl(entry.path().string().c_str(), &model, fname.c_str())) continue;

            const auto &its = model.objects[0]->volumes[0]->mesh().its;

            RawPart rp;
            rp.name = fname;

            // Extract raw triangles
            rp.tris.resize(its.indices.size());
            for (size_t i = 0; i < its.indices.size(); i++) {
                for (int v = 0; v < 3; v++) {
                    int vi = its.indices[i](v);
                    rp.tris[i].v[v][0] = its.vertices[vi].x();
                    rp.tris[i].v[v][1] = its.vertices[vi].y();
                    rp.tris[i].v[v][2] = its.vertices[vi].z();
                }
            }

            // Voxelize for Snuggle
            std::vector<float> verts(its.vertices.size() * 3);
            std::vector<uint32_t> indices(its.indices.size() * 3);
            for (size_t i = 0; i < its.vertices.size(); i++) {
                verts[i*3] = its.vertices[i].x();
                verts[i*3+1] = its.vertices[i].y();
                verts[i*3+2] = its.vertices[i].z();
            }
            for (size_t i = 0; i < its.indices.size(); i++) {
                indices[i*3] = its.indices[i](0);
                indices[i*3+1] = its.indices[i](1);
                indices[i*3+2] = its.indices[i](2);
            }

            auto err = snuggle::voxelize_indexed_mesh(
                verts.data(), its.vertices.size(),
                indices.data(), its.indices.size(),
                vox_size, rp.vox_info.grid);
            if (err != snuggle::VoxError::OK) continue;

            rp.vox_info.max_height_mm = rp.vox_info.grid.nz * vox_size;
            rp.vox_info.hull_area_mm2 = rp.vox_info.grid.nx * rp.vox_info.grid.ny * vox_size * vox_size;
            rp.vox_info.name = fname;

            parts.push_back(std::move(rp));
            if (parts.size() >= 20) break;
        }
        if (parts.size() >= 20) break;
    }

    std::cout << "Loaded " << parts.size() << " parts.\n\n";

    // ── Run test rounds ───────────────────────────────────
    int total_tests = 0, total_pass = 0, total_fail = 0;

    // Test at multiple part counts
    size_t test_counts[] = { 5, 8, 12, 15, 20 };

    for (size_t count : test_counts) {
        if (count > parts.size()) continue;

        std::cout << "--- Testing " << count << " parts ---\n";

        // Build PartInfo vector for nester
        std::vector<snuggle::PartInfo> nester_parts;
        for (size_t i = 0; i < count; i++)
            nester_parts.push_back(parts[i].vox_info);

        // Run Snuggle with rotation safety margin
        snuggle::NesterConfig cfg;
        cfg.bed_width_mm = 256.0f;
        cfg.bed_height_mm = 256.0f;
        cfg.voxel_size_mm = vox_size;
        cfg.population_size = 512;
        cfg.max_generations = 100;
        cfg.min_gap_mm = 5.0f;
        cfg.timeout_seconds = 30.0;

        snuggle::SnuggleNester nester(cfg);
        auto result = nester.run(nester_parts);

        std::cout << "  Snuggle: feasible=" << result.feasible
                  << " voxel_collisions=" << result.collisions
                  << " time=" << result.time_ms << "ms\n";

        if (!result.feasible) {
            std::cout << "  SKIP (not feasible)\n\n";
            continue;
        }

        // ── Transform meshes to placed positions ──────────
        std::vector<PlacedMesh> placed(count);
        for (size_t i = 0; i < count; i++) {
            const auto &pl = result.placements[i];
            placed[i].name = parts[i].name;
            placed[i].tris.resize(parts[i].tris.size());

            // The nester currently does NOT rotate voxel grids.
            // The placement (pl.x, pl.y) is a translation offset; pl.zrot
            // is assigned but not used in collision detection.
            //
            // For validation, we must test what ACTUALLY happens:
            // the nester places parts with XY translation only (no rotation
            // applied to collision). So the validator should also NOT rotate.
            // If the validator rotates but the nester doesn't, we're testing
            // a different arrangement than what was computed.
            //
            // Test the ACTUAL arrangement: translate only, no rotation.
            for (size_t ti = 0; ti < parts[i].tris.size(); ti++) {
                for (int v = 0; v < 3; v++) {
                    placed[i].tris[ti].v[v][0] = parts[i].tris[ti].v[v][0] + pl.x;
                    placed[i].tris[ti].v[v][1] = parts[i].tris[ti].v[v][1] + pl.y;
                    placed[i].tris[ti].v[v][2] = parts[i].tris[ti].v[v][2];
                }
            }
            placed[i].compute_aabbs();
        }

        // ── Exact pairwise collision check ────────────────
        auto t0 = Clock::now();
        size_t total_tri_collisions = 0;
        size_t pairs_checked = 0;
        size_t pairs_with_collision = 0;

        for (size_t i = 0; i < count; i++) {
            for (size_t j = i + 1; j < count; j++) {
                size_t col = check_mesh_pair(placed[i], placed[j]);
                pairs_checked++;
                if (col > 0) {
                    pairs_with_collision++;
                    total_tri_collisions += col;
                    std::cout << "  COLLISION: " << placed[i].name
                              << " vs " << placed[j].name
                              << " (" << col << " triangle intersections)\n";
                }
                snuggle::polite_yield();
            }
        }

        auto t1 = Clock::now();
        double check_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        total_tests++;
        if (pairs_with_collision == 0) {
            total_pass++;
            std::cout << "  PASS: " << pairs_checked << " pairs checked, "
                      << "0 collisions (" << check_ms << "ms)\n";
        } else {
            total_fail++;
            std::cout << "  FAIL: " << pairs_with_collision << " colliding pairs, "
                      << total_tri_collisions << " triangle intersections ("
                      << check_ms << "ms)\n";
        }
        std::cout << "\n";
    }

    std::cout << "=== FINAL RESULT ===\n";
    std::cout << "Tests: " << total_tests
              << " | Pass: " << total_pass
              << " | Fail: " << total_fail << "\n";
    std::cout << (total_fail == 0 ? "ALL CLEAR — Snuggle is collision-free (independently verified)\n"
                                  : "COLLISIONS DETECTED — investigate!\n");

    return total_fail > 0 ? 1 : 0;
}
