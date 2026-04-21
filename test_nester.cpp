#include <iostream>
#include <vector>
#include <cmath>

#define private public
#include "snuggle_nester.hpp"
#undef private

int g_pass = 0, g_fail = 0;

void EXPECT(bool cond, const char* msg) {
    if (cond) {
        std::cout << "  PASS: " << msg << "\n";
        g_pass++;
    } else {
        std::cout << "  FAIL: " << msg << "\n";
        g_fail++;
    }
}

int main() {
    std::cout << "=== Snuggle Nester Test ===\n";

    snuggle::NesterConfig cfg;
    cfg.bed_width_mm = 200.0f;
    cfg.bed_height_mm = 200.0f;
    cfg.min_gap_mm = 5.0f;
    cfg.lock_rotation = false;

    snuggle::SnuggleNester nester(cfg);

    snuggle::Individual ind;
    ind.placements.resize(3);

    std::vector<snuggle::PartInfo> parts(3);

    // Part 0: small
    parts[0].hull_area_mm2 = 100.0f;
    // Part 1: large
    parts[1].hull_area_mm2 = 400.0f;
    // Part 2: medium
    parts[2].hull_area_mm2 = 256.0f;

    nester.seed_greedy_center(ind, parts);

    EXPECT(std::abs(ind.placements[1].x - 100.0f) < 0.01f, "Part 1 (large) x-coordinate centered");
    EXPECT(std::abs(ind.placements[1].y - 100.0f) < 0.01f, "Part 1 (large) y-coordinate centered");
    EXPECT(std::abs(ind.placements[2].x - 88.9391f) < 0.01f, "Part 2 (medium) x-coordinate");
    EXPECT(std::abs(ind.placements[2].y - 110.132f) < 0.01f, "Part 2 (medium) y-coordinate");
    EXPECT(std::abs(ind.placements[0].x - 102.45f) < 0.01f, "Part 0 (small) x-coordinate");
    EXPECT(std::abs(ind.placements[0].y - 72.1074f) < 0.01f, "Part 0 (small) y-coordinate");

    std::cout << "\n=== Results ===\n";
    std::cout << "  Passed: " << g_pass << "\n";
    std::cout << "  Failed: " << g_fail << "\n";

    if (g_fail > 0) {
        std::cout << "  STATUS: SOME TESTS FAILED\n";
        return 1;
    }
    std::cout << "  STATUS: ALL TESTS PASSED\n";
    return 0;
}
