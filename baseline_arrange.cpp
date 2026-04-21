// baseline_arrange.cpp — Snuggle Benchmark: Orca Convex Arrange Baseline
//
// Loads STL files, runs Orca's 2D convex hull arrangement on a simulated bed,
// measures timing and packing efficiency. This is the "before" measurement.
//
// Guardrails:
//   - Watchdog timer: kills arrange if it exceeds MAX_ARRANGE_SECONDS
//   - Per-file load timeout
//   - Memory guard: refuses to load meshes > MAX_MESH_TRIANGLES
//   - Bed bounds validation on output

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <filesystem>

// Orca / libslic3r headers
#include "libslic3r/libslic3r.h"
#include "libslic3r/Model.hpp"
#include "libslic3r/Format/STL.hpp"
#include "libslic3r/Arrange.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Geometry/ConvexHull.hpp"

namespace stdfs = std::filesystem;
using namespace Slic3r;
using Clock = std::chrono::steady_clock;

// ── Guardrail constants ────────────────────────────────────
static constexpr double  MAX_ARRANGE_SECONDS   = 30.0;   // watchdog kills arrange after this
static constexpr double  MAX_FILE_LOAD_SECONDS = 10.0;   // per-file load timeout
static constexpr size_t  MAX_MESH_TRIANGLES    = 5000000; // refuse meshes over 5M tris
static constexpr double  BED_WIDTH_MM          = 256.0;   // Bambu X1C
static constexpr double  BED_HEIGHT_MM         = 256.0;
static constexpr double  MIN_OBJ_DISTANCE_MM   = 5.0;    // 5mm gap between parts

// ── Timing helper ──────────────────────────────────────────
struct Timer {
    Clock::time_point start;
    Timer() : start(Clock::now()) {}
    double elapsed_ms() const {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }
    double elapsed_s() const { return elapsed_ms() / 1000.0; }
};

// ── Watchdog: runs arrange on a thread, kills if timeout ───
struct WatchdogResult {
    bool completed  = false;
    bool timed_out  = false;
    double time_ms  = 0.0;
};

WatchdogResult run_with_watchdog(
    std::function<void()> fn,
    double timeout_seconds)
{
    WatchdogResult result;
    std::atomic<bool> done{false};
    Timer timer;

    std::thread worker([&]() {
        fn();
        done.store(true);
    });

    // Poll for completion (100ms intervals)
    while (!done.load()) {
        if (timer.elapsed_s() > timeout_seconds) {
            result.timed_out = true;
            result.time_ms = timer.elapsed_ms();
            std::cerr << "  WATCHDOG: Operation exceeded "
                      << timeout_seconds << "s timeout!\n";
            // We can't safely kill the thread, but we flag it and detach
            worker.detach();
            return result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    worker.join();
    result.completed = true;
    result.time_ms = timer.elapsed_ms();
    return result;
}

// ── Load a single STL with guardrails ──────────────────────
bool load_stl_guarded(const std::string &path, Model &model, std::string &err) {
    Timer timer;

    // Check file exists and isn't huge
    if (!stdfs::exists(path)) {
        err = "File not found: " + path;
        return false;
    }
    auto fsize = stdfs::file_size(path);
    if (fsize > 500 * 1024 * 1024) { // 500MB
        err = "File too large: " + std::to_string(fsize / (1024*1024)) + " MB";
        return false;
    }

    bool ok = Slic3r::load_stl(path.c_str(), &model, stdfs::path(path).stem().string().c_str());
    if (!ok) {
        err = "load_stl failed";
        return false;
    }

    // Check triangle count on the just-loaded object
    ModelObject *obj = model.objects.back();
    size_t total_tris = 0;
    for (const ModelVolume *v : obj->volumes)
        total_tris += v->mesh().its.indices.size();

    if (total_tris > MAX_MESH_TRIANGLES) {
        err = "Mesh has " + std::to_string(total_tris) + " triangles (max "
              + std::to_string(MAX_MESH_TRIANGLES) + ")";
        model.delete_object(model.objects.size() - 1);
        return false;
    }

    // load_stl creates a ModelObject but NOT a ModelInstance.
    // We must add one for the arrange API to work.
    if (obj->instances.empty()) {
        auto *inst = obj->add_instance();
        // Center the part on the bed like Orca does on import.
        // First, center the mesh geometry at XY origin so the arrange
        // algorithm can place it freely within the bed bounds.
        BoundingBoxf3 bb3 = obj->bounding_box_exact();
        Vec3d center = bb3.center();
        // Offset instance so the mesh XY center lands at bed center.
        // The arrange algo will then move it from there.
        inst->set_offset(Vec3d(
            BED_WIDTH_MM / 2.0 - center.x(),
            BED_HEIGHT_MM / 2.0 - center.y(),
            -bb3.min.z()  // drop to bed surface
        ));
    }

    if (timer.elapsed_s() > MAX_FILE_LOAD_SECONDS) {
        err = "Load took " + std::to_string(timer.elapsed_s()) + "s (max "
              + std::to_string(MAX_FILE_LOAD_SECONDS) + "s)";
        return false;
    }

    return true;
}

// ── Compute packing metrics ────────────────────────────────
struct PackingMetrics {
    double bed_area_mm2;           // Total bed area
    double bounding_area_mm2;      // Bounding rect of all arranged parts
    double sum_hull_area_mm2;      // Sum of individual part convex hull areas
    double packing_efficiency;     // sum_hull_area / bounding_area (higher = tighter)
    double bed_utilization;        // sum_hull_area / bed_area
    int    parts_placed;           // How many actually fit
    int    parts_total;            // How many we tried to place
    bool   all_within_bed;         // All parts inside bed bounds
    double arrange_time_ms;        // How long arrange() took
};

PackingMetrics compute_metrics(
    const arrangement::ArrangePolygons &items,
    double bed_w_mm, double bed_h_mm,
    double arrange_time_ms)
{
    PackingMetrics m{};
    m.bed_area_mm2 = bed_w_mm * bed_h_mm;
    m.parts_total = (int)items.size();
    m.arrange_time_ms = arrange_time_ms;

    double min_x = 1e18, min_y = 1e18, max_x = -1e18, max_y = -1e18;
    double sum_hull_area = 0.0;
    int placed = 0;
    bool all_in = true;

    for (const auto &ap : items) {
        // bed_idx may stay -1 even for placed items in headless mode.
        // Check if a valid translation was assigned instead.
        bool was_placed = (ap.bed_idx >= 0) ||
                          (ap.translation.x() != 0 || ap.translation.y() != 0);
        if (!was_placed) continue;
        placed++;

        // Get the polygon points transformed by the arrangement result
        Polygon hull = ap.poly.contour;

        // Apply rotation
        if (std::abs(ap.rotation) > 1e-9) {
            hull.rotate(ap.rotation);
        }
        // Apply translation
        hull.translate(ap.translation);

        // Compute area of this hull in mm^2
        double area_scaled = std::abs(hull.area());
        double area_mm2 = area_scaled * SCALING_FACTOR * SCALING_FACTOR;
        sum_hull_area += area_mm2;

        // Update bounding rect
        BoundingBox bb = hull.bounding_box();
        double bbmin_x = unscale_(bb.min.x());
        double bbmin_y = unscale_(bb.min.y());
        double bbmax_x = unscale_(bb.max.x());
        double bbmax_y = unscale_(bb.max.y());

        min_x = std::min(min_x, bbmin_x);
        min_y = std::min(min_y, bbmin_y);
        max_x = std::max(max_x, bbmax_x);
        max_y = std::max(max_y, bbmax_y);

        // Check bed bounds
        if (bbmin_x < -0.5 || bbmin_y < -0.5 ||
            bbmax_x > bed_w_mm + 0.5 || bbmax_y > bed_h_mm + 0.5) {
            all_in = false;
        }
    }

    m.parts_placed = placed;
    m.all_within_bed = all_in;

    if (placed > 0) {
        m.bounding_area_mm2 = (max_x - min_x) * (max_y - min_y);
        m.sum_hull_area_mm2 = sum_hull_area;
        m.packing_efficiency = (m.bounding_area_mm2 > 0)
            ? (sum_hull_area / m.bounding_area_mm2) : 0.0;
        m.bed_utilization = sum_hull_area / m.bed_area_mm2;
    }

    return m;
}

// ── Print metrics ──────────────────────────────────────────
void print_metrics(const std::string &test_name, const PackingMetrics &m) {
    std::cout << "\n====== " << test_name << " ======\n";
    std::cout << "  Parts placed:      " << m.parts_placed << " / " << m.parts_total << "\n";
    std::cout << "  Arrange time:      " << m.arrange_time_ms << " ms\n";
    std::cout << "  Bed area:          " << m.bed_area_mm2 << " mm^2\n";
    std::cout << "  Bounding area:     " << m.bounding_area_mm2 << " mm^2\n";
    std::cout << "  Sum hull areas:    " << m.sum_hull_area_mm2 << " mm^2\n";
    std::cout << "  Packing efficiency:" << (m.packing_efficiency * 100.0) << "%"
              << " (hull area / bounding area)\n";
    std::cout << "  Bed utilization:   " << (m.bed_utilization * 100.0) << "%"
              << " (hull area / bed area)\n";
    std::cout << "  All within bed:    " << (m.all_within_bed ? "YES" : "NO") << "\n";

    // Per-part positions
    std::cout << "  ---\n";
}

void print_placements(const arrangement::ArrangePolygons &items) {
    for (size_t i = 0; i < items.size(); i++) {
        const auto &ap = items[i];
        bool placed = (ap.bed_idx >= 0) ||
                      (ap.translation.x() != 0 || ap.translation.y() != 0);
        if (!placed) {
            std::cout << "  Part " << i << ": NOT PLACED\n";
        } else {
            double x_mm = unscale_(ap.translation.x());
            double y_mm = unscale_(ap.translation.y());
            double rot_deg = ap.rotation * 180.0 / M_PI;
            std::cout << "  Part " << i << ": x=" << x_mm << " y=" << y_mm
                      << " rot=" << rot_deg << "deg bed=" << ap.bed_idx << "\n";
        }
    }
}

// ── Test case definition ───────────────────────────────────
struct TestCase {
    std::string name;
    std::vector<std::string> stl_paths;
};

// ── MAIN ───────────────────────────────────────────────────
int main(int argc, char **argv) {
    std::cout << "=== Snuggle Benchmark: Orca Convex Arrange Baseline ===\n";
    std::cout << "Bed: " << BED_WIDTH_MM << " x " << BED_HEIGHT_MM << " mm\n";
    std::cout << "Min gap: " << MIN_OBJ_DISTANCE_MM << " mm\n";
    std::cout << "Watchdog timeout: " << MAX_ARRANGE_SECONDS << " s\n\n";

    // Set SNUGGLE_TEST_PARTS env var or pass directory as first arg.
    std::string parts_root = "test-parts";
    if (argc > 1) parts_root = argv[1];
    const char* env = std::getenv("SNUGGLE_TEST_PARTS");
    if (env) parts_root = env;

    // ── Define test cases ──────────────────────────────────
    std::vector<TestCase> tests = {
        {
            "Test 1: Small Parts (4 files)",
            {
                parts_root + "/small/part1.stl",
                parts_root + "/small/part2.stl",
                parts_root + "/small/part3.stl",
                parts_root + "/small/part4.stl",
            }
        },
        {
            "Test 2: Mixed Sizes (5 files)",
            {
                parts_root + "/mixed/large_part.stl",
                parts_root + "/mixed/medium_part1.stl",
                parts_root + "/mixed/medium_part2.stl",
                parts_root + "/mixed/small_part1.stl",
                parts_root + "/mixed/small_part2.stl",
            }
        },
        {
            "Test 3: Complex Geometry (6 files)",
            {
                parts_root + "/complex/cylinder.stl",
                parts_root + "/complex/piston.stl",
                parts_root + "/complex/ring1.stl",
                parts_root + "/complex/ring2.stl",
                parts_root + "/complex/pin.stl",
                parts_root + "/complex/bracket.stl",
            }
        },
    };

    // ── Run each test case ─────────────────────────────────
    std::vector<PackingMetrics> all_metrics;

    for (const auto &tc : tests) {
        std::cout << "Loading: " << tc.name << "\n";

        // Load all STLs into a Model
        Model model;
        bool load_ok = true;

        for (const auto &path : tc.stl_paths) {
            std::string err;
            std::cout << "  Loading: " << stdfs::path(path).filename().string() << "... ";

            if (!load_stl_guarded(path, model, err)) {
                std::cout << "FAILED: " << err << "\n";
                load_ok = false;
                break;
            }

            // Print triangle count
            ModelObject *obj = model.objects.back();
            size_t tris = 0;
            for (const ModelVolume *v : obj->volumes)
                tris += v->mesh().its.indices.size();
            std::cout << "OK (" << tris << " tris)\n";
        }

        if (!load_ok) {
            std::cout << "  SKIPPING test due to load failure.\n\n";
            continue;
        }

        // Build ArrangePolygons from model instances
        arrangement::ArrangePolygons items;
        size_t total_instances = 0;
        for (ModelObject *obj : model.objects) {
            total_instances += obj->instances.size();
        }
        items.reserve(total_instances);

        for (ModelObject *obj : model.objects) {
            for (ModelInstance *inst : obj->instances) {
                arrangement::ArrangePolygon ap;
                inst->get_arrange_polygon(static_cast<void*>(&ap));
                items.push_back(ap);
            }
        }

        std::cout << "  Prepared " << items.size() << " arrange polygons.\n";

        // Debug: print hull info
        for (size_t i = 0; i < items.size(); i++) {
            const auto &ap = items[i];
            auto bb = ap.poly.contour.bounding_box();
            std::cout << "    Hull " << i << ": "
                      << ap.poly.contour.size() << " pts, bbox=["
                      << unscale_(bb.min.x()) << "," << unscale_(bb.min.y()) << "]-["
                      << unscale_(bb.max.x()) << "," << unscale_(bb.max.y()) << "]\n";
        }

        // Define bed as Points polygon (how Orca actually does it).
        // Orca's bed_shape for Bambu X1C is a rectangle from (0,0) to (256,256).
        Points bed = {
            Point(scale_(0.0),          scale_(0.0)),
            Point(scale_(BED_WIDTH_MM), scale_(0.0)),
            Point(scale_(BED_WIDTH_MM), scale_(BED_HEIGHT_MM)),
            Point(scale_(0.0),          scale_(BED_HEIGHT_MM)),
        };
        std::cout << "  Bed: Points polygon, [0,0]-[" << BED_WIDTH_MM << "," << BED_HEIGHT_MM << "]\n";

        // Configure arrange params
        arrangement::ArrangeParams params;
        params.min_obj_distance = scale_(MIN_OBJ_DISTANCE_MM);
        params.allow_rotations = false;  // Match Snuggle: Z-rotation only via algo
        params.accuracy = 1.0f;          // Max quality for baseline

        // Run arrange with watchdog
        std::cout << "  Running Orca arrange (convex hull, BoundingBox bed)...\n";

        arrangement::ArrangePolygons excludes; // empty

        auto wd_result = run_with_watchdog([&]() {
            arrangement::arrange(items, excludes, bed, params);
        }, MAX_ARRANGE_SECONDS);

        // Debug: check bed_idx values after arrange
        for (size_t i = 0; i < items.size(); i++) {
            std::cout << "    After arrange: item " << i << " bed_idx=" << items[i].bed_idx
                      << " trans=(" << unscale_(items[i].translation.x()) << ","
                      << unscale_(items[i].translation.y()) << ")"
                      << " rot=" << (items[i].rotation * 180.0 / M_PI) << "deg\n";
        }

        if (wd_result.timed_out) {
            std::cout << "  TIMEOUT after " << wd_result.time_ms << " ms. Skipping.\n\n";
            continue;
        }

        // Compute and print metrics
        auto metrics = compute_metrics(items, BED_WIDTH_MM, BED_HEIGHT_MM, wd_result.time_ms);
        print_metrics(tc.name, metrics);
        print_placements(items);
        all_metrics.push_back(metrics);

        std::cout << "\n";
    }

    // ── Summary ────────────────────────────────────────────
    std::cout << "\n====== SUMMARY ======\n";
    std::cout << "Test                              | Parts | Time(ms) | Efficiency | Bed Use\n";
    std::cout << "----------------------------------|-------|----------|------------|--------\n";

    for (size_t i = 0; i < all_metrics.size(); i++) {
        const auto &m = all_metrics[i];
        char line[256];
        snprintf(line, sizeof(line), "%-34s| %2d/%2d | %7.1f  |   %5.1f%%   | %5.1f%%",
                 tests[i].name.substr(0, 34).c_str(),
                 m.parts_placed, m.parts_total,
                 m.arrange_time_ms,
                 m.packing_efficiency * 100.0,
                 m.bed_utilization * 100.0);
        std::cout << line << "\n";
    }

    std::cout << "\nBaseline complete. These numbers are what Snuggle needs to beat.\n";
    return 0;
}
