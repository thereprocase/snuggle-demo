// test_seed_greedy_line.cpp — Unit test for seed_greedy_line

#include <iostream>
#include <vector>
#include <cmath>

#define private public
#define protected public
#include "snuggle_nester.hpp"

// Utility macro for tests
#define EXPECT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "  FAIL: " << msg << std::endl; \
            return 1; \
        } else { \
            std::cout << "  PASS: " << msg << std::endl; \
        } \
    } while(0)

int main() {
    std::cout << "=== Snuggle Nester seed_greedy_line Test ===\n";

    snuggle::NesterConfig cfg;
    cfg.bed_width_mm = 256.0f;
    cfg.bed_height_mm = 256.0f;
    cfg.min_gap_mm = 5.0f;
    cfg.lock_rotation = false;

    snuggle::SnuggleNester nester(cfg);

    snuggle::Individual ind;
    std::vector<snuggle::PartInfo> parts(4);

    for (int i = 0; i < 4; ++i) {
        parts[i].grid.voxel_size = 1.0f;
        parts[i].initial_zrot = 0.0f;
    }

    parts[0].grid.nx = 50;  // Width 50
    parts[1].grid.nx = 100; // Width 100
    parts[2].grid.nx = 150; // Width 150
    parts[3].grid.nx = 50;  // Width 50

    ind.placements.resize(4);

    nester.seed_greedy_line(ind, parts);

    EXPECT(std::abs(ind.placements[0].x - 30.0f) < 1e-4, "Part 0 x placement (5.0 gap + 25.0 half-width)");
    EXPECT(std::abs(ind.placements[0].y - 128.0f) < 1e-4, "Part 0 y placement (center line)");

    EXPECT(std::abs(ind.placements[1].x - 110.0f) < 1e-4, "Part 1 x placement (5+50+5 + 50 half-width)");
    EXPECT(std::abs(ind.placements[1].y - 128.0f) < 1e-4, "Part 1 y placement");

    EXPECT(std::abs(ind.placements[2].x - 240.0f) < 1e-4, "Part 2 x placement (60+100+5 + 75 half-width)");
    EXPECT(std::abs(ind.placements[2].y - 128.0f) < 1e-4, "Part 2 y placement");

    // Part 2 places cursor_x at 165 + 150 + 5 = 320, which triggers wrap-around (> 246)
    // Next line y = 128 + 40 = 168
    // Next x = 5.0 + 25.0 = 30.0
    EXPECT(std::abs(ind.placements[3].x - 30.0f) < 1e-4, "Part 3 x placement (wrapped to next line)");
    EXPECT(std::abs(ind.placements[3].y - 168.0f) < 1e-4, "Part 3 y placement (wrapped to next line)");

    std::cout << "All assertions passed!\n";
    return 0;
}
