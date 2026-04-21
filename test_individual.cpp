// test_individual.cpp — Unit tests for snuggle::Individual

#include <iostream>
#include "snuggle_nester.hpp"

// Simple mock for Slic3r namespace
namespace Slic3r {
}

using namespace snuggle;

int g_pass = 0;
int g_fail = 0;

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
    std::cout << "=== Snuggle Individual Test ===\n";

    Individual ind;

    // Test 1: Feasible individual
    ind.collision_count = 0;
    ind.oob_count = 0;
    EXPECT(ind.is_feasible(), "Individual with 0 collisions and 0 oob should be feasible");

    // Test 2: Infeasible individual (collisions)
    ind.collision_count = 1;
    ind.oob_count = 0;
    EXPECT(!ind.is_feasible(), "Individual with 1 collision and 0 oob should not be feasible");

    // Test 3: Infeasible individual (out of bounds)
    ind.collision_count = 0;
    ind.oob_count = 1;
    EXPECT(!ind.is_feasible(), "Individual with 0 collisions and 1 oob should not be feasible");

    // Test 4: Infeasible individual (both violations)
    ind.collision_count = 1;
    ind.oob_count = 1;
    EXPECT(!ind.is_feasible(), "Individual with 1 collision and 1 oob should not be feasible");

    std::cout << "\n=== Results ===\n";
    std::cout << "  Passed: " << g_pass << "\n";
    std::cout << "  Failed: " << g_fail << "\n";

    return g_fail > 0 ? 1 : 0;
}
