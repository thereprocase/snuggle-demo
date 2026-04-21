#include <iostream>
#include <vector>

#include "snuggle_nester.hpp"

using namespace snuggle;

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
    std::cout << "=== Snuggle Crossover Unit Test ===\n";

    {
        std::cout << "\n--- Test 1: crossover() inherits mostly from fitter ---\n";

        NesterConfig cfg;
        SnuggleNester nester(cfg, 42); // Deterministic seed

        Individual parentA, parentB;
        size_t n_parts = 1000; // Large number to get stable statistics
        parentA.placements.resize(n_parts);
        parentB.placements.resize(n_parts);

        for(size_t i=0; i<n_parts; ++i) {
            parentA.placements[i] = {1.0f, 1.0f, 0.0f}; // Fitter values
            parentB.placements[i] = {2.0f, 2.0f, 0.0f}; // Other values
        }

        // Setup A to be definitively fitter
        parentA.collision_count = 0; parentA.oob_count = 0; parentA.fitness = 100.0f;
        parentB.collision_count = 0; parentB.oob_count = 0; parentB.fitness = 10.0f;

        Individual child;
        child.placements.resize(n_parts);

        nester.crossover(parentA, parentB, child, n_parts);

        size_t fromA = 0;
        size_t fromB = 0;
        for(size_t i=0; i<n_parts; ++i) {
            if(child.placements[i].x == 1.0f) fromA++;
            else if(child.placements[i].x == 2.0f) fromB++;
        }

        std::cout << "  Inherited from Fitter (A): " << fromA << "\n";
        std::cout << "  Inherited from Other  (B): " << fromB << "\n";

        EXPECT(fromA + fromB == n_parts, "All parts came from either A or B");
        EXPECT(fromB > 250 && fromB < 350, "Roughly 30% came from the other parent");
    }

    {
        std::cout << "\n--- Test 2: crossover() respects is_better_than logic (A infeasible vs B feasible) ---\n";

        NesterConfig cfg;
        SnuggleNester nester(cfg, 123);

        Individual parentA, parentB;
        size_t n_parts = 1000;
        parentA.placements.resize(n_parts);
        parentB.placements.resize(n_parts);

        for(size_t i=0; i<n_parts; ++i) {
            parentA.placements[i] = {1.0f, 1.0f, 0.0f};
            parentB.placements[i] = {2.0f, 2.0f, 0.0f}; // B should be fitter
        }

        // Setup A to be infeasible, B to be feasible
        parentA.collision_count = 1; parentA.oob_count = 0; parentA.fitness = 1000.0f; // High fitness but infeasible
        parentB.collision_count = 0; parentB.oob_count = 0; parentB.fitness = 10.0f;

        Individual child;
        child.placements.resize(n_parts);

        nester.crossover(parentA, parentB, child, n_parts);

        size_t fromA = 0;
        size_t fromB = 0;
        for(size_t i=0; i<n_parts; ++i) {
            if(child.placements[i].x == 1.0f) fromA++;
            else if(child.placements[i].x == 2.0f) fromB++;
        }

        std::cout << "  Inherited from Other  (A): " << fromA << "\n";
        std::cout << "  Inherited from Fitter (B): " << fromB << "\n";

        EXPECT(fromA + fromB == n_parts, "All parts came from either A or B");
        EXPECT(fromA > 250 && fromA < 350, "Roughly 30% came from the other parent (A)");
        EXPECT(fromB > 650 && fromB < 750, "Roughly 70% came from the fitter parent (B)");
    }

    {
        std::cout << "\n--- Test 3: crossover() degrees of infeasibility (A slightly vs B heavily infeasible) ---\n";

        NesterConfig cfg;
        SnuggleNester nester(cfg, 999);

        Individual parentA, parentB;
        size_t n_parts = 1000;
        parentA.placements.resize(n_parts);
        parentB.placements.resize(n_parts);

        for(size_t i=0; i<n_parts; ++i) {
            parentA.placements[i] = {1.0f, 1.0f, 0.0f}; // A should be fitter (less infeasible)
            parentB.placements[i] = {2.0f, 2.0f, 0.0f};
        }

        // Setup A to be less infeasible
        parentA.collision_count = 5; parentA.oob_count = 2; parentA.fitness = 0.0f;
        parentB.collision_count = 20; parentB.oob_count = 5; parentB.fitness = 0.0f;

        Individual child;
        child.placements.resize(n_parts);

        nester.crossover(parentA, parentB, child, n_parts);

        size_t fromA = 0;
        size_t fromB = 0;
        for(size_t i=0; i<n_parts; ++i) {
            if(child.placements[i].x == 1.0f) fromA++;
            else if(child.placements[i].x == 2.0f) fromB++;
        }

        std::cout << "  Inherited from Fitter (A): " << fromA << "\n";
        std::cout << "  Inherited from Other  (B): " << fromB << "\n";

        EXPECT(fromA + fromB == n_parts, "All parts came from either A or B");
        EXPECT(fromA > 650 && fromA < 750, "Roughly 70% came from the fitter parent (A)");
        EXPECT(fromB > 250 && fromB < 350, "Roughly 30% came from the other parent (B)");
    }

    return g_fail > 0 ? 1 : 0;
}
