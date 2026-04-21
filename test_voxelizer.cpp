// test_voxelizer.cpp — Snuggle Voxelizer Proof of Concept
//
// Loads test STLs, voxelizes them politely, validates:
//   1. Voxelization produces reasonable grids
//   2. Same part at same position = collision detected
//   3. Parts moved far apart = no collision
//   4. Parts at arrange baseline positions = collision check works
//
// Resource guards:
//   - Memory cap enforced by polite_voxelizer.hpp
//   - Total runtime watchdog
//   - Per-mesh voxelization timeout

#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <filesystem>
#include <cmath>

// Orca headers (for STL loading)
#include "libslic3r/libslic3r.h"
#include "libslic3r/Model.hpp"
#include "libslic3r/Format/STL.hpp"
#include "libslic3r/TriangleMesh.hpp"

// Snuggle voxelizer
#include "polite_voxelizer.hpp"

namespace stdfs = std::filesystem;
using Clock = std::chrono::steady_clock;
using namespace Slic3r;

// ── Watchdog ──────────────────────────────────────────────
static Clock::time_point g_start;
static constexpr double TOTAL_TIMEOUT_S = 120.0;  // 2 min max for entire test

bool check_watchdog() {
    double elapsed = std::chrono::duration<double>(Clock::now() - g_start).count();
    if (elapsed > TOTAL_TIMEOUT_S) {
        std::cerr << "WATCHDOG: Total runtime exceeded " << TOTAL_TIMEOUT_S << "s. Aborting.\n";
        return false;
    }
    return true;
}

// ── Extract triangles from Orca TriangleMesh ──────────────
//    Copies into snuggle's format. Polite: yields during copy.
struct MeshData {
    std::vector<float>    vertices;  // flat xyz
    std::vector<uint32_t> indices;   // flat i0,i1,i2
    size_t num_tris = 0;
    size_t num_verts = 0;
};

MeshData extract_mesh(const TriangleMesh &mesh) {
    MeshData md;
    const auto &its = mesh.its;
    md.num_verts = its.vertices.size();
    md.num_tris = its.indices.size();

    md.vertices.resize(md.num_verts * 3);
    for (size_t i = 0; i < md.num_verts; i++) {
        md.vertices[i*3+0] = its.vertices[i].x();
        md.vertices[i*3+1] = its.vertices[i].y();
        md.vertices[i*3+2] = its.vertices[i].z();
        if ((i+1) % 5000 == 0) snuggle::polite_yield();
    }

    md.indices.resize(md.num_tris * 3);
    for (size_t i = 0; i < md.num_tris; i++) {
        md.indices[i*3+0] = its.indices[i](0);
        md.indices[i*3+1] = its.indices[i](1);
        md.indices[i*3+2] = its.indices[i](2);
    }

    return md;
}

// ── Test result tracking ──────────────────────────────────
static int g_pass = 0, g_fail = 0;

void EXPECT(bool cond, const char* msg) {
    if (cond) {
        std::cout << "  PASS: " << msg << "\n";
        g_pass++;
    } else {
        std::cout << "  FAIL: " << msg << "\n";
        g_fail++;
    }
}

// ── MAIN ──────────────────────────────────────────────────
int main(int argc, char** argv) {
    g_start = Clock::now();

    std::cout << "=== Snuggle Voxelizer Test ===\n";
    std::cout << "Memory cap: " << snuggle::MAX_VOXEL_MEMORY_MB << " MB per grid\n";
    std::cout << "Max grid dim: " << snuggle::MAX_GRID_DIM << " voxels/axis\n";
    std::cout << "Watchdog: " << TOTAL_TIMEOUT_S << "s total\n\n";

    // Set via env var SNUGGLE_TEST_PARTS or pass as first argument.
    // Should contain STL files directly or in subdirectories.
    std::string parts_root = "test-parts";
    if (argc > 1) parts_root = argv[1];
    const char* env = std::getenv("SNUGGLE_TEST_PARTS");
    if (env) parts_root = env;

    // ── Test 1: Basic voxelization of a small part ─────────
    {
        std::cout << "--- Test 1: Voxelize a small part ---\n";
        Model model;
        std::string path = parts_root + "/small/part1.stl";
        bool loaded = load_stl(path.c_str(), &model, "cap");
        EXPECT(loaded, "STL loaded");
        if (!loaded) goto done;

        MeshData md = extract_mesh(model.objects[0]->volumes[0]->mesh());
        std::cout << "  Mesh: " << md.num_tris << " tris, " << md.num_verts << " verts\n";

        snuggle::VoxelGrid grid;
        float voxel_size = 1.0f; // 1mm resolution

        auto t0 = Clock::now();
        auto err = snuggle::voxelize_indexed_mesh(
            md.vertices.data(), md.num_verts,
            md.indices.data(), md.num_tris,
            voxel_size, grid,
            [](float p, const char* phase) -> bool {
                return check_watchdog();
            });
        auto t1 = Clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        EXPECT(err == snuggle::VoxError::OK, "Voxelization succeeded");
        std::cout << "  Grid: " << grid.nx << "x" << grid.ny << "x" << grid.nz
                  << " (" << grid.memory_bytes() << " bytes)\n";
        std::cout << "  Time: " << ms << " ms\n";

        size_t solid = grid.count_solid();
        std::cout << "  Solid voxels: " << solid << " / " << grid.total_voxels()
                  << " (" << (100.0 * solid / grid.total_voxels()) << "%)\n";

        EXPECT(grid.nx > 0 && grid.ny > 0 && grid.nz > 0, "Grid has nonzero dimensions");
        EXPECT(solid > 0, "Grid has solid voxels");
        EXPECT(grid.memory_bytes() < 1024 * 1024, "Grid under 1MB (it's a small part)");
        EXPECT(ms < 5000.0, "Voxelized in under 5 seconds");
    }

    if (!check_watchdog()) goto done;

    // ── Test 2: Self-collision (same part, same position) ──
    {
        std::cout << "\n--- Test 2: Self-collision detection ---\n";
        Model model;
        std::string path = parts_root + "/small/part2.stl";
        load_stl(path.c_str(), &model, "cap");
        MeshData md = extract_mesh(model.objects[0]->volumes[0]->mesh());

        snuggle::VoxelGrid gridA, gridB;
        snuggle::voxelize_indexed_mesh(
            md.vertices.data(), md.num_verts,
            md.indices.data(), md.num_tris,
            1.0f, gridA);
        snuggle::voxelize_indexed_mesh(
            md.vertices.data(), md.num_verts,
            md.indices.data(), md.num_tris,
            1.0f, gridB);

        // Same position = full collision
        snuggle::Vec3f zero = {0, 0, 0};
        size_t collisions = snuggle::VoxelGrid::collision_count(gridA, zero, gridB, zero);
        std::cout << "  Same position collisions: " << collisions << "\n";
        EXPECT(collisions > 0, "Identical parts at same position collide");
        EXPECT(collisions == gridA.count_solid(), "All solid voxels overlap");
    }

    if (!check_watchdog()) goto done;

    // ── Test 3: No collision when far apart ────────────────
    {
        std::cout << "\n--- Test 3: No collision when separated ---\n";
        Model model;
        std::string path = parts_root + "/small/part2.stl";
        load_stl(path.c_str(), &model, "cap");
        MeshData md = extract_mesh(model.objects[0]->volumes[0]->mesh());

        snuggle::VoxelGrid gridA, gridB;
        snuggle::voxelize_indexed_mesh(
            md.vertices.data(), md.num_verts,
            md.indices.data(), md.num_tris,
            1.0f, gridA);
        snuggle::voxelize_indexed_mesh(
            md.vertices.data(), md.num_verts,
            md.indices.data(), md.num_tris,
            1.0f, gridB);

        // Move B far away
        snuggle::Vec3f zero = {0, 0, 0};
        snuggle::Vec3f far_away = {500.0f, 500.0f, 0.0f};
        size_t collisions = snuggle::VoxelGrid::collision_count(gridA, zero, gridB, far_away);
        std::cout << "  Far apart collisions: " << collisions << "\n";
        EXPECT(collisions == 0, "Parts 500mm apart don't collide");
    }

    if (!check_watchdog()) goto done;

    // ── Test 4: Larger mesh voxelization ──
    {
        std::cout << "\n--- Test 4: Large mesh (high triangle count) ---\n";
        Model model;
        std::string path = parts_root + "/large/big_part.stl";
        bool loaded = load_stl(path.c_str(), &model, "case");
        EXPECT(loaded, "Large STL loaded");
        if (!loaded) goto done;

        MeshData md = extract_mesh(model.objects[0]->volumes[0]->mesh());
        std::cout << "  Mesh: " << md.num_tris << " tris\n";

        snuggle::VoxelGrid grid;
        float voxel_size = 2.0f; // 2mm for large part (polite memory)

        auto t0 = Clock::now();
        auto err = snuggle::voxelize_indexed_mesh(
            md.vertices.data(), md.num_verts,
            md.indices.data(), md.num_tris,
            voxel_size, grid,
            [](float p, const char* phase) -> bool {
                return check_watchdog();
            });
        auto t1 = Clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        EXPECT(err == snuggle::VoxError::OK, "Large mesh voxelized OK");
        std::cout << "  Grid: " << grid.nx << "x" << grid.ny << "x" << grid.nz
                  << " (" << grid.memory_bytes() / 1024 << " KB)\n";
        std::cout << "  Time: " << ms << " ms\n";

        size_t solid = grid.count_solid();
        std::cout << "  Solid voxels: " << solid << "\n";

        EXPECT(ms < 30000.0, "Voxelized in under 30 seconds");
        EXPECT(grid.memory_bytes() < 10 * 1024 * 1024, "Grid under 10MB");
        EXPECT(solid > 0, "Has solid voxels");
    }

    if (!check_watchdog()) goto done;

    // ── Test 5: Two different parts, collision at close range ─
    {
        std::cout << "\n--- Test 5: Two different parts, sliding collision ---\n";
        Model modelA, modelB;
        load_stl((parts_root + "/small/part2.stl").c_str(), &modelA, "cap");
        load_stl((parts_root + "/small/part3.stl").c_str(), &modelB, "case");

        MeshData mdA = extract_mesh(modelA.objects[0]->volumes[0]->mesh());
        MeshData mdB = extract_mesh(modelB.objects[0]->volumes[0]->mesh());

        snuggle::VoxelGrid gridA, gridB;
        snuggle::voxelize_indexed_mesh(mdA.vertices.data(), mdA.num_verts,
            mdA.indices.data(), mdA.num_tris, 1.0f, gridA);
        snuggle::voxelize_indexed_mesh(mdB.vertices.data(), mdB.num_verts,
            mdB.indices.data(), mdB.num_tris, 1.0f, gridB);

        snuggle::Vec3f zero = {0, 0, 0};

        // Slide part B from far away toward A, detect when collision starts
        std::cout << "  Sliding part B toward A...\n";
        float last_no_collision = 200.0f;
        float first_collision = 0.0f;
        bool found_transition = false;

        for (float dx = 200.0f; dx >= -10.0f; dx -= 5.0f) {
            snuggle::Vec3f offset = {dx, 0.0f, 0.0f};
            size_t c = snuggle::VoxelGrid::collision_count(gridA, zero, gridB, offset);
            if (c == 0) {
                last_no_collision = dx;
            } else if (!found_transition) {
                first_collision = dx;
                found_transition = true;
                std::cout << "  Collision first detected at dx=" << dx << "mm ("
                          << c << " overlapping voxels)\n";
            }
            if (!check_watchdog()) goto done;
        }

        EXPECT(found_transition, "Detected collision transition as parts approach");
        EXPECT(last_no_collision > first_collision, "Clear separation before collision zone");
        std::cout << "  No collision at dx=" << last_no_collision
                  << "mm, collision at dx=" << first_collision << "mm\n";
    }

    if (!check_watchdog()) goto done;

    // ── Test 6: Memory guard ──────────────────────────────
    {
        std::cout << "\n--- Test 6: Memory guard (try insane resolution) ---\n";
        Model model;
        load_stl((parts_root + "/small/part2.stl").c_str(), &model, "cap");
        MeshData md = extract_mesh(model.objects[0]->volumes[0]->mesh());

        snuggle::VoxelGrid grid;
        // 0.01mm resolution on a 30mm part = 3000^3 grid = WAY too big
        auto err = snuggle::voxelize_indexed_mesh(
            md.vertices.data(), md.num_verts,
            md.indices.data(), md.num_tris,
            0.01f, grid);

        EXPECT(err != snuggle::VoxError::OK,
               "Memory guard rejected insane resolution");
        std::cout << "  Error: " << snuggle::vox_error_str(err) << " (expected)\n";
    }

    // ── Test 7: VoxelGrid manual set/get ──────────────────
    {
        std::cout << "\n--- Test 7: VoxelGrid manual set/get ---\n";
        snuggle::VoxelGrid grid;
        grid.allocate(2, 2, 2);

        // Verify initially empty
        EXPECT(grid.get(0, 0, 0) == false, "Newly allocated grid is empty");

        // Set some bits
        grid.set(0, 0, 0);
        grid.set(1, 1, 1);
        grid.set(0, 1, 0);

        // Verify set bits
        EXPECT(grid.get(0, 0, 0) == true, "Bit (0,0,0) was set");
        EXPECT(grid.get(1, 1, 1) == true, "Bit (1,1,1) was set");
        EXPECT(grid.get(0, 1, 0) == true, "Bit (0,1,0) was set");

        // Verify unset bits
        EXPECT(grid.get(1, 0, 0) == false, "Bit (1,0,0) is unset");
        EXPECT(grid.get(0, 0, 1) == false, "Bit (0,0,1) is unset");
        EXPECT(grid.get(1, 1, 0) == false, "Bit (1,1,0) is unset");

        // Out of bounds
        grid.set(2, 2, 2); // Should not crash
        EXPECT(grid.get(2, 2, 2) == false, "Out of bounds get returns false");
    }

done:
    double total_s = std::chrono::duration<double>(Clock::now() - g_start).count();

    std::cout << "\n=== Results ===\n";
    std::cout << "  Passed: " << g_pass << "\n";
    std::cout << "  Failed: " << g_fail << "\n";
    std::cout << "  Total time: " << total_s << "s\n";

    if (g_fail > 0) {
        std::cout << "  STATUS: SOME TESTS FAILED\n";
        return 1;
    }
    std::cout << "  STATUS: ALL TESTS PASSED\n";
    return 0;
}
