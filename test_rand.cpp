#include "snuggle_nester.hpp"
#include <iostream>

namespace snuggle {
class SnuggleNesterTest {
public:
    static bool test_rand() {
        NesterConfig cfg;
        SnuggleNester nester(cfg, 42); // Seeded for determinism

        float lo = -10.5f;
        float hi = 20.5f;

        bool passed = true;
        for (int i = 0; i < 1000; i++) {
            float val = nester.randf(lo, hi);
            if (val < lo || val > hi) {
                std::cout << "FAIL: randf returned " << val << " which is outside [" << lo << ", " << hi << "]\n";
                passed = false;
                break;
            }
        }

        if (passed) {
            std::cout << "PASS: randf generated 1000 values correctly within bounds\n";
        }

        size_t int_lo = 5;
        size_t int_hi = 15;
        for (int i = 0; i < 1000; i++) {
            size_t val = nester.randi(int_lo, int_hi);
            if (val < int_lo || val > int_hi) {
                std::cout << "FAIL: randi returned " << val << " which is outside [" << int_lo << ", " << int_hi << "]\n";
                passed = false;
                break;
            }
        }

        if (passed) {
            std::cout << "PASS: randi generated 1000 values correctly within bounds\n";
        }

        return passed;
    }
};
}

int main() {
    bool passed = snuggle::SnuggleNesterTest::test_rand();
    return passed ? 0 : 1;
}
