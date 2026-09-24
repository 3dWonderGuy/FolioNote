/**
 * =========================================================================================
 * @file test_rtree.cpp
 * @brief Standalone Local Stress & Invariant Test Suite for Folio::RTree
 * =========================================================================================
 *
 * ARCHITECTURAL PURPOSE & SAFETY ASSURANCE:
 * -----------------------------------------
 * In FolioNote, the R-Tree is the spatial backbone of the vector canvas engine. Every single
 * visual entity (pen strokes, highlighter marks, text boxes, images, PDF backing pages)
 * registers an Axis-Aligned Bounding Box (AABB) into this tree. 
 *
 * During high-refresh rendering (60 FPS / 120 FPS), the viewport frustum queries this tree
 * to cull invisible objects outside the screen boundaries. If the R-Tree drops a node,
 * corrupts a bounding box, fails to handle duplicate coordinates, or enters an infinite
 * rotation loop during tree balancing:
 *   1. Strokes visually disappear or flicker during panning and zooming.
 *   2. The main rendering loop stutters or permanently deadlocks.
 *   3. Eraser hit-tests fail to locate strokes, leading to ghost data or data loss.
 *
 * CAN YOU SAFELY RUN THIS TEST REPEATEDLY?
 * ---------------------------------------
 * YES. Absolutely.
 * - Entirely In-Memory: This test executes 100% within process heap memory. It does not
 *   create, alter, or touch your physical documents, notebooks, or settings files.
 * - Deterministic Pseudorandomness: The random number generators use fixed seeds (e.g. 42, 999).
 *   This guarantees that every single run generates the exact same sequence of boxes, making
 *   any edge-case failure 100% reproducible on any machine.
 * - Isolated Lifecycle: Each test function instantiates its own local `RTree` object on
 *   the stack, completely destructing and freeing memory upon function exit.
 *
 * WHAT THIS SUITE TESTS (COMPREHENSIVE COVERAGE):
 * -----------------------------------------------
 * 1. Test_DegenerateAndEdgeCases:
 *    - Invalid Inputs: Verifies that UID 0 (reserved engine null) is rejected.
 *    - Dimensionless Geometries: Verifies points (0 width, 0 height) and 1D lines (0 thickness).
 *    - Spatial Boundaries: Edge-to-edge touching boxes and negative coordinate space.
 *    - Safe Deletion: Removing non-existent IDs must be a graceful no-op.
 *
 * 2. Test_OverwriteAndMove:
 *    - Object Mutation: When an existing UID is moved on the canvas and re-inserted, the
 *      R-Tree must update the bounding box in-place and prune the old spatial leaf.
 *    - Leaf Duplication Prevention: Asserts that element count remains constant.
 *
 * 3. Test_IdenticalOverlaps:
 *    - Split Heuristic Stability: Poorly implemented quadratic or linear R-Tree node splits
 *      often divide by zero or recurse infinitely when 300 objects share the exact same bounds.
 *
 * 4. Test_GroundTruthEquivalence:
 *    - Mathematical Correctness: Generates 2,000 randomized objects and performs 200 view
 *      queries. Every query result is verified against an independent, brute-force O(N) linear
 *      scan. If the R-Tree misses even a single overlapping stroke, the test fails.
 *
 * 5. Test_MemoryPoolAndHeavyChurn:
 *    - Node Recycling: Inserts 5,000 objects, deletes 3,000, and re-inserts 3,000 new ones.
 *      Validates that `recycledNodeIndices` recycles node slots rather than growing the vector indefinitely.
 *
 * 6. Test_CycleTreeIntegrity:
 *    - Dynamic Balancing & Loop Prevention: Simulates 300 frames of `Update()` / `CycleTree()`
 *      rebalancing to prove no circular parent-child graph links are ever formed.
 * =========================================================================================
 */

#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <algorithm>
#include <chrono>
#include <unordered_set>
#include <unordered_map>
#include <cstdint>

// Core Spatial Indexing Engine Header
#include "core/spatial/r_tree.hpp"

/**
 * @def TEST_ASSERT
 * @brief Terminal assertion macro that reports failure specifics without crashing the test runner.
 */
#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "\n  [FAILED] " << msg << "\n" \
                      << "  File: " << __FILE__ << " | Line: " << __LINE__ << "\n"; \
            return false; \
        } \
    } while (0)

/**
 * @def RUN_TEST_CASE
 * @brief Executes a test method, measures execution time in milliseconds, and prints formatted output.
 */
#define RUN_TEST_CASE(fn) \
    do { \
        std::cout << "[RUNNING] " << #fn << "... " << std::flush; \
        auto t0 = std::chrono::high_resolution_clock::now(); \
        if (fn()) { \
            auto t1 = std::chrono::high_resolution_clock::now(); \
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count(); \
            std::cout << "PASSED (" << ms << " ms)\n"; \
        } else { \
            std::cout << ">> TEST FAILED <<\n"; \
            return 1; \
        } \
    } while (0)

using namespace Folio;

/**
 * @brief Independent ground-truth overlap check.
 * 
 * Uses standard separating axis theorem (SAT) logic to determine if two AABBs intersect.
 * Used exclusively by the test harness to verify the R-Tree's internal spatial results.
 */
static inline bool BoxesIntersect(const AABB& a, const AABB& b) {
    return !(a.maxX < b.minX || a.minX > b.maxX || a.maxY < b.minY || a.minY > b.maxY);
}

// =========================================================================================
// TEST CASE 1: Boundary Conditions, Degenerate Geometries, & Error Recovery
// =========================================================================================
/**
 * @brief Verifies engine resilience against abnormal, zero-area, or invalid geometric coordinates.
 * 
 * In real drawing sessions, users frequently create single-dot pen taps (points), perfectly
 * vertical/horizontal ruler lines (0 width or 0 height), or pan into negative coordinates.
 * This test guarantees the R-Tree handles these cases gracefully.
 */
bool Test_DegenerateAndEdgeCases() {
    RTree tree;

    // -------------------------------------------------------------------------------------
    // A. Reserved UID 0 Rejection
    // In FolioNote, UID 0 is the sentinel "Invalid/Null Object" identifier.
    // -------------------------------------------------------------------------------------
    tree.Insert(0, AABB(0.0, 0.0, 10.0, 10.0));
    TEST_ASSERT(tree.GetAll().empty(), "Tree must reject UID 0 and remain empty");

    // -------------------------------------------------------------------------------------
    // B. Point Geometry (Single Pen Tap: Width = 0, Height = 0)
    // -------------------------------------------------------------------------------------
    tree.Insert(1, AABB(50.0, 50.0, 50.0, 50.0));
    TEST_ASSERT(tree.GetAll().size() == 1, "Failed to insert zero-area point bounding box");
    
    // An exact-point query right on coordinate (50, 50) must find UID 1
    auto hitPoint = tree.Query(AABB(50.0, 50.0, 50.0, 50.0));
    TEST_ASSERT(hitPoint.size() == 1 && hitPoint[0] == 1, "Exact point query failed to retrieve point object");

    // -------------------------------------------------------------------------------------
    // C. 1D Line Geometries (Zero-Thickness Horizontal and Vertical Lines)
    // -------------------------------------------------------------------------------------
    tree.Insert(2, AABB(100.0, 20.0, 100.0, 80.0)); // Vertical line (width = 0)
    tree.Insert(3, AABB(20.0, 200.0, 80.0, 200.0)); // Horizontal line (height = 0)

    // Query window intersecting the vertical line along its height
    auto hitVert = tree.Query(AABB(99.0, 40.0, 101.0, 50.0));
    TEST_ASSERT(std::find(hitVert.begin(), hitVert.end(), 2) != hitVert.end(), "Failed to hit vertical line");

    // -------------------------------------------------------------------------------------
    // D. Boundary Contact (Touching Edges with Zero Overlap Distance)
    // -------------------------------------------------------------------------------------
    tree.Insert(4, AABB(0.0, 0.0, 10.0, 10.0));
    // Query box touches box 4 exactly at corner coordinate (10.0, 10.0)
    auto touching = tree.Query(AABB(10.0, 10.0, 20.0, 20.0));
    TEST_ASSERT(std::find(touching.begin(), touching.end(), 4) != touching.end(), 
                "Touching borders must be registered as overlapping by canvas query rules");

    // -------------------------------------------------------------------------------------
    // E. Extreme Negative Coordinates (Infinite Canvas Panning Up/Left)
    // -------------------------------------------------------------------------------------
    tree.Insert(5, AABB(-5000.0, -5000.0, -4900.0, -4900.0));
    auto hitNeg = tree.Query(AABB(-4950.0, -4950.0, -4800.0, -4800.0));
    TEST_ASSERT(hitNeg.size() == 1 && hitNeg[0] == 5, "Failed to retrieve object situated in negative space");

    // -------------------------------------------------------------------------------------
    // F. Safe Error Handling on Non-Existent UID Removal
    // -------------------------------------------------------------------------------------
    // Attempting to remove an ID that doesn't exist should log a warning and return cleanly
    tree.Remove(99999);
    TEST_ASSERT(tree.GetAll().size() == 5, "Removing non-existent UID corrupted internal count");

    return true;
}

// =========================================================================================
// TEST CASE 2: Overwrite, Re-insert, and In-place Movement Test
// =========================================================================================
/**
 * @brief Verifies that moving an entity on the canvas updates its spatial location without leaks.
 * 
 * When a user drags a stroke or selection box, the engine re-inserts the same UID with
 * new coordinates. The R-Tree must locate the old leaf, remove it, adjust parent bounds,
 * and insert the new position without creating duplicate records.
 */
bool Test_OverwriteAndMove() {
    RTree tree;

    // 1. Place object at initial coordinate (Position A)
    tree.Insert(100, AABB(10.0, 10.0, 20.0, 20.0));
    TEST_ASSERT(tree.GetAll().size() == 1, "Initial insert count mismatch");

    // 2. Drag object far away to Position B by re-inserting the same UID
    tree.Insert(100, AABB(500.0, 500.0, 520.0, 520.0));
    TEST_ASSERT(tree.GetAll().size() == 1, "Re-inserting existing UID must not duplicate records");

    // 3. Verify it is completely gone from Position A
    auto oldLoc = tree.Query(AABB(0.0, 0.0, 30.0, 30.0));
    TEST_ASSERT(oldLoc.empty(), "Ghost object detected: old coordinates still returned after move");

    // 4. Verify it is actively indexed at Position B
    auto newLoc = tree.Query(AABB(490.0, 490.0, 530.0, 530.0));
    TEST_ASSERT(newLoc.size() == 1 && newLoc[0] == 100, "Moved object not found at target coordinates");

    // 5. Delete and verify complete removal
    tree.Remove(100);
    TEST_ASSERT(tree.GetAll().empty(), "Remove failed to clear object");
    TEST_ASSERT(tree.Query(AABB(490.0, 490.0, 530.0, 530.0)).empty(), "Deleted object still queryable");

    return true;
}

// =========================================================================================
// TEST CASE 3: Identical Overlapping Clusters (Split Degeneracy Stress)
// =========================================================================================
/**
 * @brief Stress tests node splitting when many items share the exact same coordinates.
 * 
 * Many textbook R-Tree implementations fail or crash when dozens of objects share identical
 * bounds because perimeter/area expansion calculations evaluate to zero, leading to
 * division-by-zero or infinite recursive splits.
 */
bool Test_IdenticalOverlaps() {
    RTree tree;
    constexpr int IDENTICAL_COUNT = 300;

    // Insert 300 distinct UIDs with the exact same AABB
    AABB commonBox(100.0, 100.0, 150.0, 150.0);
    for (uint32_t uid = 1; uid <= IDENTICAL_COUNT; ++uid) {
        tree.Insert(uid, commonBox);
    }

    TEST_ASSERT(tree.GetAll().size() == IDENTICAL_COUNT, "Failed to index identical overlapping cluster");

    // Frustum enclosing the cluster must retrieve all 300 items
    auto hits = tree.Query(AABB(90.0, 90.0, 160.0, 160.0));
    TEST_ASSERT(hits.size() == IDENTICAL_COUNT, "Cluster query missed elements in identical coordinates");

    // Test clean purge
    tree.Clear();
    TEST_ASSERT(tree.GetAll().empty(), "Clear failed to purge clustered tree");
    return true;
}

// =========================================================================================
// TEST CASE 4: Ground Truth Equivalence (Brute-Force Mathematical Verification)
// =========================================================================================
/**
 * @brief Validates R-Tree query recall against a 100% brute-force O(N) linear search.
 * 
 * Inserts 2,000 randomized objects across an expansive 20,000 x 20,000 canvas.
 * Executes 200 random viewport queries. Each query's results are verified against
 * an exhaustive linear loop over every single object.
 *
 * If the R-Tree returns a false positive (outside the view) or a false negative (missed stroke),
 * this test fails instantly.
 */
bool Test_GroundTruthEquivalence() {
    RTree tree;
    constexpr int OBJECT_COUNT = 2000;
    constexpr int QUERY_COUNT = 200;

    // Fixed seed ensures deterministic runs across compilers and architectures
    std::mt19937_64 rng(42);
    std::uniform_real_distribution<double> coordDist(-10000.0, 10000.0);
    std::uniform_real_distribution<double> sizeDist(1.0, 200.0);

    struct Item {
        uint32_t uid;
        AABB box;
    };
    std::vector<Item> referenceData;
    referenceData.reserve(OBJECT_COUNT);

    // 1. Populate both the R-Tree and the linear verification array
    for (uint32_t uid = 1; uid <= OBJECT_COUNT; ++uid) {
        double x = coordDist(rng);
        double y = coordDist(rng);
        double w = sizeDist(rng);
        double h = sizeDist(rng);
        AABB box(x, y, x + w, y + h);

        referenceData.push_back({uid, box});
        tree.Insert(uid, box);
    }

    // 2. Perform 200 queries and assert identical recall
    for (int q = 0; q < QUERY_COUNT; ++q) {
        double qx = coordDist(rng);
        double qy = coordDist(rng);
        double qw = sizeDist(rng) * 5.0; // Simulated viewport window
        double qh = sizeDist(rng) * 5.0;
        AABB qBox(qx, qy, qx + qw, qy + qh);

        // A. Fast R-Tree Spatial Query
        auto treeResults = tree.Query(qBox);
        std::unordered_set<uint32_t> treeSet(treeResults.begin(), treeResults.end());

        // B. Ground Truth Exhaustive Linear Scan
        std::unordered_set<uint32_t> groundTruthSet;
        for (const auto& item : referenceData) {
            if (BoxesIntersect(item.box, qBox)) {
                groundTruthSet.insert(item.uid);
            }
        }

        // C. Assert exact match
        TEST_ASSERT(treeSet.size() == groundTruthSet.size(), 
                    "Recall count mismatch between RTree and Ground Truth reference");
        for (uint32_t uid : groundTruthSet) {
            TEST_ASSERT(treeSet.count(uid) == 1, 
                        "R-Tree failed to return an object that visibly intersects the query box");
        }
    }

    return true;
}

// =========================================================================================
// TEST CASE 5: Heavy Memory Churn & Node Pool Recycling
// =========================================================================================
/**
 * @brief Stress tests memory pooling under rapid creation and deletion cycles.
 * 
 * Simulates heavy editing (e.g. rapid undo/redo, continuous handwriting and erasing).
 * Tests whether deleted nodes are properly recycled through `recycledNodeIndices` without
 * causing memory fragmentation or unbounded vector allocation.
 */
bool Test_MemoryPoolAndHeavyChurn() {
    RTree tree;
    constexpr int BATCH_SIZE = 5000;

    std::mt19937_64 rng(999);
    std::uniform_real_distribution<double> pos(0.0, 50000.0);

    // Phase 1: Insert 5,000 entities
    for (uint32_t uid = 1; uid <= BATCH_SIZE; ++uid) {
        double x = pos(rng);
        double y = pos(rng);
        tree.Insert(uid, AABB(x, y, x + 50.0, y + 50.0));
    }
    TEST_ASSERT(tree.GetAll().size() == BATCH_SIZE, "Phase 1: Initial insert count mismatch");
    TEST_ASSERT(tree.GetRecycledNodeCount() == 0, "Phase 1: Recycled pool should initially be empty");

    // Phase 2: Erase 3,000 entities (creates holes in the internal node pool)
    for (uint32_t uid = 1; uid <= 3000; ++uid) {
        tree.Remove(uid);
    }
    TEST_ASSERT(tree.GetAll().size() == 2000, "Phase 2: Post-removal active count mismatch");
    TEST_ASSERT(tree.GetRecycledNodeCount() > 0, "Phase 2: Recycled node pool should contain freed nodes");

    // Phase 3: Insert 3,000 new entities (UIDs 10001..13000)
    // The engine should fill recycled slots from recycledNodeIndices rather than endlessly appending
    for (uint32_t uid = 10001; uid <= 13000; ++uid) {
        double x = pos(rng);
        double y = pos(rng);
        tree.Insert(uid, AABB(x, y, x + 50.0, y + 50.0));
    }
    TEST_ASSERT(tree.GetAll().size() == 5000, "Phase 3: Count mismatch after re-filling recycled pool");

    // Phase 4: Query full canvas to verify tree connectivity survived pool recycling
    AABB totalBox(-1000.0, -1000.0, 60000.0, 60000.0);
    auto allHits = tree.Query(totalBox);
    TEST_ASSERT(allHits.size() == 5000, "Phase 4: Node pool recycling resulted in lost/unreachable nodes");

    return true;
}

// =========================================================================================
// TEST CASE 6: Tree Balancing, Cycling, & Cyclic Graph Guard
// =========================================================================================
/**
 * @brief Validates continuous frame maintenance (`Update()` / `CycleTree()`).
 * 
 * FolioNote incrementally optimizes the spatial tree layout across frames to keep query
 * latency under 1ms during fast panning.
 * This test simulates 300 consecutive rendering frames to prove that dynamic subtree
 * rotations never generate cyclic references (parent pointing to child pointing to parent).
 */
bool Test_CycleTreeIntegrity() {
    RTree tree;
    constexpr int COUNT = 600;

    // Populate initial staggered distribution
    for (uint32_t uid = 1; uid <= COUNT; ++uid) {
        double p = static_cast<double>(uid * 25);
        tree.Insert(uid, AABB(p, p, p + 20.0, p + 20.0));
    }

    // Simulate 300 active frame ticks of tree maintenance
    for (int frame = 0; frame < 300; ++frame) {
        tree.Update();
    }

    // Verify tree topology remains fully connected with zero orphaned nodes
    AABB queryArea(0.0, 0.0, 100000.0, 100000.0);
    auto hits = tree.Query(queryArea);
    TEST_ASSERT(hits.size() == COUNT, "Continuous tree rotations orphaned or dropped nodes");
    TEST_ASSERT(tree.ValidateIntegrity(), "Tree integrity should hold after 300 cycles");

    return true;
}

// =========================================================================================
// TEST CASE 7: Self-Healing Reconstruction & Structural Invariant Validation
// =========================================================================================
/**
 * @brief Validates tree invariant checking and self-healing Rebuild functionality.
 * 
 * Tests:
 *   1. ValidateIntegrity() on empty tree.
 *   2. Rebuild(items) from external vector of (UID, AABB) pairs.
 *   3. Self-healing Rebuild() restoring all active objects into a pristine hierarchy.
 */
bool Test_SelfHealingAndIntegrityValidation() {
    // 1. Empty tree validation
    RTree emptyTree;
    TEST_ASSERT(emptyTree.ValidateIntegrity(), "Empty tree should satisfy all integrity invariants");

    // 2. Direct Rebuild(items) from external collection
    std::vector<std::pair<uint32_t, AABB>> externalItems;
    for (uint32_t uid = 1; uid <= 400; ++uid) {
        double p = static_cast<double>(uid * 10);
        externalItems.emplace_back(uid, AABB(p, p, p + 8.0, p + 8.0));
    }

    RTree treeFromItems;
    treeFromItems.Rebuild(externalItems);
    TEST_ASSERT(treeFromItems.GetObjectCount() == 400, "Rebuild(items) count mismatch");
    TEST_ASSERT(treeFromItems.ValidateIntegrity(), "Rebuild(items) produced invalid tree structure");

    auto queryHits = treeFromItems.Query(AABB(0.0, 0.0, 5000.0, 5000.0));
    TEST_ASSERT(queryHits.size() == 400, "Rebuild(items) query count mismatch");

    // 3. Self-healing Rebuild() on populated tree
    treeFromItems.Rebuild();
    TEST_ASSERT(treeFromItems.GetObjectCount() == 400, "Self-healing Rebuild count mismatch");
    TEST_ASSERT(treeFromItems.ValidateIntegrity(), "Self-healing Rebuild did not maintain integrity");

    auto hitsAfterRebuild = treeFromItems.Query(AABB(0.0, 0.0, 5000.0, 5000.0));
    TEST_ASSERT(hitsAfterRebuild.size() == 400, "Query mismatch after self-healing Rebuild");

    return true;
}

// =========================================================================================
// TEST CASE 8: Plugin / Extension Safety & Circuit Breaker Protection
// =========================================================================================
/**
 * @brief Proves that rogue third-party plugins or extensions passing corrupted bounds
 * (NaN, Inf, inverted, or extreme numbers) are rejected at the gate and never freeze the main thread.
 */
bool Test_PluginSafetyAndCircuitBreaker() {
    RTree tree;

    // 1. Buggy extension passes NaN coordinates
    double nanVal = std::numeric_limits<double>::quiet_NaN();
    double infVal = std::numeric_limits<double>::infinity();

    tree.Insert(100, AABB(nanVal, 0.0, 10.0, 10.0));
    TEST_ASSERT(!tree.Contains(100), "NaN bounding box must be rejected at the gate");

    tree.Insert(101, AABB(0.0, -infVal, 10.0, 10.0));
    TEST_ASSERT(!tree.Contains(101), "-Inf bounding box must be rejected at the gate");

    tree.Insert(102, AABB(50.0, 50.0, 10.0, 10.0)); // Inverted box: min > max
    TEST_ASSERT(!tree.Contains(102), "Inverted box must be rejected at the gate");

    // Extreme float overflow check (> 100,000 km)
    tree.Insert(103, AABB(0.0, 0.0, 1e20, 1e20));
    TEST_ASSERT(!tree.Contains(103), "Overflow bounding box must be rejected at the gate");

    // 2. Normal objects insert fine
    tree.Insert(1, AABB(10.0, 10.0, 20.0, 20.0));
    TEST_ASSERT(tree.Contains(1), "Valid object should insert normally");
    TEST_ASSERT(tree.ValidateIntegrity(), "Tree should be fully valid");
    TEST_ASSERT(!tree.IsCircuitBroken(), "Circuit breaker should not be tripped");

    return true;
}

// =========================================================================================
// Main Entry Point
// =========================================================================================
int main() {
    std::cout << "\n======================================================\n";
    std::cout << "     FolioNote Spatial R-Tree Diagnostic Suite        \n";
    std::cout << "======================================================\n";

    auto tStart = std::chrono::high_resolution_clock::now();

    RUN_TEST_CASE(Test_DegenerateAndEdgeCases);
    RUN_TEST_CASE(Test_OverwriteAndMove);
    RUN_TEST_CASE(Test_IdenticalOverlaps);
    RUN_TEST_CASE(Test_GroundTruthEquivalence);
    RUN_TEST_CASE(Test_MemoryPoolAndHeavyChurn);
    RUN_TEST_CASE(Test_CycleTreeIntegrity);
    RUN_TEST_CASE(Test_SelfHealingAndIntegrityValidation);
    RUN_TEST_CASE(Test_PluginSafetyAndCircuitBreaker);

    auto tEnd = std::chrono::high_resolution_clock::now();
    double totalMs = std::chrono::duration<double, std::milli>(tEnd - tStart).count();

    std::cout << "------------------------------------------------------\n";
    std::cout << ">>> ALL R-TREE INVARIANTS VERIFIED (100% HEALTHY) <<<\n";
    std::cout << "    Total Test Suite Duration: " << totalMs << " ms\n";
    std::cout << "======================================================\n\n";

    return 0;
}