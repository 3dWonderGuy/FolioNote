#pragma once
#include "aabb.hpp"
#include <vector>
#include <memory>
#include <algorithm>
#include "utils/Logger.hpp"

class CanvasObject;

/**
 * @brief A dynamic bounding-volume hierarchy (BVH) / R-Tree implementation for spatial indexing.
 * 
 * WHAT IT IS FOR:
 * This R-Tree is a spatial data structure designed to quickly find which objects (like strokes
 * or images) are currently visible on the screen. Instead of looping over millions of strokes
 * every frame to check if they are inside the viewport (which would be extremely slow), the R-Tree 
 * groups nearby objects into larger bounding boxes. 
 * 
 * HOW IT WORKS:
 * - Leaf Nodes: Represent the actual objects (e.g., ink strokes) and store their UID and bounding box.
 * - Internal Nodes: Represent groups of objects. Their bounding box is the union (enclosing box) 
 *   of all their children's bounding boxes.
 * 
 * When querying the tree (e.g., "what's visible on screen?"), we check the root node. If the screen
 * intersects the root, we check its children. If a child's box doesn't intersect the screen, we completely 
 * ignore it (and all of its descendants), instantly culling thousands of unseen objects.
 * 
 * MEMORY MANAGEMENT:
 * The tree nodes are stored in a contiguous `std::vector` (`r_tree`). This avoids cache misses and the 
 * overhead of allocating individual nodes on the heap. When nodes are deleted, their indices are added to 
 * `freeIndices` so they can be recycled later.
 */
class RTree {
private:

    // A single node in the R-Tree. Can be either an internal branch or a leaf.
    struct Node {
        AABB ObjBounds;                                // The bounding box [minX, minY, maxX, maxY] enclosing this node and all descendants
        uint32_t uid = 0;                              // The object UID (e.g. stroke ID). Only valid for leaf nodes. 0 for internal nodes.
        int32_t left = -1;                             // Index of the left child node in the r_tree vector.
        int32_t right = -1;                            // Index of the right child node in the r_tree vector.
        int32_t parent = -1;                           // Index of the parent node in the r_tree vector.

        // Helper function: A leaf node has no children. It holds the actual object UID.
        constexpr bool IsLeaf() const noexcept { 
            return left == -1 && right == -1; 
        }
        
        // Helper function: A dead node is one that has been deleted and is sitting in the free/recycled list.
        constexpr bool IsDead() const noexcept {
            return left == -2 || right == -2;
        }
    };

    // The flat pool of nodes. Storing them in a vector provides excellent CPU cache locality compared to pointers.
    std::vector<Node> r_tree;

    // A stack of node indices that have been deleted and can be reused for new nodes.
    std::vector<int> freeIndices; 
    
    // The index of the root node of the tree. -1 means the tree is empty.
    int rootIndex = -1;

    // A counter used by CycleTree() to slowly re-insert leaves over multiple frames to incrementally optimize the tree.
    size_t cycleIndex = 0;

    /**
     * @brief Calculates area of an AABB. Used to determine the "cost" of placing a node in a specific branch.
     */
    double Area(const AABB& b) const noexcept;

    /**
     * @brief Computes the union (bounding box that encloses both) of two AABBs.
     */
    AABB Union(const AABB& a, const AABB& b) const noexcept;

    // --- Internal Tree Mechanics ---

    // Pulls a recycled node index from freeIndices, or pushes a new Node onto the vector if none are free.
    int AllocateNode();
    
    // Marks a node as dead and adds its index to freeIndices for recycling.
    void FreeNode(int nodeIdx);
    
    // Checks if the tree can be locally optimized by swapping children around to reduce overlapping bounding boxes.
    int Balance(int nodeIdx);
    
    // Walks up the tree from a node to the root, expanding bounding boxes to ensure parents always fully enclose their children.
    void RefitBoundsUp(int nodeIdx);
    
    // The core insertion logic: traverses down the tree to find the best place to put a new leaf to minimize volume expansion.
    void InsertLeaf(int leafIdx);
    
    // The core removal logic: removes a leaf and patches the hole left behind.
    void RemoveLeaf(int leafIdx);
    
    // Incrementally removes and re-inserts a few leaves to fix degrading tree quality over time (especially after many moves).
    void CycleTree();

public:

    /**
     * @brief Wipes the tree completely empty.
     */
    void Clear() noexcept;

    /**
     * @brief Inserts a new object into the spatial index.
     * @param _uid Unique identifier for the object (e.g. stroke ID).
     * @param targetBounds The physical bounding box of the object.
     */
    void Insert(const uint32_t _uid, const AABB& targetBounds);

    /**
     * @brief Removes an object from the spatial index.
     * @param _uid The UID of the object to remove.
     */
    void Remove(const uint32_t _uid);

    /**
     * @brief Maintenance function. Should be called periodically (e.g. every frame or tick).
     * Applies rotations to balance the tree and incrementally cycles leaves to maintain optimal query performance.
     */
    void Update();

    /**
     * @brief Updates the bounding box of an existing object.
     * @param _uid The UID of the object to update.
     * @param targetBounds The new bounding box.
     */
    void Update(const uint32_t _uid, const AABB& targetBounds);

    /**
     * @brief The most important function: queries the tree for objects inside an area.
     * This traverses the tree, skipping branches that don't intersect the area, resulting in O(log N) lookup time.
     * @param area The bounding box of the camera/viewport.
     * @return A vector of UIDs for all objects whose bounding boxes intersect the query area.
     */
    std::vector<uint32_t> Query(const AABB& area) const;

    /**
     * @brief Returns every single active object UID currently in the tree.
     */
    std::vector<uint32_t> GetAll() const;

    [[nodiscard]] size_t GetNodeCount() const noexcept { return r_tree.size(); }
    [[nodiscard]] size_t GetFreeCount() const noexcept { return freeIndices.size(); }
    [[nodiscard]] int GetRootIndex() const noexcept { return rootIndex; }
    [[nodiscard]] bool IsEmpty() const noexcept { return rootIndex == -1; }
};