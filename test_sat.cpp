#include "polite_voxelizer.hpp"
#include <iostream>
#include <cmath>

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

int main() {
    using namespace snuggle;

    // The AABB is centered at the origin for this test
    Vec3f half(1.0f, 1.0f, 1.0f);

    std::cout << "--- Test triangle_aabb_overlap ---\n";

    // Case 1: Intersecting triangle
    Vec3f v0(0.0f, 0.0f, 0.0f);
    Vec3f v1(2.0f, 0.0f, 0.0f);
    Vec3f v2(0.0f, 2.0f, 0.0f);
    EXPECT(detail::triangle_aabb_overlap(v0, v1, v2, half) == true, "Intersecting triangle");

    // Case 2: Disjoint triangle (outside x max)
    v0 = Vec3f(2.0f, 0.0f, 0.0f);
    v1 = Vec3f(3.0f, 0.0f, 0.0f);
    v2 = Vec3f(2.0f, 1.0f, 0.0f);
    EXPECT(detail::triangle_aabb_overlap(v0, v1, v2, half) == false, "Disjoint triangle (outside x max)");

    // Case 3: Disjoint triangle (outside x min)
    v0 = Vec3f(-2.0f, 0.0f, 0.0f);
    v1 = Vec3f(-3.0f, 0.0f, 0.0f);
    v2 = Vec3f(-2.0f, 1.0f, 0.0f);
    EXPECT(detail::triangle_aabb_overlap(v0, v1, v2, half) == false, "Disjoint triangle (outside x min)");

    // Case 4: Touching edge
    v0 = Vec3f(1.0f, -2.0f, 0.0f);
    v1 = Vec3f(1.0f, 2.0f, 0.0f);
    v2 = Vec3f(3.0f, 0.0f, 0.0f);
    EXPECT(detail::triangle_aabb_overlap(v0, v1, v2, half) == true, "Touching edge");

    // Case 5: Touching vertex
    v0 = Vec3f(1.0f, 1.0f, 1.0f);
    v1 = Vec3f(2.0f, 1.0f, 1.0f);
    v2 = Vec3f(1.0f, 2.0f, 1.0f);
    EXPECT(detail::triangle_aabb_overlap(v0, v1, v2, half) == true, "Touching vertex");

    // Case 6: Completely inside
    v0 = Vec3f(0.0f, 0.0f, 0.0f);
    v1 = Vec3f(0.5f, 0.0f, 0.0f);
    v2 = Vec3f(0.0f, 0.5f, 0.0f);
    EXPECT(detail::triangle_aabb_overlap(v0, v1, v2, half) == true, "Completely inside");

    // Case 7: Larger triangle containing the box
    v0 = Vec3f(-5.0f, -5.0f, 0.0f);
    v1 = Vec3f(5.0f, -5.0f, 0.0f);
    v2 = Vec3f(0.0f, 5.0f, 0.0f);
    EXPECT(detail::triangle_aabb_overlap(v0, v1, v2, half) == true, "Triangle containing the box");

    if (g_fail > 0) {
        std::cout << "TESTS FAILED: " << g_fail << " failures\n";
        return 1;
    }

    std::cout << "ALL TESTS PASSED\n";
    return 0;
}
