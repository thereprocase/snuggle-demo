// test_snuggle.cpp — Snuggle Genetic Nester: First Run
//
// Loads test STLs, voxelizes them politely, runs the genetic nester,
// validates collision-free placement, reports packing metrics.
//
// This is the "does Darwin actually produce valid arrangements?" test.

#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <cmath>
#include <filesystem>

// Orca headers (STL loading)
#include "libslic3r/libslic3r.h"
#include "libslic3r/Model.hpp"
#include "libslic3r/Format/STL.hpp"
#include "libslic3r/TriangleMesh.hpp"

// Snuggle
#include "polite_voxelizer.hpp"
#include "snuggle_nester.hpp"

namespace stdfs = std::filesystem;
using namespace Slic3r;
using Clock = std::chrono::steady_clock;

static constexpr float VOXEL_SIZE_MM  = 1.5f;   // Balance speed vs accuracy
static constexpr float BED_W          = 256.0f;
static constexpr float BED_H          = 256.0f;

// ── Extract mesh data from Orca ───────────────────────────
struct RawMesh {
    std::vector<float>    verts;
    std::vector<uint32_t> indices;
    size_t nv = 0, nt = 0;
};

RawMesh extract(const TriangleMesh &mesh) {
    RawMesh m;
    const auto &its = mesh.its;
    m.nv = its.vertices.size();
    m.nt = its.indices.size();
    m.verts.resize(m.nv * 3);
    m.indices.resize(m.nt * 3);
    for (size_t i = 0; i < m.nv; i++) {
        m.verts[i*3+0] = its.vertices[i].x();
        m.verts[i*3+1] = its.vertices[i].y();
        m.verts[i*3+2] = its.vertices[i].z();
    }
    for (size_t i = 0; i < m.nt; i++) {
        m.indices[i*3+0] = its.indices[i](0);
        m.indices[i*3+1] = its.indices[i](1);
        m.indices[i*3+2] = its.indices[i](2);
    }
    return m;
}

// ── Compute packing bounding area ─────────────────────────
float bounding_area(const std::vector<snuggle::Placement> &pl,
                    const std::vector<snuggle::PartInfo> &parts)
{
    float min_x = 1e18f, max_x = -1e18f;
    float min_y = 1e18f, max_y = -1e18f;
    for (size_t i = 0; i < pl.size(); i++) {
        const auto &p = pl[i];
        const auto &g = parts[i].grid;
        float px_min = p.x + g.origin.x;
        float py_min = p.y + g.origin.y;
        float px_max = p.x + g.origin.x + g.nx * g.voxel_size;
        float py_max = p.y + g.origin.y + g.ny * g.voxel_size;
        min_x = std::min(min_x, px_min);
        min_y = std::min(min_y, py_min);
        max_x = std::max(max_x, px_max);
        max_y = std::max(max_y, py_max);
    }
    return (max_x - min_x) * (max_y - min_y);
}

// ── Validate: re-check collisions independently ───────────
size_t validate_collisions(const std::vector<snuggle::Placement> &pl,
                           const std::vector<snuggle::PartInfo> &parts)
{
    size_t total = 0;
    for (size_t i = 0; i < pl.size(); i++) {
        for (size_t j = i + 1; j < pl.size(); j++) {
            snuggle::Vec3f oi = {pl[i].x, pl[i].y, 0};
            snuggle::Vec3f oj = {pl[j].x, pl[j].y, 0};
            total += snuggle::VoxelGrid::collision_count(
                parts[i].grid, oi, parts[j].grid, oj);
        }
        snuggle::polite_yield();
    }
    return total;
}

// ── Test case ─────────────────────────────────────────────
struct TestCase {
    std::string name;
    std::vector<std::string> paths;
};

int main(int argc, char** argv) {
    std::cout << "=== SNUGGLE: Genetic Nester First Run ===\n";
    std::cout << "Voxel size: " << VOXEL_SIZE_MM << " mm\n";
    std::cout << "Bed: " << BED_W << " x " << BED_H << " mm\n\n";

    // Set SNUGGLE_TEST_PARTS env var or pass directory as first arg.
    std::string R = "test-parts";
    if (argc > 1) R = argv[1];
    const char* env = std::getenv("SNUGGLE_TEST_PARTS");
    if (env) R = env;

    // ── CONFIGURE YOUR TEST CASES HERE ───────────────────────
    // Point these at your own STL files. Organize by project/size.
    // Example structure:
    //   test-parts/
    //     small/part1.stl, part2.stl, ...
    //     mixed/large_part.stl, medium_part.stl, small_part.stl
    //     complex/cylinder.stl, ring.stl, bracket.stl
    std::vector<TestCase> tests = {
        {
            "Small Parts (example: 4-8 parts under 50mm)",
            {
                R + "/small/part1.stl",
                R + "/small/part2.stl",
                R + "/small/part3.stl",
                R + "/small/part4.stl",
            }
        },
        {
            "Mixed Sizes (example: 1 large + small parts)",
            {
                R + "/mixed/large_part.stl",
                R + "/mixed/medium_part_L.stl",
                R + "/mixed/medium_part_R.stl",
                R + "/mixed/small_part1.stl",
                R + "/mixed/small_part2.stl",
            }
        },
        {
            "Complex Geometry (example: cylinders, rings, brackets)",
            {
                R + "/complex/cylinder.stl",
                R + "/complex/piston.stl",
                R + "/complex/ring1.stl",
                R + "/complex/ring2.stl",
                R + "/complex/pin.stl",
                R + "/complex/bracket.stl",
            }
        },
    };

    for (const auto &tc : tests) {
        std::cout << "====== " << tc.name << " ======\n";

        // ── Load & voxelize ────────────────────────────────
        std::vector<snuggle::PartInfo> parts;
        bool ok = true;
        double vox_time_total = 0;

        for (const auto &path : tc.paths) {
            std::string fname = stdfs::path(path).filename().string();
            std::cout << "  Loading " << fname << "... ";

            Model model;
            if (!load_stl(path.c_str(), &model, fname.c_str())) {
                std::cout << "FAILED\n"; ok = false; break;
            }

            RawMesh rm = extract(model.objects[0]->volumes[0]->mesh());

            snuggle::PartInfo pi;
            pi.name = fname;

            auto t0 = Clock::now();
            auto err = snuggle::voxelize_indexed_mesh(
                rm.verts.data(), rm.nv,
                rm.indices.data(), rm.nt,
                VOXEL_SIZE_MM, pi.grid);
            auto t1 = Clock::now();
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            vox_time_total += ms;

            if (err != snuggle::VoxError::OK) {
                std::cout << "VOX FAILED: " << snuggle::vox_error_str(err) << "\n";
                ok = false; break;
            }

            // Compute part metrics
            pi.max_height_mm = pi.grid.nz * pi.grid.voxel_size;
            pi.hull_area_mm2 = pi.grid.nx * pi.grid.ny * pi.grid.voxel_size * pi.grid.voxel_size;

            std::cout << rm.nt << " tris -> "
                      << pi.grid.nx << "x" << pi.grid.ny << "x" << pi.grid.nz
                      << " (" << ms << "ms)\n";

            parts.push_back(std::move(pi));
        }

        if (!ok) {
            std::cout << "  SKIPPED\n\n";
            continue;
        }

        std::cout << "  Voxelization total: " << vox_time_total << " ms\n";

        // ── Run Snuggle ────────────────────────────────────
        snuggle::NesterConfig cfg;
        cfg.bed_width_mm  = BED_W;
        cfg.bed_height_mm = BED_H;
        cfg.population_size = 512;
        cfg.max_generations = 100;
        cfg.timeout_seconds = 20.0;
        cfg.min_gap_mm = 5.0f;

        std::cout << "  Running Snuggle (pop=" << cfg.population_size
                  << ", gen=" << cfg.max_generations << ")...\n";

        snuggle::SnuggleNester nester(cfg);
        auto result = nester.run(parts, [](size_t gen, size_t max_gen,
                                           const snuggle::Individual &best) -> bool {
            if (gen % 20 == 0) {
                std::cout << "    Gen " << gen << "/" << max_gen
                          << " | collisions=" << best.collision_count
                          << " oob=" << best.oob_count
                          << " feasible=" << (best.is_feasible() ? "YES" : "no")
                          << " fitness=" << best.fitness << "\n";
            }
            return true; // continue
        });

        // ── Report ─────────────────────────────────────────
        std::cout << "\n  --- Result ---\n";
        std::cout << "  Time:        " << result.time_ms << " ms"
                  << (result.timed_out ? " (TIMEOUT)" : "") << "\n";
        std::cout << "  Feasible:    " << (result.feasible ? "YES" : "NO") << "\n";
        std::cout << "  Collisions:  " << result.collisions << "\n";
        std::cout << "  Out of bed:  " << result.oob << "\n";
        std::cout << "  Fitness:     " << result.fitness << "\n";

        // Validate independently
        size_t real_collisions = validate_collisions(result.placements, parts);
        std::cout << "  Validated:   " << real_collisions << " collisions (independent check)\n";

        // Packing metrics
        float bbox = bounding_area(result.placements, parts);
        float bed_area = BED_W * BED_H;
        std::cout << "  Bounding:    " << bbox << " mm^2 ("
                  << (bbox / bed_area * 100.0f) << "% of bed)\n";

        // Per-part placements
        for (size_t i = 0; i < result.placements.size(); i++) {
            const auto &p = result.placements[i];
            std::cout << "    " << parts[i].name
                      << ": x=" << p.x << " y=" << p.y
                      << " rot=" << (p.zrot * 180.0f / 3.14159265f) << "deg\n";
        }

        // Pass/fail
        if (result.feasible && real_collisions == 0) {
            std::cout << "  STATUS: PASS (collision-free, on-bed)\n";
        } else {
            std::cout << "  STATUS: FAIL";
            if (!result.feasible) std::cout << " (not feasible)";
            if (real_collisions > 0) std::cout << " (" << real_collisions << " collisions)";
            std::cout << "\n";
        }

        std::cout << "\n";
    }

    return 0;
}
