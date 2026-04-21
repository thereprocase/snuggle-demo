// gpu_bench.cpp — GPU vs CPU comparison + GPU optimization iterations
//
// Loads N real parts, evaluates arrangements on both CPU and GPU,
// reports timing side-by-side.

#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <cstring>
#include <random>

#define GLEW_STATIC
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Model.hpp"
#include "libslic3r/Format/STL.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include "polite_voxelizer.hpp"
#include "snuggle_nester.hpp"

namespace stdfs = std::filesystem;
using namespace Slic3r;
using Clock = std::chrono::steady_clock;

// ── Batched GPU shader: one dispatch evaluates ALL pairs ───
// Each workgroup handles one pair (i,j). Pair list is in a buffer.
static const char* BATCHED_SHADER = R"(
#version 430 core
layout(local_size_x = 256) in;

// All voxel grids packed into one buffer, with an offset table
layout(std430, binding = 0) readonly buffer AllGrids { uint all_grids[]; };

// Per-grid metadata: offset into all_grids, nx, ny, nz
struct GridInfo {
    int offset;  // word offset into all_grids
    int nx, ny, nz;
};
layout(std430, binding = 1) readonly buffer GridInfos { GridInfo grid_infos[]; };

// Per-pair work items: which two grids, their placements
struct PairWork {
    int grid_a, grid_b;       // indices into grid_infos
    float ax, ay;             // placement of A (voxel units)
    float bx, by;             // placement of B (voxel units)
    float cos_t, sin_t;       // rotation of B
    float center_bx, center_by; // rotation center of B
};
layout(std430, binding = 2) readonly buffer PairWorks { PairWork pairs[]; };

// Results: one uint per pair
layout(std430, binding = 3) buffer Results { uint results[]; };

uniform int num_pairs;

bool getVoxel(int grid_offset, int nx, int ny, int nz, int x, int y, int z) {
    if (x < 0 || x >= nx || y < 0 || y >= ny || z < 0 || z >= nz) return false;
    int idx = x + y * nx + z * nx * ny;
    return (all_grids[grid_offset + (idx >> 5)] & (1u << (idx & 31))) != 0u;
}

shared uint shared_count;

void main() {
    // Each workgroup handles one pair
    uint pair_idx = gl_WorkGroupID.x;
    if (pair_idx >= num_pairs) return;

    uint local_id = gl_LocalInvocationID.x;

    if (local_id == 0u) shared_count = 0u;
    barrier();

    PairWork pw = pairs[pair_idx];
    GridInfo ga = grid_infos[pw.grid_a];
    GridInfo gb = grid_infos[pw.grid_b];

    // Each thread handles a slice of grid A's voxels
    int total_columns = ga.nx * ga.ny;
    uint my_count = 0;

    for (int col = int(local_id); col < total_columns; col += 256) {
        int ax = col % ga.nx;
        int ay = col / ga.nx;

        for (int az = 0; az < ga.nz; az++) {
            if (!getVoxel(ga.offset, ga.nx, ga.ny, ga.nz, ax, ay, az)) continue;

            // World position
            float wx = float(ax) + pw.ax;
            float wy = float(ay) + pw.ay;

            // Into B's rotated frame
            float dx = wx - pw.bx - pw.center_bx;
            float dy = wy - pw.by - pw.center_by;
            float bx_local = pw.cos_t * dx + pw.sin_t * dy + pw.center_bx;
            float by_local = -pw.sin_t * dx + pw.cos_t * dy + pw.center_by;

            int bxi = int(round(bx_local));
            int byi = int(round(by_local));

            if (getVoxel(gb.offset, gb.nx, gb.ny, gb.nz, bxi, byi, az))
                my_count++;
        }
    }

    if (my_count > 0u)
        atomicAdd(shared_count, my_count);

    barrier();

    if (local_id == 0u)
        results[pair_idx] = shared_count;
}
)";

GLuint compile_compute(const char* src) {
    GLuint s = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096]; glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::cerr << "Shader error:\n" << log << "\n"; return 0;
    }
    GLuint p = glCreateProgram();
    glAttachShader(p, s); glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096]; glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::cerr << "Link error:\n" << log << "\n"; return 0;
    }
    glDeleteShader(s);
    return p;
}

struct LoadedPart {
    snuggle::VoxelGrid grid;
    std::vector<uint32_t> packed; // bit-packed for GPU
    std::string name;
};

std::vector<uint32_t> pack_grid(const snuggle::VoxelGrid &g) {
    size_t total = g.nx * g.ny * g.nz;
    size_t words = (total + 31) / 32;
    std::vector<uint32_t> packed(words, 0);
    for (size_t z = 0; z < g.nz; z++)
    for (size_t y = 0; y < g.ny; y++)
    for (size_t x = 0; x < g.nx; x++) {
        if (g.get(x, y, z)) {
            size_t idx = x + y * g.nx + z * g.nx * g.ny;
            packed[idx / 32] |= (1u << (idx % 32));
        }
    }
    return packed;
}

struct Placement { float x, y, rot; };

int main() {
    std::cout << "=== Snuggle GPU vs CPU Benchmark ===\n\n";

    // ── Init GL ───────────────────────────────────────────
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* win = glfwCreateWindow(1, 1, "", nullptr, nullptr);
    if (!win) { std::cerr << "GL 4.3 failed\n"; return 1; }
    glfwMakeContextCurrent(win);
    glewExperimental = GL_TRUE; glewInit();
    while (glGetError() != GL_NO_ERROR) {}
    std::cout << "GPU: " << glGetString(GL_RENDERER) << "\n";

    GLuint program = compile_compute(BATCHED_SHADER);
    if (!program) return 1;
    std::cout << "Batched shader: OK\n\n";

    // ── Load parts ────────────────────────────────────────
    std::vector<std::string> dirs = {
        "F:/Claude/3d-test-parts/sorted/battery-electronics/small-under-50mm",
        "F:/Claude/3d-test-parts/sorted/coffee-gaggia/small-under-50mm",
        "F:/Claude/3d-test-parts/sorted/flight-sim-gaming/small-under-50mm",
        "F:/Claude/3d-test-parts/sorted/misc/small-under-50mm",
        "F:/Claude/3d-test-parts/sorted/storage-organization/small-under-50mm",
        "F:/Claude/3d-test-parts/sorted/printer-accessories/small-under-50mm",
        "F:/Claude/3d-test-parts/sorted/kitchen-household/small-under-50mm",
        "F:/Claude/3d-test-parts/sorted/camera-photo/small-under-50mm",
        "F:/Claude/3d-test-parts/sorted/organic-decorative/small-under-50mm",
    };

    std::vector<LoadedPart> parts;
    float vs = 0.5f; // finest resolution to max GPU advantage

    for (const auto &dir : dirs) {
        if (!stdfs::exists(dir)) continue;
        for (const auto &entry : stdfs::directory_iterator(dir)) {
            auto ext = entry.path().extension().string();
            if (ext != ".stl" && ext != ".STL") continue;
            Model model;
            std::string fname = entry.path().filename().string();
            if (!load_stl(entry.path().string().c_str(), &model, fname.c_str())) continue;

            const auto &its = model.objects[0]->volumes[0]->mesh().its;
            std::vector<float> verts(its.vertices.size() * 3);
            std::vector<uint32_t> indices(its.indices.size() * 3);
            for (size_t i = 0; i < its.vertices.size(); i++) {
                verts[i*3]=its.vertices[i].x(); verts[i*3+1]=its.vertices[i].y(); verts[i*3+2]=its.vertices[i].z();
            }
            for (size_t i = 0; i < its.indices.size(); i++) {
                indices[i*3]=its.indices[i](0); indices[i*3+1]=its.indices[i](1); indices[i*3+2]=its.indices[i](2);
            }

            LoadedPart lp;
            lp.name = fname;
            auto err = snuggle::voxelize_indexed_mesh(verts.data(), its.vertices.size(),
                indices.data(), its.indices.size(), vs, lp.grid);
            if (err != snuggle::VoxError::OK) continue;
            lp.packed = pack_grid(lp.grid);
            parts.push_back(std::move(lp));
            if (parts.size() >= 35) break;
        }
        if (parts.size() >= 35) break;
    }
    std::cout << "Loaded " << parts.size() << " parts\n\n";

    size_t N = parts.size();
    size_t n_pairs = N * (N - 1) / 2;

    // ── Pack all grids into one GPU buffer ────────────────
    struct GridInfoGPU { int offset, nx, ny, nz; };
    std::vector<uint32_t> all_grids;
    std::vector<GridInfoGPU> grid_infos;

    for (auto &p : parts) {
        GridInfoGPU gi;
        gi.offset = (int)all_grids.size();
        gi.nx = (int)p.grid.nx; gi.ny = (int)p.grid.ny; gi.nz = (int)p.grid.nz;
        grid_infos.push_back(gi);
        all_grids.insert(all_grids.end(), p.packed.begin(), p.packed.end());
    }

    std::cout << "All grids packed: " << (all_grids.size() * 4 / 1024) << " KB\n";

    // Upload
    GLuint ssboGrids, ssboInfos, ssboPairs, ssboResults;
    glGenBuffers(1, &ssboGrids);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboGrids);
    glBufferData(GL_SHADER_STORAGE_BUFFER, all_grids.size() * 4, all_grids.data(), GL_STATIC_DRAW);

    glGenBuffers(1, &ssboInfos);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboInfos);
    glBufferData(GL_SHADER_STORAGE_BUFFER, grid_infos.size() * sizeof(GridInfoGPU), grid_infos.data(), GL_STATIC_DRAW);

    // ── Generate random arrangements ──────────────────────
    std::mt19937 rng(42);
    auto randf = [&](float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); };

    int n_arrangements = 200;
    std::vector<std::vector<Placement>> arrangements(n_arrangements);
    for (auto &arr : arrangements) {
        arr.resize(N);
        for (size_t i = 0; i < N; i++) {
            arr[i].x = randf(10, 240) / vs;
            arr[i].y = randf(10, 240) / vs;
            arr[i].rot = randf(0, 6.283f);
        }
    }

    // ── CPU benchmark ─────────────────────────────────────
    std::cout << "\n--- CPU: " << n_arrangements << " arrangements, " << N << " parts ---\n";
    {
        auto t0 = Clock::now();
        size_t total_col = 0;

        for (int a = 0; a < n_arrangements; a++) {
            for (size_t i = 0; i < N; i++) {
                for (size_t j = i + 1; j < N; j++) {
                    auto &pi = arrangements[a][i], &pj = arrangements[a][j];
                    snuggle::Vec3f oi = {pi.x * vs, pi.y * vs, 0};
                    snuggle::Vec3f oj = {pj.x * vs, pj.y * vs, 0};

                    // Broad-phase 2D AABB intersection check
                    float ix_min = oi.x + parts[i].grid.origin.x;
                    float ix_max = ix_min + parts[i].grid.nx * parts[i].grid.voxel_size;
                    float iy_min = oi.y + parts[i].grid.origin.y;
                    float iy_max = iy_min + parts[i].grid.ny * parts[i].grid.voxel_size;

                    float jx_min = oj.x + parts[j].grid.origin.x;
                    float jx_max = jx_min + parts[j].grid.nx * parts[j].grid.voxel_size;
                    float jy_min = oj.y + parts[j].grid.origin.y;
                    float jy_max = jy_min + parts[j].grid.ny * parts[j].grid.voxel_size;

                    if (ix_min >= jx_max || ix_max <= jx_min || iy_min >= jy_max || iy_max <= jy_min) {
                        continue;
                    }

                    // CPU collision (no rotation in CPU nester currently)
                    total_col += snuggle::VoxelGrid::collision_count(
                        parts[i].grid, oi, parts[j].grid, oj);
                }
            }
            snuggle::polite_yield();
        }

        auto t1 = Clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::cout << "  Time: " << ms << " ms\n";
        std::cout << "  Per arrangement: " << (ms / n_arrangements) << " ms\n";
        std::cout << "  Arrangements/sec: " << (n_arrangements * 1000.0 / ms) << "\n";
        std::cout << "  Total collisions: " << total_col << "\n\n";
    }

    // ── GPU benchmark (batched) ───────────────────────────
    std::cout << "--- GPU (batched): " << n_arrangements << " arrangements, " << N << " parts ---\n";

    // Pair work buffer (reused per arrangement)
    struct PairWorkGPU {
        int grid_a, grid_b;
        float ax, ay, bx, by;
        float cos_t, sin_t, center_bx, center_by;
    };
    std::vector<PairWorkGPU> pair_work(n_pairs);
    std::vector<uint32_t> results_buf(n_pairs, 0);

    glGenBuffers(1, &ssboPairs);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboPairs);
    glBufferData(GL_SHADER_STORAGE_BUFFER, n_pairs * sizeof(PairWorkGPU), nullptr, GL_DYNAMIC_DRAW);

    glGenBuffers(1, &ssboResults);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboResults);
    glBufferData(GL_SHADER_STORAGE_BUFFER, n_pairs * sizeof(uint32_t), nullptr, GL_DYNAMIC_READ);

    {
        auto t0 = Clock::now();
        size_t total_col = 0;

        for (int a = 0; a < n_arrangements; a++) {
            // Build pair work for this arrangement
            size_t pidx = 0;
            for (size_t i = 0; i < N; i++) {
                for (size_t j = i + 1; j < N; j++) {
                    auto &pi = arrangements[a][i], &pj = arrangements[a][j];
                    auto &pw = pair_work[pidx++];
                    pw.grid_a = (int)i;
                    pw.grid_b = (int)j;
                    pw.ax = pi.x; pw.ay = pi.y;
                    pw.bx = pj.x; pw.by = pj.y;
                    pw.cos_t = std::cos(pj.rot);
                    pw.sin_t = std::sin(pj.rot);
                    pw.center_bx = parts[j].grid.nx * 0.5f;
                    pw.center_by = parts[j].grid.ny * 0.5f;
                }
            }

            // Upload pair work
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboPairs);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, n_pairs * sizeof(PairWorkGPU), pair_work.data());

            // Clear results
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboResults);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, n_pairs * sizeof(uint32_t), results_buf.data());

            // Dispatch: one workgroup per pair
            glUseProgram(program);
            glUniform1i(glGetUniformLocation(program, "num_pairs"), (int)n_pairs);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssboGrids);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssboInfos);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssboPairs);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, ssboResults);

            glDispatchCompute((GLuint)n_pairs, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

            // Read results
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboResults);
            glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, n_pairs * sizeof(uint32_t), results_buf.data());

            for (size_t p = 0; p < n_pairs; p++)
                total_col += results_buf[p];
        }

        glFinish();
        auto t1 = Clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::cout << "  Time: " << ms << " ms\n";
        std::cout << "  Per arrangement: " << (ms / n_arrangements) << " ms\n";
        std::cout << "  Arrangements/sec: " << (n_arrangements * 1000.0 / ms) << "\n";
        std::cout << "  Total collisions: " << total_col << "\n";
        std::cout << "  (GPU includes rotation, CPU does not — collision counts will differ)\n\n";
    }

    // ── GPU benchmark: no readback per arrangement ────────
    std::cout << "--- GPU (batched, deferred readback): same test ---\n";
    {
        auto t0 = Clock::now();

        for (int a = 0; a < n_arrangements; a++) {
            size_t pidx = 0;
            for (size_t i = 0; i < N; i++) {
                for (size_t j = i + 1; j < N; j++) {
                    auto &pi = arrangements[a][i], &pj = arrangements[a][j];
                    auto &pw = pair_work[pidx++];
                    pw.grid_a = (int)i; pw.grid_b = (int)j;
                    pw.ax = pi.x; pw.ay = pi.y;
                    pw.bx = pj.x; pw.by = pj.y;
                    pw.cos_t = std::cos(pj.rot); pw.sin_t = std::sin(pj.rot);
                    pw.center_bx = parts[j].grid.nx * 0.5f;
                    pw.center_by = parts[j].grid.ny * 0.5f;
                }
            }

            glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboPairs);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, n_pairs * sizeof(PairWorkGPU), pair_work.data());

            glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboResults);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, n_pairs * sizeof(uint32_t), results_buf.data());

            glUseProgram(program);
            glUniform1i(glGetUniformLocation(program, "num_pairs"), (int)n_pairs);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssboGrids);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssboInfos);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssboPairs);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, ssboResults);

            glDispatchCompute((GLuint)n_pairs, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            // NO readback — just keep dispatching
        }

        // One final readback
        glFinish();
        auto t1 = Clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::cout << "  Time: " << ms << " ms\n";
        std::cout << "  Per arrangement: " << (ms / n_arrangements) << " ms\n";
        std::cout << "  Arrangements/sec: " << (n_arrangements * 1000.0 / ms) << "\n\n";
    }

    // Cleanup
    glDeleteBuffers(1, &ssboGrids);
    glDeleteBuffers(1, &ssboInfos);
    glDeleteBuffers(1, &ssboPairs);
    glDeleteBuffers(1, &ssboResults);
    glDeleteProgram(program);
    glfwDestroyWindow(win);
    glfwTerminate();

    std::cout << "=== Done ===\n";
    return 0;
}
