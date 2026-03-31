// gpu_poc.cpp — Snuggle GPU Compute Shader Proof of Concept
//
// Proves we can:
//   1. Create an OpenGL 4.3+ context (hidden window)
//   2. Compile a compute shader
//   3. Upload voxel data to GPU via SSBO
//   4. Run collision detection on GPU
//   5. Read results back
//
// This is NOT the full Snuggle GPU pipeline. It's a "can we even do this" test.

#include <iostream>
#include <vector>
#include <chrono>
#include <cstring>

#define GLEW_STATIC
#include <GL/glew.h>
#include <GLFW/glfw3.h>

using Clock = std::chrono::steady_clock;

// ── Compute shader: pairwise voxel collision detection ────
// Two 3D voxel grids (bit-packed as uint32s) at different XY offsets.
// GPU counts how many voxels overlap.
static const char* COLLISION_SHADER = R"(
#version 430 core
layout(local_size_x = 64) in;

// Grid A and B: bit-packed voxel data (1 bit per voxel, packed into uint32s)
layout(std430, binding = 0) readonly buffer GridA { uint gridA[]; };
layout(std430, binding = 1) readonly buffer GridB { uint gridB[]; };
layout(std430, binding = 2) buffer Result { uint collision_count; };

uniform int grid_size;    // total uint32s per grid (nx*ny*nz / 32)
uniform int offset_words; // XY offset of grid B relative to A, in uint32 units

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= grid_size) return;

    // Shifted index into grid B
    int b_idx = int(idx) - offset_words;
    if (b_idx < 0 || b_idx >= grid_size) return;

    // AND the two grids — any set bit = collision
    uint overlap = gridA[idx] & gridB[b_idx];

    // Count bits (popcount)
    uint bits = overlap;
    bits = bits - ((bits >> 1) & 0x55555555u);
    bits = (bits & 0x33333333u) + ((bits >> 2) & 0x33333333u);
    bits = (bits + (bits >> 4)) & 0x0F0F0F0Fu;
    uint count = (bits * 0x01010101u) >> 24;

    if (count > 0) {
        atomicAdd(collision_count, count);
    }
}
)";

// ── Helper: check GL errors ───────────────────────────────
void check_gl(const char* label) {
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::cerr << "GL ERROR at " << label << ": 0x" << std::hex << err << std::dec << "\n";
    }
}

// ── Helper: compile shader ────────────────────────────────
GLuint compile_compute_shader(const char* source) {
    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << "Shader compile error:\n" << log << "\n";
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, shader);
    glLinkProgram(program);

    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::cerr << "Program link error:\n" << log << "\n";
        return 0;
    }

    glDeleteShader(shader);
    return program;
}

int main() {
    std::cout << "=== Snuggle GPU Compute PoC ===\n\n";

    // ── 1. Create OpenGL context ──────────────────────────
    std::cout << "Step 1: Creating OpenGL context...\n";

    if (!glfwInit()) {
        std::cerr << "GLFW init failed!\n";
        return 1;
    }

    // Request OpenGL 4.3 (minimum for compute shaders)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); // Hidden window — headless compute

    GLFWwindow* window = glfwCreateWindow(1, 1, "Snuggle GPU PoC", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GL 4.3 context!\n";
        std::cerr << "Your GPU may not support OpenGL 4.3 compute shaders.\n";
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);

    // Init GLEW
    glewExperimental = GL_TRUE;
    GLenum glewErr = glewInit();
    if (glewErr != GLEW_OK) {
        std::cerr << "GLEW init failed: " << glewGetErrorString(glewErr) << "\n";
        return 1;
    }
    // Clear the GL error from glewInit (known GLEW bug)
    glGetError();

    std::cout << "  GL Vendor:   " << glGetString(GL_VENDOR) << "\n";
    std::cout << "  GL Renderer: " << glGetString(GL_RENDERER) << "\n";
    std::cout << "  GL Version:  " << glGetString(GL_VERSION) << "\n";

    // Check compute shader support
    if (!GLEW_ARB_compute_shader) {
        std::cerr << "  Compute shaders NOT supported!\n";
        return 1;
    }
    std::cout << "  Compute shaders: SUPPORTED\n";

    GLint maxWorkGroupSize[3], maxWorkGroupCount[3], maxSSBOSize;
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 0, &maxWorkGroupSize[0]);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &maxWorkGroupCount[0]);
    glGetIntegerv(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &maxSSBOSize);
    std::cout << "  Max work group size: " << maxWorkGroupSize[0] << "\n";
    std::cout << "  Max SSBO size: " << (maxSSBOSize / (1024*1024)) << " MB\n\n";

    // ── 2. Compile compute shader ─────────────────────────
    std::cout << "Step 2: Compiling compute shader...\n";
    GLuint program = compile_compute_shader(COLLISION_SHADER);
    if (!program) {
        std::cerr << "  Shader compilation FAILED!\n";
        return 1;
    }
    std::cout << "  Shader compiled and linked OK.\n\n";

    // ── 3. Create test voxel data ─────────────────────────
    std::cout << "Step 3: Creating test voxel data...\n";

    // Simulate two 64x64x64 voxel grids (bit-packed)
    // 64^3 = 262144 voxels = 8192 uint32s per grid
    const int NX = 64, NY = 64, NZ = 64;
    const int total_voxels = NX * NY * NZ;
    const int grid_words = total_voxels / 32;

    std::vector<uint32_t> gridA(grid_words, 0);
    std::vector<uint32_t> gridB(grid_words, 0);

    // Fill grid A: solid sphere in center
    for (int z = 0; z < NZ; z++)
    for (int y = 0; y < NY; y++)
    for (int x = 0; x < NX; x++) {
        float dx = x - NX/2.0f, dy = y - NY/2.0f, dz = z - NZ/2.0f;
        if (dx*dx + dy*dy + dz*dz < 20.0f*20.0f) {
            int idx = x + y * NX + z * NX * NY;
            gridA[idx / 32] |= (1u << (idx % 32));
        }
    }

    // Fill grid B: same sphere (will be offset to test collision)
    std::memcpy(gridB.data(), gridA.data(), gridA.size() * sizeof(uint32_t));

    // Count solid voxels in A (CPU reference)
    size_t solidA = 0;
    for (auto w : gridA) {
        uint32_t b = w;
        while (b) { solidA += b & 1; b >>= 1; }
    }
    std::cout << "  Grid size: " << NX << "x" << NY << "x" << NZ
              << " = " << total_voxels << " voxels\n";
    std::cout << "  Grid memory: " << (grid_words * 4) << " bytes per grid\n";
    std::cout << "  Solid voxels in sphere: " << solidA << "\n\n";

    // ── 4. Upload to GPU ──────────────────────────────────
    std::cout << "Step 4: Uploading to GPU...\n";

    GLuint ssboA, ssboB, ssboResult;

    glGenBuffers(1, &ssboA);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboA);
    glBufferData(GL_SHADER_STORAGE_BUFFER, grid_words * sizeof(uint32_t),
                 gridA.data(), GL_STATIC_DRAW);

    glGenBuffers(1, &ssboB);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboB);
    glBufferData(GL_SHADER_STORAGE_BUFFER, grid_words * sizeof(uint32_t),
                 gridB.data(), GL_STATIC_DRAW);

    glGenBuffers(1, &ssboResult);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboResult);
    uint32_t zero = 0;
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(uint32_t), &zero, GL_DYNAMIC_READ);

    check_gl("buffer upload");
    std::cout << "  Uploaded 2 grids + result buffer.\n\n";

    // ── 5. Run collision detection ────────────────────────
    std::cout << "Step 5: Running GPU collision detection...\n";

    // Test A: Same position (full overlap)
    {
        // Reset result
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboResult);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(uint32_t), &zero);

        glUseProgram(program);
        glUniform1i(glGetUniformLocation(program, "grid_size"), grid_words);
        glUniform1i(glGetUniformLocation(program, "offset_words"), 0); // same position

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssboA);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssboB);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssboResult);

        auto t0 = Clock::now();

        // Dispatch: grid_words / 64 workgroups
        glDispatchCompute((grid_words + 63) / 64, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        // Read back result
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboResult);
        uint32_t gpu_collisions = 0;
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(uint32_t), &gpu_collisions);

        auto t1 = Clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        std::cout << "  Test A (same position): " << gpu_collisions
                  << " collisions (" << ms << " ms)\n";
        std::cout << "  Expected: " << solidA << " collisions\n";
        std::cout << "  " << (gpu_collisions == solidA ? "PASS" : "FAIL") << "\n\n";
    }

    // Test B: Offset by half the grid (partial overlap)
    {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboResult);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(uint32_t), &zero);

        glUseProgram(program);
        glUniform1i(glGetUniformLocation(program, "grid_size"), grid_words);
        // Offset by NX/2 voxels = NX/2/32 words along X
        int offset = (NX / 2) / 32;  // 1 word = 32 voxels along X
        glUniform1i(glGetUniformLocation(program, "offset_words"), offset);

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssboA);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssboB);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssboResult);

        auto t0 = Clock::now();
        glDispatchCompute((grid_words + 63) / 64, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboResult);
        uint32_t gpu_collisions = 0;
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(uint32_t), &gpu_collisions);

        auto t1 = Clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        std::cout << "  Test B (offset " << offset << " words): "
                  << gpu_collisions << " collisions (" << ms << " ms)\n";
        std::cout << "  Expected: less than " << solidA << " (partial overlap)\n";
        std::cout << "  " << (gpu_collisions > 0 && gpu_collisions < solidA ? "PASS" : "FAIL") << "\n\n";
    }

    // Test C: Offset by entire grid (no overlap)
    {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboResult);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(uint32_t), &zero);

        glUseProgram(program);
        glUniform1i(glGetUniformLocation(program, "grid_size"), grid_words);
        glUniform1i(glGetUniformLocation(program, "offset_words"), grid_words); // way past the end

        auto t0 = Clock::now();
        glDispatchCompute((grid_words + 63) / 64, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboResult);
        uint32_t gpu_collisions = 0;
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(uint32_t), &gpu_collisions);

        auto t1 = Clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        std::cout << "  Test C (no overlap): " << gpu_collisions
                  << " collisions (" << ms << " ms)\n";
        std::cout << "  Expected: 0\n";
        std::cout << "  " << (gpu_collisions == 0 ? "PASS" : "FAIL") << "\n\n";
    }

    // ── 6. Benchmark: many dispatches ─────────────────────
    std::cout << "Step 6: Benchmark — 1000 collision checks...\n";
    {
        auto t0 = Clock::now();
        for (int i = 0; i < 1000; i++) {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboResult);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(uint32_t), &zero);

            glUseProgram(program);
            glUniform1i(glGetUniformLocation(program, "grid_size"), grid_words);
            glUniform1i(glGetUniformLocation(program, "offset_words"), i % grid_words);

            glDispatchCompute((grid_words + 63) / 64, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }
        // Force GPU to finish
        glFinish();
        auto t1 = Clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        std::cout << "  1000 dispatches in " << ms << " ms\n";
        std::cout << "  " << (ms / 1000.0) << " ms per collision check\n";
        std::cout << "  " << (1000000.0 / ms) << " collision checks per second\n\n";
    }

    // Cleanup
    glDeleteBuffers(1, &ssboA);
    glDeleteBuffers(1, &ssboB);
    glDeleteBuffers(1, &ssboResult);
    glDeleteProgram(program);
    glfwDestroyWindow(window);
    glfwTerminate();

    std::cout << "=== GPU PoC Complete ===\n";
    return 0;
}
