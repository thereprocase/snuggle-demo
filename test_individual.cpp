#include "snuggle_nester.hpp"
#include <iostream>
#include <vector>

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
    std::cout << "=== Snuggle Individual Test ===\n";

    // Create 4 individuals
    snuggle::Individual feasible_high_fitness;
    feasible_high_fitness.collision_count = 0;
    feasible_high_fitness.oob_count = 0;
    feasible_high_fitness.fitness = 10.0f;

    snuggle::Individual feasible_low_fitness;
    feasible_low_fitness.collision_count = 0;
    feasible_low_fitness.oob_count = 0;
    feasible_low_fitness.fitness = 5.0f;

    snuggle::Individual infeasible_few_violations;
    infeasible_few_violations.collision_count = 1;
    infeasible_few_violations.oob_count = 0;
    infeasible_few_violations.fitness = 100.0f; // Fitness shouldn't matter

    snuggle::Individual infeasible_many_violations;
    infeasible_many_violations.collision_count = 2;
    infeasible_many_violations.oob_count = 1;
    infeasible_many_violations.fitness = 100.0f;

    // Test 1: Feasible beats infeasible
    EXPECT(feasible_low_fitness.is_better_than(infeasible_few_violations),
           "Feasible beats infeasible");

    // Test 2: Infeasible loses to feasible
    EXPECT(!infeasible_few_violations.is_better_than(feasible_low_fitness),
           "Infeasible loses to feasible");

    // Test 3: Infeasible vs infeasible (fewer violations wins)
    EXPECT(infeasible_few_violations.is_better_than(infeasible_many_violations),
           "Infeasible vs infeasible (fewer violations wins)");
    EXPECT(!infeasible_many_violations.is_better_than(infeasible_few_violations),
           "Infeasible vs infeasible (more violations loses)");

    // Test 4: Feasible vs feasible (higher fitness wins)
    EXPECT(feasible_high_fitness.is_better_than(feasible_low_fitness),
           "Feasible vs feasible (higher fitness wins)");
    EXPECT(!feasible_low_fitness.is_better_than(feasible_high_fitness),
           "Feasible vs feasible (lower fitness loses)");

    std::cout << "\n=== Results ===\n";
    std::cout << "  Passed: " << g_pass << "\n";
    std::cout << "  Failed: " << g_fail << "\n";

    if (g_fail > 0) return 1;
    return 0;
}
