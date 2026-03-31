// sweep_resolution.cpp — Find the sweet spot: resolution vs time vs packing
//
// Runs Snuggle on one test case at multiple voxel resolutions,
// reports voxelization time, nester time, packing quality, and collision count.

#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <cmath>
#include <filesystem>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Model.hpp"
#include "libslic3r/Format/STL.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include "polite_voxelizer.hpp"
#include "snuggle_nester.hpp"

namespace stdfs = std::filesystem;
using namespace Slic3r;
using Clock = std::chrono::steady_clock;

struct RawMesh {
    std::vector<float> verts;
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
        m.verts[i*3] = its.vertices[i].x();
        m.verts[i*3+1] = its.vertices[i].y();
        m.verts[i*3+2] = its.vertices[i].z();
    }
    for (size_t i = 0; i < m.nt; i++) {
        m.indices[i*3] = its.indices[i](0);
        m.indices[i*3+1] = its.indices[i](1);
        m.indices[i*3+2] = its.indices[i](2);
    }
    return m;
}

float bounding_area(const std::vector<snuggle::Placement> &pl,
                    const std::vector<snuggle::PartInfo> &parts) {
    float min_x = 1e18f, max_x = -1e18f, min_y = 1e18f, max_y = -1e18f;
    for (size_t i = 0; i < pl.size(); i++) {
        const auto &p = pl[i];
        const auto &g = parts[i].grid;
        min_x = std::min(min_x, p.x + g.origin.x);
        min_y = std::min(min_y, p.y + g.origin.y);
        max_x = std::max(max_x, p.x + g.origin.x + g.nx * g.voxel_size);
        max_y = std::max(max_y, p.y + g.origin.y + g.ny * g.voxel_size);
    }
    return (max_x - min_x) * (max_y - min_y);
}

size_t validate_collisions(const std::vector<snuggle::Placement> &pl,
                           const std::vector<snuggle::PartInfo> &parts) {
    size_t total = 0;
    for (size_t i = 0; i < pl.size(); i++)
        for (size_t j = i + 1; j < pl.size(); j++) {
            snuggle::Vec3f oi = {pl[i].x, pl[i].y, 0};
            snuggle::Vec3f oj = {pl[j].x, pl[j].y, 0};
            total += snuggle::VoxelGrid::collision_count(parts[i].grid, oi, parts[j].grid, oj);
        }
    return total;
}

int main(int argc, char** argv) {
    std::cout << "=== Snuggle Resolution Sweep ===\n\n";

    std::string R = "test-parts";
    if (argc > 1) R = argv[1];
    const char* env = std::getenv("SNUGGLE_TEST_PARTS");
    if (env) R = env;

    // Two test cases: small parts and mixed parts
    struct TestSet {
        std::string name;
        std::vector<std::string> paths;
    };

    std::vector<TestSet> test_sets = {
        {
            "Small Parts (4 files)",
            {
                R + "/small/part1.stl",
                R + "/small/part2.stl",
                R + "/small/part3.stl",
                R + "/small/part4.stl",
            }
        },
        {
            "Mixed Sizes (5 files)",
            {
                R + "/mixed/large_part.stl",
                R + "/mixed/medium_part1.stl",
                R + "/mixed/medium_part2.stl",
                R + "/mixed/small_part1.stl",
                R + "/mixed/small_part2.stl",
            }
        },
    };

    // Resolutions to sweep
    float resolutions[] = { 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f };
    int n_res = sizeof(resolutions) / sizeof(resolutions[0]);

    // Population/generation combos to try at each resolution
    struct PGCombo { size_t pop; size_t gen; };
    PGCombo pg_combos[] = {
        {256, 50},
        {512, 100},
        {1024, 100},
    };
    int n_pg = sizeof(pg_combos) / sizeof(pg_combos[0]);

    // Load meshes once
    for (const auto &ts : test_sets) {
        std::cout << "==============================\n";
        std::cout << "Test: " << ts.name << "\n";
        std::cout << "==============================\n\n";

        // Load raw meshes
        std::vector<RawMesh> meshes;
        std::vector<std::string> names;
        for (const auto &path : ts.paths) {
            Model model;
            std::string fname = stdfs::path(path).filename().string();
            load_stl(path.c_str(), &model, fname.c_str());
            meshes.push_back(extract(model.objects[0]->volumes[0]->mesh()));
            names.push_back(fname);
        }

        // Header
        std::cout << "Res(mm) | Pop  | Gen | Vox(ms) | Nest(ms) | Total(ms) | BBox(mm2)  | Bed%   | Col | Feasible\n";
        std::cout << "--------|------|-----|---------|----------|-----------|------------|--------|-----|--------\n";

        for (int ri = 0; ri < n_res; ri++) {
            float vs = resolutions[ri];

            // Voxelize at this resolution
            std::vector<snuggle::PartInfo> parts;
            auto t_vox0 = Clock::now();
            bool vox_ok = true;

            for (size_t mi = 0; mi < meshes.size(); mi++) {
                snuggle::PartInfo pi;
                pi.name = names[mi];
                auto err = snuggle::voxelize_indexed_mesh(
                    meshes[mi].verts.data(), meshes[mi].nv,
                    meshes[mi].indices.data(), meshes[mi].nt,
                    vs, pi.grid);
                if (err != snuggle::VoxError::OK) {
                    std::cout << vs << "mm: voxelization failed for " << names[mi]
                              << " (" << snuggle::vox_error_str(err) << ")\n";
                    vox_ok = false;
                    break;
                }
                pi.max_height_mm = pi.grid.nz * pi.grid.voxel_size;
                pi.hull_area_mm2 = pi.grid.nx * pi.grid.ny * vs * vs;
                parts.push_back(std::move(pi));
            }

            auto t_vox1 = Clock::now();
            double vox_ms = std::chrono::duration<double, std::milli>(t_vox1 - t_vox0).count();

            if (!vox_ok) continue;

            // Run nester at each pop/gen combo
            for (int pi = 0; pi < n_pg; pi++) {
                snuggle::NesterConfig cfg;
                cfg.bed_width_mm = 256.0f;
                cfg.bed_height_mm = 256.0f;
                cfg.population_size = pg_combos[pi].pop;
                cfg.max_generations = pg_combos[pi].gen;
                cfg.timeout_seconds = 30.0;
                cfg.min_gap_mm = 5.0f;

                snuggle::SnuggleNester nester(cfg, 42);

                auto t_nest0 = Clock::now();
                auto result = nester.run(parts);
                auto t_nest1 = Clock::now();
                double nest_ms = std::chrono::duration<double, std::milli>(t_nest1 - t_nest0).count();

                float bbox = bounding_area(result.placements, parts);
                float bed_pct = bbox / (256.0f * 256.0f) * 100.0f;
                size_t real_col = validate_collisions(result.placements, parts);

                char line[256];
                snprintf(line, sizeof(line),
                    "%5.2f   | %4zu | %3zu | %7.0f | %8.0f | %9.0f | %10.0f | %5.1f%% | %3zu | %s",
                    vs,
                    pg_combos[pi].pop, pg_combos[pi].gen,
                    vox_ms, nest_ms, vox_ms + nest_ms,
                    bbox, bed_pct,
                    real_col,
                    (result.feasible && real_col == 0) ? "YES" : "NO");
                std::cout << line << "\n";
            }
        }
        std::cout << "\n";
    }

    return 0;
}
