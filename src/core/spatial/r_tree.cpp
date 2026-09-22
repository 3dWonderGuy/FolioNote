#include "r_tree.hpp"
#include "utils/logger.hpp"
#include <algorithm>
#include <string>

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
    freeIndices.clear();
    uidToNode.clear();
    rootIndex = -1;
    cycleIndex = 0;
    isRefitting = false;
}

/**
 * @brief Allocates a node slot, preferring recycled indices from freeIndices to minimize heap reallocations.
 * 
 * @return int32_t Valid index in r_tree pool.
 */
int32_t RTree::AllocateNode() {
    if (!freeIndices.empty()) {
        int32_t idx = freeIndices.back();
        freeIndices.pop_back();
        r_tree[idx].ObjBounds = AABB{};
        r_tree[idx].uid = 0;
        r_tree[idx].left = -1;
        r_tree[idx].right = -1;
        r_tree[idx].parent = -1;
        return idx;
    }

    r_tree.push_back(Node{});
    return static_cast<int32_t>(r_tree.size() - 1);
}

/**
 * @brief Recycles a node by marking its pointer links as dead (-2) and adding its index to freeIndices.
 * 
 * @param nodeIdx Index of node to free.
 */
void RTree::FreeNode(int32_t nodeIdx) {
    if (nodeIdx < 0 || static_cast<size_t>(nodeIdx) >= r_tree.size()) {
        LOG_ERROR_CODE(RTree, FolioErrorCode::CanvasRTreeCorrupted, "FreeNode called with out-of-bounds node index: " + std::to_string(nodeIdx));
        return;
    }
    r_tree[nodeIdx].left = -2;
    r_tree[nodeIdx].right = -2;
    r_tree[nodeIdx].parent = -2;
    r_tree[nodeIdx].uid = 0;
    r_tree[nodeIdx].ObjBounds = AABB{};
    freeIndices.push_back(nodeIdx);
}

/**
 * @brief Inserts a leaf into the dynamic hierarchy using the Surface Area Heuristic (SAH).
 * 
 * @param leafIdx Index of the pre-initialized leaf node in r_tree.
 */
void RTree::InsertLeaf(int32_t leafIdx) {
    if (leafIdx < 0 || static_cast<size_t>(leafIdx) >= r_tree.size()) {
        LOG_ERROR_CODE(RTree, FolioErrorCode::CanvasRTreeCorrupted, "InsertLeaf called with out-of-bounds leaf index: " + std::to_string(leafIdx));
        return;
    }
    const AABB targetBounds = r_tree[leafIdx].ObjBounds; // Value copy: prevents UAF if r_tree reallocates

    if (rootIndex == -1) {
        rootIndex = leafIdx;
        r_tree[leafIdx].parent = -1;
        return;
    }

    // STEP 1: Find best sibling based on surface area enlargement heuristic
    int32_t current = rootIndex;
    size_t depth = 0;
    const size_t maxDepth = r_tree.size() + 2; // Cycle guard prevents infinite loop on corrupted trees
    while (!r_tree[current].IsLeaf() && depth++ < maxDepth) {
        int32_t left = r_tree[current].left;
        int32_t right = r_tree[current].right;

        if (left == -1 || static_cast<size_t>(left) >= r_tree.size() ||
            right == -1 || static_cast<size_t>(right) >= r_tree.size()) {
            LOG_ERROR_CODE(RTree, FolioErrorCode::CanvasRTreeCorrupted, "Corrupted internal node " + std::to_string(current) + " missing valid left/right children in InsertLeaf.");
            break;
        }

        double areaLeft = Area(r_tree[left].ObjBounds);
        double areaRight = Area(r_tree[right].ObjBounds);

        double combinedLeft = Area(Union(r_tree[left].ObjBounds, targetBounds));
        double combinedRight = Area(Union(r_tree[right].ObjBounds, targetBounds));

        double costLeft = combinedLeft - areaLeft;
        double costRight = combinedRight - areaRight;

        // Propagate branch enlargement penalty for non-leaves
        if (!r_tree[left].IsLeaf()) {
            costLeft += combinedLeft - areaLeft;
        }
        if (!r_tree[right].IsLeaf()) {
            costRight += combinedRight - areaRight;
        }

        if (costLeft < costRight) {
            current = left;
        } else {
            current = right;
        }
    }

    if (depth >= maxDepth) {
        LOG_ERROR_CODE(RTree, FolioErrorCode::CanvasRTreeCorrupted, "Tree traversal depth limit (" + std::to_string(maxDepth) + ") exceeded in InsertLeaf - cyclic link detected!");
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
 * @param leafIdx Index of the leaf node in r_tree.
 */
void RTree::RemoveLeaf(int32_t leafIdx) {
    if (leafIdx < 0 || static_cast<size_t>(leafIdx) >= r_tree.size()) {
        LOG_ERROR_CODE(RTree, FolioErrorCode::CanvasRTreeCorrupted, "RemoveLeaf called with out-of-bounds leaf index: " + std::to_string(leafIdx));
        return;
    }

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

    if (grandParent != -1 && static_cast<size_t>(grandParent) < r_tree.size()) {
        if (r_tree[grandParent].left == parent) {
            r_tree[grandParent].left = sibling;
        } else if (r_tree[grandParent].right == parent) {
            r_tree[grandParent].right = sibling;
        } else {
            LOG_ERROR_CODE(RTree, FolioErrorCode::CanvasRTreeCorrupted, "Tree corruption detected in RemoveLeaf: grandParent " + std::to_string(grandParent) + " child mismatch with parent " + std::to_string(parent));
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
    if (iA == -1 || static_cast<size_t>(iA) >= r_tree.size() || r_tree[iA].IsLeaf() || r_tree[iA].IsDead()) {
        return iA;
    }

    int32_t iB = r_tree[iA].left;
    int32_t iC = r_tree[iA].right;

    if (iB == -1 || static_cast<size_t>(iB) >= r_tree.size() || r_tree[iB].IsDead() ||
        iC == -1 || static_cast<size_t>(iC) >= r_tree.size() || r_tree[iC].IsDead()) {
        return iA;
    }

    bool rotated = false;

    // Check rotations on left child iB
    if (!r_tree[iB].IsLeaf()) {
        int32_t iD = r_tree[iB].left;
        int32_t iE = r_tree[iB].right;

        if (iD != -1 && static_cast<size_t>(iD) < r_tree.size() && !r_tree[iD].IsDead() &&
            iE != -1 && static_cast<size_t>(iE) < r_tree.size() && !r_tree[iE].IsDead()) {

            double areaB = Area(r_tree[iB].ObjBounds);

            // Rotation 1: Swap C with D
            double costD = Area(Union(r_tree[iC].ObjBounds, r_tree[iE].ObjBounds));
            // Rotation 2: Swap C with E
            double costE = Area(Union(r_tree[iC].ObjBounds, r_tree[iD].ObjBounds));

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
    }

    isRefitting = false;
}

/**
 * @brief Incrementally cycles leaves (remove + reinsert) over frames to continuously optimize
 * the bounding volume hierarchy and eliminate structural degradation.
 * 
 * Maintains complete synchronization with uidToNode by routing through Remove(uid) and Insert(uid, b).
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
 * @param _uid Unique identifier of object.
 * @param targetBounds Physical bounding box in world space.
 */
void RTree::Insert(const uint32_t _uid, const AABB& targetBounds) {
    if (_uid == 0) {
        LOG_WARN_CODE(RTree, FolioErrorCode::CanvasObjectNotFound, "Attempted to insert object with reserved/invalid UID 0 into spatial index.");
        return;
    }

    if (targetBounds.IsEmpty()) {
        LOG_WARN_CODE(RTree, FolioErrorCode::CanvasInvalidTransform, "Inserting object UID " + std::to_string(_uid) + " with empty/inverted bounding box.");
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
 * @param _uid Unique identifier of object to remove.
 */
void RTree::Remove(const uint32_t _uid) {
    if (_uid == 0) return;
    if (rootIndex == -1) return;

    auto it = uidToNode.find(_uid);
    if (it == uidToNode.end()) {
        LOG_WARN_CODE(RTree, FolioErrorCode::CanvasObjectNotFound, "Attempted to remove unindexed object UID " + std::to_string(_uid) + " from spatial index.");
        return;
    }

    int32_t targetIdx = it->second;
    uidToNode.erase(it);

    if (targetIdx < 0 || static_cast<size_t>(targetIdx) >= r_tree.size() ||
        r_tree[targetIdx].IsDead() || !r_tree[targetIdx].IsLeaf()) {
        LOG_ERROR_CODE(RTree, FolioErrorCode::CanvasRTreeCorrupted, "Corrupted node index " + std::to_string(targetIdx) + " in uidToNode for UID " + std::to_string(_uid));
        return;
    }

    RemoveLeaf(targetIdx);
}

/**
 * @brief Maintenance pass: runs an amortized cycle of leaf re-insertions.
 */
void RTree::Update() {
    if (rootIndex == -1) return;
    CycleTree();
}

/**
 * @brief Updates the bounding box of an existing object.
 * Executes in O(1) lookup + O(log N) tree repositioning.
 * 
 * @param _uid Object unique identifier.
 * @param targetBounds Updated bounding box.
 */
void RTree::Update(const uint32_t _uid, const AABB& targetBounds) {
    if (_uid == 0) return;
    if (targetBounds.IsEmpty()) {
        LOG_WARN_CODE(RTree, FolioErrorCode::CanvasInvalidTransform, "Updating object UID " + std::to_string(_uid) + " with empty/inverted bounding box.");
    }
    Remove(_uid);
    Insert(_uid, targetBounds);
}

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

    if (area.IsEmpty()) {
        LOG_WARN_CODE(RTree, FolioErrorCode::CanvasInvalidTransform, "Spatial query requested with empty/inverted bounding box.");
        return results;
    }

    std::vector<int32_t> stack;
    stack.reserve(64);
    stack.push_back(rootIndex);

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