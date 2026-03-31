// sweep_full.cpp — Full 5-test sweep across the sweet spot range
//
// Focused on the interesting zone from the first sweep:
//   Resolutions: 1.0, 1.5, 2.0mm
//   Pop/Gen: 256/50 (fast), 512/100 (balanced)
// All 5 test cases including large parts.
//
// Runtime guard: 10min total watchdog.

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

static auto g_start = Clock::now();
static constexpr double WATCHDOG_S = 600.0; // 10 min total

bool watchdog_ok() {
    double e = std::chrono::duration<double>(Clock::now() - g_start).count();
    if (e > WATCHDOG_S) {
        std::cerr << "WATCHDOG: " << e << "s elapsed, aborting.\n";
        return false;
    }
    return true;
}

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

struct TestSet {
    std::string name;
    std::vector<std::string> paths;
};

int main(int argc, char** argv) {
    g_start = Clock::now();
    std::cout << "=== Snuggle Full Sweep (5 tests x 3 res x 2 configs) ===\n";
    std::cout << "Watchdog: " << WATCHDOG_S << "s\n\n";

    std::string R = "test-parts";
    if (argc > 1) R = argv[1];
    const char* env = std::getenv("SNUGGLE_TEST_PARTS");
    if (env) R = env;

    std::vector<TestSet> tests = {
        {
            "1-Small (4 files)",
            {
                R + "/small/part1.stl",
                R + "/small/part2.stl",
                R + "/small/part3.stl",
                R + "/small/part4.stl",
            }
        },
        {
            "2-Mixed (5 files)",
            {
                R + "/mixed/large_part.stl",
                R + "/mixed/medium_part1.stl",
                R + "/mixed/medium_part2.stl",
                R + "/mixed/small_part1.stl",
                R + "/mixed/small_part2.stl",
            }
        },
        {
            "3-Complex (6 files)",
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

    float resolutions[] = { 1.0f, 1.5f, 2.0f };
    struct PG { size_t pop; size_t gen; const char* label; };
    PG configs[] = {
        {256,  50, "fast"},
        {512, 100, "balanced"},
    };

    // Print header
    std::cout << "Test               | Res  | Config    | Vox(ms) | Nest(ms) | Total(ms) | BBox(mm2) | Bed%   | Col | OK\n";
    std::cout << "-------------------|------|-----------|---------|----------|-----------|-----------|--------|-----|---\n";

    for (const auto &ts : tests) {
        if (!watchdog_ok()) break;

        // Load meshes once per test
        std::vector<RawMesh> meshes;
        std::vector<std::string> names;
        bool load_ok = true;

        for (const auto &path : ts.paths) {
            Model model;
            std::string fname = stdfs::path(path).filename().string();
            if (!load_stl(path.c_str(), &model, fname.c_str())) {
                std::cout << ts.name << ": LOAD FAILED " << fname << "\n";
                load_ok = false; break;
            }
            meshes.push_back(extract(model.objects[0]->volumes[0]->mesh()));
            names.push_back(fname);
        }
        if (!load_ok) continue;

        for (float vs : resolutions) {
            if (!watchdog_ok()) break;

            // Voxelize at this resolution
            std::vector<snuggle::PartInfo> parts;
            auto t0 = Clock::now();
            bool vox_ok = true;

            for (size_t mi = 0; mi < meshes.size(); mi++) {
                snuggle::PartInfo pi;
                pi.name = names[mi];
                auto err = snuggle::voxelize_indexed_mesh(
                    meshes[mi].verts.data(), meshes[mi].nv,
                    meshes[mi].indices.data(), meshes[mi].nt,
                    vs, pi.grid);
                if (err != snuggle::VoxError::OK) {
                    std::cout << ts.name << " @ " << vs << "mm: vox fail " << names[mi] << "\n";
                    vox_ok = false; break;
                }
                pi.max_height_mm = pi.grid.nz * pi.grid.voxel_size;
                pi.hull_area_mm2 = pi.grid.nx * pi.grid.ny * vs * vs;
                parts.push_back(std::move(pi));
            }
            auto t1 = Clock::now();
            double vox_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            if (!vox_ok) continue;

            for (const auto &cfg_pg : configs) {
                if (!watchdog_ok()) break;

                snuggle::NesterConfig cfg;
                cfg.bed_width_mm = 256.0f;
                cfg.bed_height_mm = 256.0f;
                cfg.population_size = cfg_pg.pop;
                cfg.max_generations = cfg_pg.gen;
                cfg.timeout_seconds = 30.0;
                cfg.min_gap_mm = 5.0f;

                snuggle::SnuggleNester nester(cfg, 42);
                auto nt0 = Clock::now();
                auto result = nester.run(parts);
                auto nt1 = Clock::now();
                double nest_ms = std::chrono::duration<double, std::milli>(nt1 - nt0).count();

                float bbox = bounding_area(result.placements, parts);
                float bed_pct = bbox / (256.0f * 256.0f) * 100.0f;
                size_t col = validate_collisions(result.placements, parts);
                bool ok = result.feasible && col == 0;

                char line[300];
                snprintf(line, sizeof(line),
                    "%-19s| %3.1f  | %-9s | %7.0f | %8.0f | %9.0f | %9.0f | %5.1f%% | %3zu | %s",
                    ts.name.c_str(), vs, cfg_pg.label,
                    vox_ms, nest_ms, vox_ms + nest_ms,
                    bbox, bed_pct, col, ok ? "YES" : "NO");
                std::cout << line << "\n";
                std::cout.flush();
            }
        }
    }

    double total = std::chrono::duration<double>(Clock::now() - g_start).count();
    std::cout << "\nTotal sweep time: " << total << "s\n";
    return 0;
}
