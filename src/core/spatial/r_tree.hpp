#pragma once
#include "aabb.hpp"
#include <vector>
#include <memory>
#include <algorithm>
#include <unordered_map>
#include <cstdint>
#include "utils/logger.hpp"

class CanvasObject;

/**
 * @brief Dynamic Bounding Volume Hierarchy (BVH) / R-Tree for fast 2D spatial indexing.
 * 
 * MATHEMATICAL FOUNDATION & WORKING PROCESS:
 * ------------------------------------------
 * 1. Bounding Volume Hierarchy (BVH):
 *    - Leaves represent individual canvas objects (strokes, text boxes, images, shapes),
 *      storing their unique object ID (`uid`) and their Axis-Aligned Bounding Box (AABB)
 *      `[minX, minY, maxX, maxY]`.
 *    - Internal nodes represent clusters of objects. The bounding box of an internal node
 *      is the minimum enclosing bounding box (Union) of all its children:
 *        B_parent.minX = min(B_left.minX, B_right.minX)
 *        B_parent.minY = min(B_left.minY, B_right.minY)
 *        B_parent.maxX = max(B_left.maxX, B_right.maxX)
 *        B_parent.maxY = max(B_left.maxY, B_right.maxY)
 * 
 * 2. Surface Area Heuristic (SAH) & Volume Enlargement Metric:
 *    - During leaf insertion (`InsertLeaf`), we descend from `rootIndex` to locate the optimal
 *      sibling node that minimizes total bounding volume expansion.
 *    - For each candidate child C with current area A(C), inserting a new leaf with bounds B_new
 *      results in a combined area A(Union(C, B_new)).
 *    - The insertion cost for child C is:
 *        Cost(C) = A(Union(C, B_new)) - A(C)
 *      For non-leaf children, an additional dilation penalty equal to Cost(C) is added.
 *    - The branch with the lower total cost is descended into.
 *    - Minimizing area enlargement directly minimizes false-positive subtree traversals during viewport queries.
 * 
 * 3. Fast O(1) UID Lookup Table:
 *    - `uidToNode` maps an object UID (`uint32_t`) directly to its leaf index (`int32_t`) in the node pool.
 *    - Deletion (`Remove`) and repositioning (`Update`) execute in O(1) index lookup + O(log N) tree re-linking,
 *      eliminating O(N) linear vector scans over the entire scene.
 * 
 * 4. Incremental Re-balancing & Ancestor Refitting:
 *    - `Balance(int32_t iA)` evaluates tree rotations (swapping grandchild nodes across sibling branches)
 *      whenever subtree bounding area can be strictly reduced.
 *    - Whenever a rotation mutates the bounding box of a subtree, `RefitBoundsUp` propagates bounding box
 *      shrinkages and expansions up to the root, ensuring frustum culling never drops visible objects.
 *    - `CycleTree()` incrementally re-inserts leaves over successive frames to prevent tree degradation
 *      from repeated translations and zooms.
 * 
 * 5. User data safety feature, prevents rtree corruption, on first sign rtree rebuild is forced
 */
class RTree {
private:

    /**
     * @brief A single node in the R-Tree pool. Internal nodes have children; leaves hold object UIDs.
     */
    struct Node {
        AABB ObjBounds;                                ///< Bounding box enclosing this node and all its descendants.
        uint32_t uid = 0;                              ///< Object UID. Non-zero for leaf nodes; 0 for internal nodes.
        int32_t left = -1;                             ///< Index of left child in r_tree vector (-1 if leaf, -2 if dead).
        int32_t right = -1;                            ///< Index of right child in r_tree vector (-1 if leaf, -2 if dead).
        int32_t parent = -1;                           ///< Index of parent node in r_tree vector (-1 if root, -2 if dead).

        /**
         * @brief Checks whether this node is a leaf (contains an object UID, no children).
         * @return true if both left and right child indices are -1.
         */
        [[nodiscard]] constexpr bool IsLeaf() const noexcept { 
            return left == -1 && right == -1; 
        }
        
        /**
         * @brief Checks whether this node is marked as dead (in recycledNodeIndices recycling list).
         * @return true if node has been deallocated and recycled.
         */
        [[nodiscard]] constexpr bool IsDead() const noexcept {
            return left == -2 || right == -2;
        }
    };

    /// Contiguous node pool providing cache-friendly memory layout.
    std::vector<Node> r_tree;

    /// Recycled node indices available for immediate reuse without heap allocations (LIFO free list).
    std::vector<int32_t> recycledNodeIndices; 

    /// Fast O(1) lookup mapping object UID to its leaf node index in r_tree.
    std::unordered_map<uint32_t, int32_t> uidToNode;
    
    /// Root node index (-1 if tree is empty).
    int32_t rootIndex = -1;

    /// Round-robin cursor for incremental tree maintenance in CycleTree().
    size_t cycleIndex = 0;

    /// Re-entrancy guard flag to prevent infinite recursion during ancestor refitting passes.
    bool isRefitting = false;

    /// Re-entrancy guard flag to prevent recursive rebuild loops during emergency self-healing.
    bool isRebuilding = false;

    /// Circuit breaker: tracks consecutive rebuild attempts to prevent main-thread freeze / rebuild storms.
    uint32_t consecutiveRebuilds = 0;
    static constexpr uint32_t MAX_CONSECUTIVE_REBUILDS = 3;
    bool isCircuitBroken = false;

    /**
     * @brief Computes 2D surface area of an AABB.
     *   Area = (maxX - minX) * (maxY - minY)
     * Returns 0.0 for empty or degenerate (inverted) boxes.
     * 
     * @param b Target bounding box.
     * @return Area in world units as double (>= 0.0).
     */
    [[nodiscard]] double Area(const AABB& b) const noexcept;

    /**
     * @brief Calculates the smallest AABB that encloses both input boxes.
     *   result.minX = min(a.minX, b.minX)
     *   result.minY = min(a.minY, b.minY)
     *   result.maxX = max(a.maxX, b.maxX)
     *   result.maxY = max(a.maxY, b.maxY)
     * 
     * @param a First bounding box.
     * @param b Second bounding box.
     * @return New AABB enclosing both inputs, returned by value.
     */
    [[nodiscard]] AABB Union(const AABB& a, const AABB& b) const noexcept;

    // --- Internal Tree Mechanics ---

    /**
     * @brief Allocates a node slot, preferring recycled indices from recycledNodeIndices to minimize heap reallocations.
     * 
     * @return int32_t Valid 0-based index in r_tree pool.
     */
    int32_t AllocateNode();
    
    /**
     * @brief Marks a node as dead (-2 links) and pushes index to recycledNodeIndices stack for reuse.
     * 
     * @param nodeIdx Index of node to recycle.
     */
    void FreeNode(int32_t nodeIdx);
    
    /**
     * @brief Evaluates tree rotations around node iA to minimize combined surface area.
     * 
     * Rotational Cost Metric:
     *   Evaluates swapping grandchild nodes across left (iB) and right (iC) subtrees.
     *   If a rotation strictly reduces the surface area, updates pointers and bounds.
     *   If iA has a parent, propagates updated bounds upwards via RefitBoundsUp.
     * 
     * @param iA Internal node index to balance.
     * @return int32_t The updated index of node iA.
     */
    int32_t Balance(int32_t iA);
    
    /**
     * @brief Traverses upwards from nodeIdx to rootIndex, recalculating bounding boxes
     * and applying local balancing rotations.
     * 
     * Protected by `isRefitting` flag to prevent re-entrant recursion.
     * 
     * @param nodeIdx Starting node index to ascend from.
     */
    void RefitBoundsUp(int32_t nodeIdx);
    
    /**
     * @brief Traverses tree down from root using Surface Area Heuristic (SAH) to attach a new leaf node.
     * 
     * Workflow:
     *   1. Descend greedily picking the child branch with minimum surface area expansion cost.
     *   2. Create a new internal parent node holding both sibling and leaf.
     *   3. Update old parent's child pointer (with explicit left/right check).
     *   4. Refit ancestor bounds up to rootIndex.
     * 
     * @param leafIdx Index of pre-initialized leaf node in r_tree to insert.
     */
    void InsertLeaf(int32_t leafIdx);
    
    /**
     * @brief Detaches a leaf node, collapses its parent, and promotes its sibling to grandparent.
     * 
     * @param leafIdx Index of leaf node to remove.
     */
    void RemoveLeaf(int32_t leafIdx);
    
    /**
     * @brief Cycles a small batch of leaves each frame (remove + reinsert) to incrementally
     * eliminate structural degradation and maintain optimal query depth.
     */
    void CycleTree();

public:

    RTree() = default;
    ~RTree() = default;

    // Non-copyable to prevent accidental tree duplication
    RTree(const RTree&) = delete;
    RTree& operator=(const RTree&) = delete;

    // Move-constructible and move-assignable
    RTree(RTree&&) noexcept = default;
    RTree& operator=(RTree&&) noexcept = default;

    /**
     * @brief Clears all nodes, free indices, and UID mappings, resetting the spatial index to empty state.
     */
    void Clear() noexcept;

    /**
     * @brief Inserts an object into the spatial index.
     * If uid is already present, automatically updates its bounding box cleanly.
     * 
     * @param _uid Unique identifier of object (e.g. stroke or text box UID).
     * @param targetBounds Axis-aligned bounding box of object in world coordinates.
     */
    void Insert(const uint32_t _uid, const AABB& targetBounds);

    /**
     * @brief Removes an object from the spatial index in O(1) map lookup + O(log N) tree unlinking.
     * 
     * @param _uid Unique identifier of object to remove.
     */
    void Remove(const uint32_t _uid);

    /**
     * @brief Maintenance routine: balances internal nodes and incrementally cycles leaves.
     * Call once per frame or viewport update.
     */
    void Update();

    /**
     * @brief Updates the bounding box of an existing object.
     * Executes in O(1) map lookup + O(log N) tree repositioning.
     * 
     * @param _uid Object unique identifier.
     * @param targetBounds New axis-aligned bounding box in world coordinates.
     */
    void Update(const uint32_t _uid, const AABB& targetBounds);

    /**
     * @brief Fast hierarchical spatial query: finds all objects whose bounds intersect the query area.
     * Traversal complexity: O(log N) average, skipping culled subtrees.
     * 
     * @param area Query bounding box (e.g. camera viewport in world space).
     * @return std::vector<uint32_t> Vector of object UIDs intersecting the query area.
     */
    [[nodiscard]] std::vector<uint32_t> Query(const AABB& area) const;

    /**
     * @brief Retrieves all active object UIDs registered in the index.
     * 
     * @return std::vector<uint32_t> Vector of all active object UIDs.
     */
    [[nodiscard]] std::vector<uint32_t> GetAll() const;

    /**
     * @brief Checks whether an object UID is currently registered in the tree in O(1) time.
     * 
     * @param uid Object unique identifier.
     * @return true if UID exists in uidToNode map.
     */
    [[nodiscard]] bool Contains(uint32_t uid) const noexcept {
        return uidToNode.find(uid) != uidToNode.end();
    }

    /**
     * @brief Returns total node count in the pool (including dead/recycled nodes).
     */
    [[nodiscard]] size_t GetNodeCount() const noexcept { return r_tree.size(); }

    /**
     * @brief Returns number of recycled nodes available in recycledNodeIndices stack.
     */
    [[nodiscard]] size_t GetRecycledNodeCount() const noexcept { return recycledNodeIndices.size(); }

    /**
     * @brief Backwards-compatible alias for GetRecycledNodeCount().
     */
    [[nodiscard]] size_t GetFreeCount() const noexcept { return GetRecycledNodeCount(); }

    /**
     * @brief Returns total number of active objects registered in the tree.
     */
    [[nodiscard]] size_t GetObjectCount() const noexcept { return uidToNode.size(); }

    /**
     * @brief Returns the root node index (-1 if empty).
     */
    [[nodiscard]] int32_t GetRootIndex() const noexcept { return rootIndex; }

    /**
     * @brief Checks whether the tree contains zero active objects.
     */
    [[nodiscard]] bool IsEmpty() const noexcept { return rootIndex == -1; }

    /**
     * @brief Performs complete self-healing reconstruction of the spatial index.
     * 
     * Working Process:
     *   1. Scans existing active leaf nodes and the UID map to harvest all valid (UID, AABB) pairs.
     *   2. Completely resets internal node pool, recycled indices, and UID lookup table via Clear().
     *   3. Pre-allocates vector capacity (2N nodes for N leaves) to prevent repeated dynamic reallocations.
     *   4. Re-inserts all active entities from scratch using the Surface Area Heuristic (SAH).
     * 
     * Guarantees:
     *   - Re-establishes pristine hierarchy invariant with zero dangling or cyclic links.
     *   - Runs in O(N log N) time; for typical canvases (thousands of strokes), executes in ~1-5ms.
     *   - Safe against re-entrant calls via `isRebuilding` guard.
     */
    void Rebuild();

    /**
     * @brief Rebuilds the spatial index directly from an external list of (UID, AABB) pairs.
     * 
     * Working Process:
     *   1. Completely clears existing pool and lookup structures.
     *   2. Reserves necessary pool capacity.
     *   3. Sequentially inserts each provided entity into the tree.
     * 
     * @param items Vector of pairs containing (object UID, world-space AABB).
     */
    void Rebuild(const std::vector<std::pair<uint32_t, AABB>>& items);

    /**
     * @brief Validates the structural integrity and mathematical invariants of the tree.
     * 
     * Working Process:
     *   1. If tree is empty, verifies uidToNode is also empty.
     *   2. Traverses down from rootIndex verifying:
     *      - Node indices are within valid pool range [0, r_tree.size()).
     *      - No dead nodes (-2) are present in the active hierarchy.
     *      - Every internal node has valid left/right children whose parent pointers point back to it.
     *      - Enclosing bounding box tightly fits children's bounding boxes.
     *      - Traversal depth does not exceed maxDepth (ensures acyclic graph).
     *   3. Confirms every entry in uidToNode maps to a leaf node with identical UID and bounds.
     *   4. Confirms all recycledNodeIndices are marked dead (-2).
     * 
     * @return true if all invariants hold; false if any corruption or anomaly is detected.
     */
    [[nodiscard]] bool ValidateIntegrity() const noexcept;

    /**
     * @brief Checks whether the circuit breaker is currently tripped (protecting main thread from thrashing).
     */
    [[nodiscard]] bool IsCircuitBroken() const noexcept { return isCircuitBroken; }

    /**
     * @brief Resets the circuit breaker and consecutive rebuild counters to restore normal operation.
     */
    void ResetCircuitBreaker() noexcept {
        consecutiveRebuilds = 0;
        isCircuitBroken = false;
    }
};