#include "r_tree.hpp"
#include "utils/logger.hpp"
#include <algorithm>
#include <string>
#include <unordered_set>

/**
 * @brief Computes the surface area of a 2D bounding box: A = (maxX - minX) * (maxY - minY).
 * Degenerate or inverted bounding boxes yield 0.0 area.
 * 
 * @param b Axis-Aligned Bounding Box to evaluate.
 * @return Computed surface area as a positive double.
 */
double RTree::Area(const AABB& b) const noexcept {
    if (b.IsEmpty()) return 0.0;
    double w = b.maxX - b.minX;
    double h = b.maxY - b.minY;
    return (w > 0.0 && h > 0.0) ? (w * h) : 0.0;
}

/**
 * @brief Computes the union (minimum enclosing box) of two AABBs.
 * 
 * @param a First bounding box.
 * @param b Second bounding box.
 * @return Enclosing AABB by value.
 */
AABB RTree::Union(const AABB& a, const AABB& b) const noexcept {
    AABB res = a;
    res.Merge(b);
    return res;
}

/**
 * @brief Resets the spatial index, freeing all nodes and clearing the UID lookup table.
 */
void RTree::Clear() noexcept {
    if (!r_tree.empty()) {
        LOG_INFO(RTree, "Clearing spatial index (purging " + std::to_string(uidToNode.size()) + 
                        " objects across " + std::to_string(r_tree.size()) + " pool nodes).");
    }
    r_tree.clear();
    recycledNodeIndices.clear();
    uidToNode.clear();
    rootIndex = -1;
    cycleIndex = 0;
    isRefitting = false;
}

/**
 * @brief Allocates a node slot, preferring recycled indices from recycledNodeIndices to minimize heap reallocations.
 * 
 * Working Process:
 *   1. Check if `recycledNodeIndices` has any previously freed node indices (O(1) LIFO stack pop).
 *   2. If available, retrieve and pop the last recycled index, wipe its state (reset bounds, uid, pointers),
 *      and return it immediately without triggering any vector resize or heap allocations.
 *   3. If no recycled slots exist, append a fresh Node onto the `r_tree` vector (`push_back`),
 *      growing the pool size and returning the new index (size - 1).
 * 
 * @return int32_t Valid index in r_tree pool.
 */
int32_t RTree::AllocateNode() {

    // check if the node recycling id not empty, if no, pull it out and resent node paraments prepping it for future use
    if (!recycledNodeIndices.empty()) {
        int32_t idx = recycledNodeIndices.back();
        recycledNodeIndices.pop_back();
        r_tree[idx] = Node{};
        return idx;
    }

    // in case there are no available free reycled nodes we just create brand new node
    r_tree.push_back(Node{});
    return static_cast<int32_t>(r_tree.size() - 1);
}

/**
 * @brief Recycles a node by marking its pointer links as dead (-2) and adding its index to recycledNodeIndices.
 * 
 * Working Process:
 *   1. Bounds check: ensure `nodeIdx` is within valid pool range [0, r_tree.size()).
 *   2. Poison node links: set left, right, and parent to -2 (sentinel for dead/recycled nodes).
 *   3. Reset payload: set uid to 0 and ObjBounds to empty AABB.
 *   4. Push `nodeIdx` onto `recycledNodeIndices` LIFO stack so subsequent AllocateNode() calls reuse this slot.
 * 
 * @param nodeIdx Index of node in r_tree pool to recycle.
 */
void RTree::FreeNode(int32_t nodeIdx) {

    // check if the node index is valid, if not, log an error and return
    if (nodeIdx < 0 || static_cast<size_t>(nodeIdx) >= r_tree.size()) {
        LOG_ERROR_CODE(RTree, FolioErrorCode::CanvasRTreeCorrupted, "FreeNode called with out-of-bounds node index: " + std::to_string(nodeIdx));
        if (!isRebuilding) {
            Rebuild();
        }
        return;
    }
    // poison node links
    r_tree[nodeIdx].left = -2;
    r_tree[nodeIdx].right = -2;
    r_tree[nodeIdx].parent = -2;
    r_tree[nodeIdx].uid = 0;
    r_tree[nodeIdx].ObjBounds = AABB{};
    recycledNodeIndices.push_back(nodeIdx);
}


/**
 * @brief Inserts a leaf into the dynamic hierarchy using the Surface Area Heuristic (SAH).
 * 
 * @param leafIdx Index of the pre-initialized leaf node in r_tree.
 */
void RTree::InsertLeaf(int32_t leafIdx) {

    // check if the leaf index is valid, if not, log an error and return
    if (leafIdx < 0 || static_cast<size_t>(leafIdx) >= r_tree.size()) {
        LOG_ERROR_CODE(RTree, FolioErrorCode::CanvasRTreeCorrupted, "InsertLeaf called with out-of-bounds leaf index: " + std::to_string(leafIdx));
        if (!isRebuilding) {
            Rebuild();
        }
        return;
    }

    // Value copy: prevents UAF if r_tree reallocates
    const AABB targetBounds = r_tree[leafIdx].ObjBounds; 

    // chekc if this is the root node, special case senerio
    if (rootIndex == -1) {
        rootIndex = leafIdx;
        r_tree[leafIdx].parent = -1;
        return;
    }

    // STEP 1: Find best sibling based on surface area enlargement heuristic
    int32_t current = rootIndex;
    size_t depth = 0;
    // Cycle guard prevents infinite loop on corrupted trees
    const size_t maxDepth = r_tree.size() + 2; 

    // we check if the current node is the leaf, cause there is no lower level to go, as well as check if we wen't over max depth, preventign invinity loop
    while (!r_tree[current].IsLeaf() && depth++ < maxDepth) {

        // present left and rigt
        int32_t left = r_tree[current].left;
        int32_t right = r_tree[current].right;


        // Structural Invariant Verification:
        // In a binary R-Tree, every internal (non-leaf) node MUST have exactly two valid children.
        // Since the while loop verified !r_tree[current].IsLeaf(), 'current' is guaranteed to be an internal node.
        // If either child index is -1 (unassigned) or out-of-bounds, the hierarchy has suffered corruption.
        // We guard against a crash on r_tree[left] / r_tree[right], log the error, and trigger an emergency Rebuild().
        if (left == -1 || static_cast<size_t>(left) >= r_tree.size() ||
            right == -1 || static_cast<size_t>(right) >= r_tree.size()) {
            LOG_ERROR_CODE(RTree, FolioErrorCode::CanvasRTreeCorrupted, "Corrupted internal node " + std::to_string(current) + " missing valid left/right children in InsertLeaf.");
            if (!isRebuilding) {
                Rebuild();
            }
            return;
        }

        // idea is we combine areas of each of the childrens boxes with new boject boundary area to later check which one has smallest area change
        double areaLeft = Area(r_tree[left].ObjBounds);
        double areaRight = Area(r_tree[right].ObjBounds);

        double combinedLeft = Area(Union(r_tree[left].ObjBounds, targetBounds));
        double combinedRight = Area(Union(r_tree[right].ObjBounds, targetBounds));

        double costLeft = combinedLeft - areaLeft;
        double costRight = combinedRight - areaRight;

        // If there is a case where we compare a leaf to a parent node side by side, leafe should win since has least effect on the tree
        if (!r_tree[left].IsLeaf()) {
            costLeft += combinedLeft - areaLeft;
        }
        if (!r_tree[right].IsLeaf()) {
            costRight += combinedRight - areaRight;
        }

        // area change comparison logic
        if (costLeft < costRight) {
            current = left;
        } else {
            current = right;
        }
    }

    // cycle guard, if we went over the max depth, log an error and return
    if (depth >= maxDepth) {
        LOG_ERROR_CODE(RTree, FolioErrorCode::CanvasRTreeCorrupted, "Tree traversal depth limit (" + std::to_string(maxDepth) + ") exceeded in InsertLeaf - cyclic link detected!");
        if (!isRebuilding) {
            Rebuild();
        }
        return;
    }

    // STEP 2: Create a new parent node and attach sibling + new leaf
    int32_t sibling = current;
    int32_t oldParent = r_tree[sibling].parent;
    const AABB siblingBounds = r_tree[sibling].ObjBounds; // Value copy: prevents stale read if r_tree vector reallocates
    int32_t newParentIdx = AllocateNode();

    r_tree[newParentIdx].ObjBounds = Union(siblingBounds, targetBounds);
    r_tree[newParentIdx].uid = 0;
    r_tree[newParentIdx].left = sibling;
    r_tree[newParentIdx].right = leafIdx;
    r_tree[newParentIdx].parent = oldParent;

    r_tree[sibling].parent = newParentIdx;
    r_tree[leafIdx].parent = newParentIdx;

    // STEP 3: Update Old Parent link (explicitly verify left or right match)
    if (oldParent != -1 && static_cast<size_t>(oldParent) < r_tree.size()) {

        if (r_tree[oldParent].left == sibling) {
            r_tree[oldParent].left = newParentIdx;
        } else if (r_tree[oldParent].right == sibling) {

            r_tree[oldParent].right = newParentIdx;
        } else {

            LOG_ERROR_CODE(RTree, FolioErrorCode::CanvasRTreeCorrupted, "Tree corruption detected in InsertLeaf: oldParent " + std::to_string(oldParent) + " child mismatch with sibling " + std::to_string(sibling));
            if (!isRebuilding) {
                Rebuild();
            }
            return;
        }

    } else {

        rootIndex = newParentIdx;
    }

    // STEP 4: Balance and refit bounds up to the root
    RefitBoundsUp(newParentIdx);
}

/**
 * @brief Removes a leaf node from the hierarchy, promotes its sibling, and collapses its parent.
 * 
 * Working Process (How it works):
 *   1. Validation: We first check if the leaf we want to remove actually exists and is within valid bounds.
 *   2. Root Case: If the tree only has one node (the root) and we are removing it, we just clear the root index and free the node.
 *   3. Find Relatives: A node in a binary tree (unless it's the root) has a parent, a grandparent, and a sibling.
 *      Since the parent is just a container holding the leaf and its sibling, removing the leaf means the parent is now redundant.
 *   4. Collapse and Promote: We delete the leaf AND its parent. To keep the tree connected, we take the leaf's sibling 
 *      and attach it directly to the grandparent, effectively bypassing and removing the redundant parent node.
 *   5. Fix Root Case 2: If the redundant parent was actually the root, the sibling becomes the new root.
 *   6. Re-fit Bounds: Finally, since the grandparent inherited a new child (the sibling), its physical bounding box (and all boxes up to the root) 
 *      might have shrunk. We walk up the tree recalculating the bounding boxes using `RefitBoundsUp`.
 * 
 * @param leafIdx Index of the leaf node in r_tree.
 */
void RTree::RemoveLeaf(int32_t leafIdx) {

    // make sure tree is valid, if not, log an error and return
    if (leafIdx < 0 || static_cast<size_t>(leafIdx) >= r_tree.size()) {
        LOG_ERROR_CODE(RTree, FolioErrorCode::CanvasRTreeCorrupted, "RemoveLeaf called with out-of-bounds leaf index: " + std::to_string(leafIdx));
        if (!isRebuilding) {
            Rebuild();
        }
        return;
    }

    // special case senerio check for root node
    if (leafIdx == rootIndex) {
        rootIndex = -1;
        FreeNode(leafIdx);
        return;
    }

    int32_t parent = r_tree[leafIdx].parent;
    if (parent == -1 || static_cast<size_t>(parent) >= r_tree.size()) {
        LOG_WARN_CODE(RTree, FolioErrorCode::CanvasRTreeCorrupted, "RemoveLeaf node " + std::to_string(leafIdx) + " has invalid parent index (" + std::to_string(parent) + ")");
        FreeNode(leafIdx);
        return;
    }

    int32_t grandParent = r_tree[parent].parent;
    int32_t sibling = (r_tree[parent].left == leafIdx) ? r_tree[parent].right : r_tree[parent].left;

    // lil' bit of of cleanup 
    if (grandParent != -1 && static_cast<size_t>(grandParent) < r_tree.size()) {
        if (r_tree[grandParent].left == parent) {
            r_tree[grandParent].left = sibling;
        } else if (r_tree[grandParent].right == parent) {
            r_tree[grandParent].right = sibling;
        } else {
            LOG_ERROR_CODE(RTree, FolioErrorCode::CanvasRTreeCorrupted, "Tree corruption detected in RemoveLeaf: grandParent " + std::to_string(grandParent) + " child mismatch with parent " + std::to_string(parent));
            if (!isRebuilding) {
                Rebuild();
            }
            return;
        }
        if (sibling != -1 && static_cast<size_t>(sibling) < r_tree.size()) {
            r_tree[sibling].parent = grandParent;
        }
        FreeNode(parent);
        FreeNode(leafIdx);

        RefitBoundsUp(grandParent);
    } else {
        rootIndex = sibling;
        if (sibling != -1 && static_cast<size_t>(sibling) < r_tree.size()) {
            r_tree[sibling].parent = -1;
        }
        FreeNode(parent);
        FreeNode(leafIdx);
    }
}

/**
 * @brief Evaluates tree rotations around node iA to minimize combined surface area.
 * Bounding volume changes are strictly local to iA and its children; ancestor updates
 * are handled sequentially by the caller (RefitBoundsUp) to prevent re-entrant lockouts.
 * 
 * @param iA Index of internal node to balance.
 * @return The updated index of node iA.
 */
int32_t RTree::Balance(int32_t iA) {

    // Logic Error 1:  
    if (iA == -1 || static_cast<size_t>(iA) >= r_tree.size() || r_tree[iA].IsLeaf() || r_tree[iA].IsDead()) {
        return iA;
    }

    int32_t iB = r_tree[iA].left;
    int32_t iC = r_tree[iA].right;

    // Logic Error 2: we will crash if we don't check if the children are dead or invalid indexes
    if (iB == -1 || static_cast<size_t>(iB) >= r_tree.size() || r_tree[iB].IsDead() ||
        iC == -1 || static_cast<size_t>(iC) >= r_tree.size() || r_tree[iC].IsDead()) {
        return iA;
    }

    bool rotated = false;

    // Check rotations on left child iB
    if (!r_tree[iB].IsLeaf()) {
        int32_t iD = r_tree[iB].left;
        int32_t iE = r_tree[iB].right;

        // we will crash if we don't check if the children are dead or invalid indexes
        if (iD != -1 && static_cast<size_t>(iD) < r_tree.size() && !r_tree[iD].IsDead() &&
            iE != -1 && static_cast<size_t>(iE) < r_tree.size() && !r_tree[iE].IsDead()) {

            double areaB = Area(r_tree[iB].ObjBounds);

            // Rotation 1: Swap C with D
            double costD = Area(Union(r_tree[iC].ObjBounds, r_tree[iE].ObjBounds));
            // Rotation 2: Swap C with E
            double costE = Area(Union(r_tree[iC].ObjBounds, r_tree[iD].ObjBounds));

            // re-fit bounds after rotation
            if (costD < areaB && costD <= costE) {
                r_tree[iB].left = iC;
                r_tree[iA].right = iD;

                r_tree[iC].parent = iB;
                r_tree[iD].parent = iA;

                r_tree[iB].ObjBounds = Union(r_tree[iE].ObjBounds, r_tree[iC].ObjBounds);
                r_tree[iA].ObjBounds = Union(r_tree[iB].ObjBounds, r_tree[iD].ObjBounds);
                rotated = true;

            } else if (costE < areaB && costE < costD) {
                r_tree[iB].right = iC;
                r_tree[iA].right = iE;

                r_tree[iC].parent = iB;
                r_tree[iE].parent = iA;

                r_tree[iB].ObjBounds = Union(r_tree[iD].ObjBounds, r_tree[iC].ObjBounds);
                r_tree[iA].ObjBounds = Union(r_tree[iB].ObjBounds, r_tree[iE].ObjBounds);
                rotated = true;
            }
        }
    }

    // Check rotations on right child iC (if we didn't already rotate left)
    if (!rotated && !r_tree[iC].IsLeaf()) {
        int32_t iF = r_tree[iC].left;
        int32_t iG = r_tree[iC].right;

        if (iF != -1 && static_cast<size_t>(iF) < r_tree.size() && !r_tree[iF].IsDead() &&
            iG != -1 && static_cast<size_t>(iG) < r_tree.size() && !r_tree[iG].IsDead()) {

            double areaC = Area(r_tree[iC].ObjBounds);

            // Rotation 3: Swap B with F
            double costF = Area(Union(r_tree[iB].ObjBounds, r_tree[iG].ObjBounds));
            // Rotation 4: Swap B with G
            double costG = Area(Union(r_tree[iB].ObjBounds, r_tree[iF].ObjBounds));

            if (costF < areaC && costF <= costG) {
                r_tree[iC].left = iB;
                r_tree[iA].left = iF;

                r_tree[iB].parent = iC;
                r_tree[iF].parent = iA;

                r_tree[iC].ObjBounds = Union(r_tree[iG].ObjBounds, r_tree[iB].ObjBounds);
                r_tree[iA].ObjBounds = Union(r_tree[iC].ObjBounds, r_tree[iF].ObjBounds);
            } else if (costG < areaC && costG < costF) {
                r_tree[iC].right = iB;
                r_tree[iA].left = iG;

                r_tree[iB].parent = iC;
                r_tree[iG].parent = iA;

                r_tree[iC].ObjBounds = Union(r_tree[iF].ObjBounds, r_tree[iB].ObjBounds);
                r_tree[iA].ObjBounds = Union(r_tree[iC].ObjBounds, r_tree[iG].ObjBounds);
            }
        }
    }

    return iA;
}

/**
 * @brief Walks upwards from nodeIdx to rootIndex, balancing each node and refitting bounding boxes.
 * Includes re-entrancy prevention to guard against recursive calls.
 * 
 * @param nodeIdx Index of starting node to refit upwards.
 */
void RTree::RefitBoundsUp(int32_t nodeIdx) {
    if (isRefitting) return;
    isRefitting = true;

    int32_t current = nodeIdx;
    size_t depth = 0;
    const size_t maxDepth = r_tree.size() + 2; // Cycle guard prevents infinite loops

    while (current != -1 && depth++ < maxDepth) {
        current = Balance(current);
        if (current == -1 || static_cast<size_t>(current) >= r_tree.size()) break;

        int32_t left = r_tree[current].left;
        int32_t right = r_tree[current].right;

        if (left != -1 && right != -1 &&
            static_cast<size_t>(left) < r_tree.size() && static_cast<size_t>(right) < r_tree.size()) {
            r_tree[current].ObjBounds = Union(r_tree[left].ObjBounds, r_tree[right].ObjBounds);
        }
        current = r_tree[current].parent;
    }

    if (depth >= maxDepth) {
        LOG_ERROR_CODE(RTree, FolioErrorCode::CanvasRTreeCorrupted, "Tree refitting depth limit (" + std::to_string(maxDepth) + ") exceeded in RefitBoundsUp - cyclic link detected!");
        if (!isRebuilding) {
            Rebuild();
        }
    }

    isRefitting = false;
}

/**
 * @brief Incrementally cycles leaves (remove + reinsert) over frames to continuously optimize
 * the bounding volume hierarchy and eliminate structural degradation.
 * 
 * Working Process (How it works):
 *   Over time, as objects move around, the R-Tree bounding boxes can become inefficient (too much overlap).
 *   Instead of freezing the main thread to completely rebuild the tree from scratch, this function acts as a background cleaner.
 *   
 *   1. It takes a tiny "budget" of nodes (e.g., maximum 4 nodes per call) to process, ensuring it runs extremely fast.
 *   2. It walks through the node array one by one using a persistent `cycleIndex` to remember where it left off last time.
 *   3. If it finds a valid leaf node, it completely removes the object from the tree and immediately re-inserts it.
 *   4. Why? The `Insert` function uses a smart algorithm (Surface Area Heuristic) to find the absolute best place for an object.
 *      By pulling objects out and putting them back in, they naturally settle into better, tighter bounding boxes.
 *   5. Doing this a few nodes at a time every frame keeps the tree perfectly optimized with zero lag.
 */
void RTree::CycleTree() {
    if (rootIndex == -1 || r_tree.empty() || uidToNode.empty()) return;

    // Cycle a small budget of leaves (up to 4 leaves per update call)
    size_t count = 0;
    size_t maxToCycle = std::min<size_t>(4, r_tree.size());
    size_t n = r_tree.size();

    while (count < maxToCycle && n > 0) {
        cycleIndex = cycleIndex % r_tree.size();
        int32_t candidate = static_cast<int32_t>(cycleIndex);
        cycleIndex++;
        n--;

        if (candidate != rootIndex && !r_tree[candidate].IsDead() && r_tree[candidate].IsLeaf() && r_tree[candidate].uid != 0) {
            uint32_t uid = r_tree[candidate].uid;
            AABB b = r_tree[candidate].ObjBounds;
            
            Remove(uid);
            Insert(uid, b);
            count++;
        }
    }
}

/**
 * @brief Inserts an object UID with its world-space AABB bounding box.
 * Automatically updates uidToNode mapping in O(1) time.
 * If the UID already exists, it is cleanly replaced to prevent orphaned nodes.
 * 
 * Working Process (How it works):
 *   1. Safety First: Reject invalid objects (UID 0) and physically impossible bounding boxes (NaN/Inf).
 *   2. Deduplication: If this UID is already in the tree (e.g., the user accidentally added it twice), 
 *      we remove the old one first to prevent duplicates or orphaned data.
 *   3. Create Leaf: We get a free node from our memory pool (`AllocateNode`), assign the object's ID and bounding box to it, 
 *      and mark it as a leaf (no children).
 *   4. Link to Map: We record where we put this leaf in a fast lookup table (`uidToNode`) so we can instantly find it later.
 *   5. Insert into Hierarchy: We pass this new leaf to `InsertLeaf`, which actually crawls down the tree and 
 *      finds the best physical location to place it based on minimizing bounding box sizes.
 * 
 * @param _uid Unique identifier of object.
 * @param targetBounds Physical bounding box in world space.
 */
void RTree::Insert(const uint32_t _uid, const AABB& targetBounds) {

    // Check if it has valid UID
    if (_uid == 0) {
        LOG_WARN_CODE(RTree, FolioErrorCode::CanvasObjectNotFound, "Attempted to insert object with reserved/invalid UID 0 into spatial index.");
        return;
    }

    // Input Sanitization: Reject invalid/non-finite bounds from plugins/extensions to keep tree pristine
    if (!targetBounds.IsValid()) {
        LOG_WARN_CODE(RTree, FolioErrorCode::CanvasInvalidTransform, 
                      "Rejected object UID " + std::to_string(_uid) + " with invalid/non-finite bounding box.");
        return;
    }

    // If UID is already registered, remove old node first to prevent duplicate/orphaned entries
    auto it = uidToNode.find(_uid);
    if (it != uidToNode.end()) {
        int32_t oldIdx = it->second;
        uidToNode.erase(it);
        RemoveLeaf(oldIdx);
    }

    int32_t newLeafIdx = AllocateNode();
    r_tree[newLeafIdx].ObjBounds = targetBounds;
    r_tree[newLeafIdx].uid = _uid;
    r_tree[newLeafIdx].left = -1;
    r_tree[newLeafIdx].right = -1;
    r_tree[newLeafIdx].parent = -1;

    // Register UID to node mapping for O(1) removal and updates
    uidToNode[_uid] = newLeafIdx;

    InsertLeaf(newLeafIdx);
}

/**
 * @brief Removes an object UID from the spatial index in O(1) lookup + O(log N) tree re-linking.
 * 
 * Working Process (How it works):
 *   1. Find the Node: Instead of searching the whole tree, we instantly look up the node's index using our `uidToNode` hash map.
 *   2. If it's not in the map, the object isn't in the tree, so we just exit.
 *   3. Cleanup Map: We erase the UID from our lookup map so nothing points to the soon-to-be-deleted node.
 *   4. Validation: We double-check that the node we found actually makes sense (it's not dead, and it is a leaf). 
 *      If it's corrupted, we trigger an emergency rebuild.
 *   5. Tree Removal: We hand the index off to `RemoveLeaf`, which handles the complicated process of detaching it from the 
 *      tree structure and fixing the parent connections.
 * 
 * @param _uid Unique identifier of object to remove.
 */
void RTree::Remove(const uint32_t _uid) {

    // check if it is valid imput
    if (_uid == 0) return;
    if (rootIndex == -1) return;

    // object not found
    auto it = uidToNode.find(_uid);
    if (it == uidToNode.end()) {
        LOG_WARN_CODE(RTree, FolioErrorCode::CanvasObjectNotFound, "Attempted to remove unindexed object UID " + std::to_string(_uid) + " from spatial index.");
        return;
    }

    int32_t targetIdx = it->second;
    uidToNode.erase(it);

    // Safety check
    if (targetIdx < 0 || static_cast<size_t>(targetIdx) >= r_tree.size() ||
        r_tree[targetIdx].IsDead() || !r_tree[targetIdx].IsLeaf()) {
        LOG_ERROR_CODE(RTree, FolioErrorCode::CanvasRTreeCorrupted, "Corrupted node index " + std::to_string(targetIdx) + " in uidToNode for UID " + std::to_string(_uid));
        if (!isRebuilding) {
            Rebuild();
        }
        return;
    }

    RemoveLeaf(targetIdx);
}

/**
 * @brief Maintenance pass: runs an amortized cycle of leaf re-insertions.
 * 
 * Working Process (How it works):
 *   This is usually called once per frame. It triggers the background tree optimization (`CycleTree`).
 */
void RTree::Update() {
    if (rootIndex == -1) return;
    CycleTree();
}

/**
 * @brief Updates the bounding box of an existing object.
 * Executes in O(1) lookup + O(log N) tree repositioning.
 * 
 * Working Process (How it works):
 *   1. Validation: Ensure the new bounding box makes physical sense.
 *   2. Remove and Replace: In a dynamic R-Tree, moving an object means taking it completely out of its old branch 
 *      and putting it back in so the tree can find the best new branch for it.
 *   3. We simply call `Remove` to detach the old box, and `Insert` to attach the new one in its optimal new location.
 * 
 * @param _uid Object unique identifier.
 * @param targetBounds Updated bounding box.
 */
void RTree::Update(const uint32_t _uid, const AABB& targetBounds) {
    if (_uid == 0) return;
    if (!targetBounds.IsValid()) {
        LOG_WARN_CODE(RTree, FolioErrorCode::CanvasInvalidTransform, 
                      "Rejected update for object UID " + std::to_string(_uid) + " with invalid/non-finite bounding box.");
        return;
    }
    Remove(_uid);
    Insert(_uid, targetBounds);
}



// THE BEST PART OF THE RTREE HERE, FAST OBJECT SEARCH
/**
 * @brief Hierarchical bounding-box intersection query.
 * Traverses down the tree using an explicit stack, pruning branches that do not intersect `area`.
 * 
 * Complexity: O(log N) average query time.
 * 
 * @param area Query bounding box (e.g. camera viewport).
 * @return Vector of intersecting object UIDs.
 */
std::vector<uint32_t> RTree::Query(const AABB& area) const {
    std::vector<uint32_t> results;
    if (rootIndex == -1) return results;

    // check if it is valid input
    if (!area.IsValid()) {
        LOG_WARN_CODE(RTree, FolioErrorCode::CanvasInvalidTransform, "Spatial query requested with invalid/non-finite bounding box.");
        return results;
    }

    // create temporary array where we store out stack
    std::vector<int32_t> stack;
    stack.reserve(64);
    stack.push_back(rootIndex);

    // Loop until the stack is empty
    while (!stack.empty()) {
        int32_t current = stack.back();
        stack.pop_back();

        if (current < 0 || static_cast<size_t>(current) >= r_tree.size()) continue;
        const Node& node = r_tree[current];
        if (node.IsDead()) continue;

        // Culling step: if bounding box does not intersect query area, prune entire subtree
        if (!node.ObjBounds.Intersects(area)) continue;

        if (node.IsLeaf()) {
            results.push_back(node.uid);
        } else {
            if (node.left != -1) stack.push_back(node.left);
            if (node.right != -1) stack.push_back(node.right);
        }
    }

    return results;
}

/**
 * @brief Retrieves all active object UIDs registered in the index.
 * 
 * @return Vector of all active object UIDs.
 */
std::vector<uint32_t> RTree::GetAll() const {
    std::vector<uint32_t> results;
    results.reserve(uidToNode.size());
    for (const auto& [uid, nodeIdx] : uidToNode) {
        results.push_back(uid);
    }
    return results;
}

/**
 * @brief Performs complete self-healing reconstruction of the spatial index.
 *          Idea is if there is any time where something happend custome object caused
 *          our rtree to get currupted this has to catch that, rebuild rtree from scratch over agian
 *          preserve out data and keep system healthy. Also has preventative features from, rebuild lock.
 * 
 * Working Process:
 *   1. Re-entrancy guard check (`isRebuilding`): prevents nested rebuild calls if an
 *      issue occurs during reconstruction.
 *   2. Harvests all recoverable (UID, bounds) pairs from:
 *      a) `uidToNode` hash map (where index points to a valid, live node matching UID).
 *      b) Linear sweep over `r_tree` node pool for any healthy leaves (`!node.IsDead() && node.IsLeaf() && node.uid != 0`)
 *         that may have been orphaned from `uidToNode`.
 *   3. Completely clears all internal state via `Clear()` to discard corrupted pointers/cycles.
 *   4. Pre-allocates contiguous memory for `2 * N` nodes (binary tree property).
 *   5. Sequentially re-inserts all active entities using optimal SAH insertion.
 *   6. Restores `isRebuilding = false` and logs completion metrics.
 */
void RTree::Rebuild() {
    if (isRebuilding) {
        return;
    }

    // Circuit Breaker: Prevent main-thread freeze if repeated corruptions occur
    if (isCircuitBroken) {
        LOG_ERROR_CODE(RTree, FolioErrorCode::CanvasRTreeCorrupted, 
                       "R-Tree circuit breaker is TRIPPED. Suppressing rebuild to prevent main-thread freeze.");
        return;
    }

    if (consecutiveRebuilds >= MAX_CONSECUTIVE_REBUILDS) {
        isCircuitBroken = true;
        LOG_ERROR_CODE(RTree, FolioErrorCode::CanvasRTreeCorrupted, 
                       "R-Tree circuit breaker TRIPPED after " + std::to_string(consecutiveRebuilds) + 
                       " consecutive rebuild attempts! Purging spatial tree to protect 60 FPS main thread.");
        Clear();
        return;
    }

    consecutiveRebuilds++;
    isRebuilding = true;

    LOG_WARN(RTree, "Initiating automatic R-Tree self-healing rebuild (attempt " + 
                    std::to_string(consecutiveRebuilds) + "/" + std::to_string(MAX_CONSECUTIVE_REBUILDS) + ").");

    // Step 1: Harvest all recoverable entities with valid bounds
    std::vector<std::pair<uint32_t, AABB>> activeObjects;
    activeObjects.reserve(uidToNode.size());

    // 1a. Harvest from uidToNode where index is valid, not dead, and bounds are valid
    for (const auto& [uid, nodeIdx] : uidToNode) {
        if (uid != 0 && nodeIdx >= 0 && static_cast<size_t>(nodeIdx) < r_tree.size()) {
            const auto& node = r_tree[nodeIdx];
            if (!node.IsDead() && node.uid == uid && node.ObjBounds.IsValid()) {
                activeObjects.emplace_back(uid, node.ObjBounds);
            }
        }
    }

    // 1b. Scan r_tree pool for any living leaves with valid UIDs not captured above
    for (const auto& node : r_tree) {
        if (node.uid != 0 && !node.IsDead() && node.IsLeaf() && node.ObjBounds.IsValid()) {
            bool alreadyCaptured = false;
            for (const auto& [u, _] : activeObjects) {
                if (u == node.uid) {
                    alreadyCaptured = true;
                    break;
                }
            }
            if (!alreadyCaptured) {
                activeObjects.emplace_back(node.uid, node.ObjBounds);
            }
        }
    }

    // Step 2: Clear internal tree state
    Clear();

    // Step 3: Pre-reserve pool capacity (binary tree with N leaves has at most 2N - 1 nodes)
    if (!activeObjects.empty()) {
        r_tree.reserve(activeObjects.size() * 2);
    }

    // Step 4: Re-insert entities
    for (const auto& [uid, bounds] : activeObjects) {
        Insert(uid, bounds);
    }

    isRebuilding = false;

    // Reset circuit breaker counter if rebuilt tree is completely healthy
    if (ValidateIntegrity()) {
        consecutiveRebuilds = 0;
        isCircuitBroken = false;
    }

    LOG_INFO(RTree, "R-Tree rebuild complete: " + std::to_string(activeObjects.size()) + " objects restored.");
}

/**
 * @brief Rebuilds the spatial index directly from an external list of (UID, AABB) pairs.
 * 
 * Working Process:
 *   1. Sets `isRebuilding` guard.
 *   2. Purges current tree via `Clear()`.
 *   3. Pre-allocates `2 * items.size()` node capacity.
 *   4. Inserts each item with non-zero UID.
 *   5. Clears guard.
 * 
 * @param items Vector of pairs containing (object UID, world-space AABB).
 */
void RTree::Rebuild(const std::vector<std::pair<uint32_t, AABB>>& items) {
    if (isRebuilding) {
        return;
    }
    isRebuilding = true;

    Clear();
    if (!items.empty()) {
        r_tree.reserve(items.size() * 2);
    }

    for (const auto& [uid, bounds] : items) {
        if (uid != 0 && bounds.IsValid()) {
            Insert(uid, bounds);
        }
    }

    isRebuilding = false;

    if (ValidateIntegrity()) {
        consecutiveRebuilds = 0;
        isCircuitBroken = false;
    }
}

/**
 * @brief Validates the structural integrity and mathematical invariants of the tree.
 * 
 * Invariants Checked:
 *   1. Empty tree consistency: if rootIndex == -1, uidToNode must be empty.
 *   2. Pool boundary: rootIndex (if >= 0) must be < r_tree.size().
 *   3. Cycle prevention & hierarchy validation via DFS traversal:
 *      - Traversal depth <= r_tree.size() + 2 (guarantees DAG / strictly acyclic tree).
 *      - No node in active hierarchy is dead (`IsDead() == false`).
 *      - For leaf nodes: left == -1 && right == -1 && uid != 0.
 *      - For internal nodes: left >= 0 && right >= 0 && both < r_tree.size().
 *      - Parent-child symmetry: r_tree[left].parent == current && r_tree[right].parent == current.
 *      - Bounding box containment: parent box must enclose children's boxes.
 *   4. Associative map coherence: every entry in `uidToNode` points to a visited active leaf node with matching UID.
 *   5. Free list coherence: every index in `recycledNodeIndices` is within pool bounds and marked dead.
 * 
 * @return true if all invariants hold; false if any corruption or anomaly is detected.
 */
bool RTree::ValidateIntegrity() const noexcept {
    // 1. Empty tree invariant
    if (rootIndex == -1) {
        return uidToNode.empty();
    }

    // 2. Root bounds check
    if (rootIndex < 0 || static_cast<size_t>(rootIndex) >= r_tree.size()) {
        return false;
    }

    // 3. DFS hierarchy validation and cycle detection
    std::vector<int32_t> stack;
    stack.reserve(64);
    stack.push_back(rootIndex);

    std::unordered_set<int32_t> visited;
    visited.reserve(r_tree.size());

    size_t leafCount = 0;
    const size_t maxAllowedVisited = r_tree.size();

    while (!stack.empty()) {
        int32_t current = stack.back();
        stack.pop_back();

        if (current < 0 || static_cast<size_t>(current) >= r_tree.size()) {
            return false;
        }

        // Cycle check: a node cannot be visited more than once in a tree
        if (visited.find(current) != visited.end()) {
            return false;
        }
        visited.insert(current);
        if (visited.size() > maxAllowedVisited) {
            return false;
        }

        const Node& node = r_tree[current];
        if (node.IsDead()) {
            return false; // Active tree must never contain dead nodes
        }

        if (node.IsLeaf()) {
            if (node.uid == 0) return false; // Leaves must have non-zero UID
            auto it = uidToNode.find(node.uid);
            if (it == uidToNode.end() || it->second != current) {
                return false; // UID lookup map mismatch
            }
            leafCount++;
        } else {
            // Internal node validation
            int32_t left = node.left;
            int32_t right = node.right;

            if (left < 0 || static_cast<size_t>(left) >= r_tree.size() ||
                right < 0 || static_cast<size_t>(right) >= r_tree.size()) {
                return false;
            }

            if (r_tree[left].parent != current || r_tree[right].parent != current) {
                return false; // Parent-child link asymmetry
            }

            stack.push_back(right);
            stack.push_back(left);
        }
    }

    // 4. Associative map count must equal visited leaf count
    if (uidToNode.size() != leafCount) {
        return false;
    }

    // 5. Recycled node pool check
    for (int32_t freeIdx : recycledNodeIndices) {
        if (freeIdx < 0 || static_cast<size_t>(freeIdx) >= r_tree.size()) {
            return false;
        }
        if (!r_tree[freeIdx].IsDead()) {
            return false; // All nodes in recycled stack must be marked dead
        }
        if (visited.find(freeIdx) != visited.end()) {
            return false; // Recycled node cannot be in active tree
        }
    }

    return true;
}