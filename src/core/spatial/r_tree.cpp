#include "r_tree.hpp"
#include <algorithm>

double RTree::Area(const AABB& b) const noexcept {
    double w = b.maxX - b.minX;
    double h = b.maxY - b.minY;
    return (w > 0.0 && h > 0.0) ? (w * h) : 0.0;
}

AABB RTree::Union(const AABB& a, const AABB& b) const noexcept {
    AABB res = a;   // 1. Creates a brand-new TEMPORARY copy of 'a'
    res.Merge(b);   // 2. Modifies ONLY the temporary copy
    return res;     // 3. Returns the temporary copy by value
}

void RTree::Clear() noexcept {
    r_tree.clear();
    freeIndices.clear();
    rootIndex = -1;
    cycleIndex = 0;
}

int RTree::AllocateNode() {
    if (!freeIndices.empty()) {
        int idx = freeIndices.back();
        freeIndices.pop_back();
        r_tree[idx].ObjBounds = AABB{};
        r_tree[idx].uid = 0;
        r_tree[idx].left = -1;
        r_tree[idx].right = -1;
        r_tree[idx].parent = -1;
        return idx;
    }

    r_tree.push_back(Node{});
    return static_cast<int>(r_tree.size() - 1);
}

void RTree::FreeNode(int nodeIdx) {
    if (nodeIdx < 0 || static_cast<size_t>(nodeIdx) >= r_tree.size()) return;
    r_tree[nodeIdx].left = -2;
    r_tree[nodeIdx].right = -2;
    r_tree[nodeIdx].parent = -2;
    r_tree[nodeIdx].uid = 0;
    r_tree[nodeIdx].ObjBounds = AABB{};
    freeIndices.push_back(nodeIdx);
}

void RTree::InsertLeaf(int leafIdx) {
    const AABB& targetBounds = r_tree[leafIdx].ObjBounds;

    if (rootIndex == -1) {
        rootIndex = leafIdx;
        r_tree[leafIdx].parent = -1;
        return;
    }

    // STEP 1: Find best sibling based on surface area enlargement heuristic
    // We start at the root and walk down the tree to find the node whose bounding box 
    // would grow the least if we added our new leaf to it.
    int current = rootIndex;
    while (!r_tree[current].IsLeaf()) {
        int left = r_tree[current].left;
        int right = r_tree[current].right;

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

    // STEP 2: Create a new parent node and attach sibling + new leaf
    int sibling = current;
    int oldParent = r_tree[sibling].parent;
    int newParentIdx = AllocateNode();

    r_tree[newParentIdx].ObjBounds = Union(r_tree[sibling].ObjBounds, targetBounds);
    r_tree[newParentIdx].uid = 0;
    r_tree[newParentIdx].left = sibling;
    r_tree[newParentIdx].right = leafIdx;
    r_tree[newParentIdx].parent = oldParent;

    r_tree[sibling].parent = newParentIdx;
    r_tree[leafIdx].parent = newParentIdx;

    // STEP 3: Update Old Parent
    if (oldParent != -1) {
        if (r_tree[oldParent].left == sibling) {
            r_tree[oldParent].left = newParentIdx;
        } else {
            r_tree[oldParent].right = newParentIdx;
        }
    } else {
        rootIndex = newParentIdx;
    }

    // STEP 4: Balance and refit bounds up to the root
    RefitBoundsUp(newParentIdx);
}

void RTree::RemoveLeaf(int leafIdx) {
    if (leafIdx == rootIndex) {
        rootIndex = -1;
        FreeNode(leafIdx);
        return;
    }

    int parent = r_tree[leafIdx].parent;
    int grandParent = (parent != -1) ? r_tree[parent].parent : -1;
    int sibling = (r_tree[parent].left == leafIdx) ? r_tree[parent].right : r_tree[parent].left;

    if (grandParent != -1) {
        if (r_tree[grandParent].left == parent) {
            r_tree[grandParent].left = sibling;
        } else {
            r_tree[grandParent].right = sibling;
        }
        r_tree[sibling].parent = grandParent;
        FreeNode(parent);
        FreeNode(leafIdx);

        RefitBoundsUp(grandParent);
    } else {
        rootIndex = sibling;
        r_tree[sibling].parent = -1;
        FreeNode(parent);
        FreeNode(leafIdx);
    }
}

int RTree::Balance(int iA) {
    if (iA == -1 || r_tree[iA].IsLeaf() || r_tree[iA].IsDead()) {
        return iA;
    }

    int iB = r_tree[iA].left;
    int iC = r_tree[iA].right;

    // Check rotations on left child iB
    if (iB != -1 && !r_tree[iB].IsLeaf() && !r_tree[iB].IsDead()) {
        int iD = r_tree[iB].left;
        int iE = r_tree[iB].right;

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
            return iA;
        } else if (costE < areaB && costE < costD) {
            r_tree[iB].right = iC;
            r_tree[iA].right = iE;

            r_tree[iC].parent = iB;
            r_tree[iE].parent = iA;

            r_tree[iB].ObjBounds = Union(r_tree[iD].ObjBounds, r_tree[iC].ObjBounds);
            r_tree[iA].ObjBounds = Union(r_tree[iB].ObjBounds, r_tree[iE].ObjBounds);
            return iA;
        }
    }

    // Check rotations on right child iC
    if (iC != -1 && !r_tree[iC].IsLeaf() && !r_tree[iC].IsDead()) {
        int iF = r_tree[iC].left;
        int iG = r_tree[iC].right;

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
            return iA;
        } else if (costG < areaC && costG < costF) {
            r_tree[iC].right = iB;
            r_tree[iA].left = iG;

            r_tree[iB].parent = iC;
            r_tree[iG].parent = iA;

            r_tree[iC].ObjBounds = Union(r_tree[iF].ObjBounds, r_tree[iB].ObjBounds);
            r_tree[iA].ObjBounds = Union(r_tree[iC].ObjBounds, r_tree[iG].ObjBounds);
            return iA;
        }
    }

    return iA;
}

void RTree::RefitBoundsUp(int nodeIdx) {
    int current = nodeIdx;
    while (current != -1) {
        current = Balance(current);

        int left = r_tree[current].left;
        int right = r_tree[current].right;

        if (left != -1 && right != -1) {
            r_tree[current].ObjBounds = Union(r_tree[left].ObjBounds, r_tree[right].ObjBounds);
        }
        current = r_tree[current].parent;
    }
}

void RTree::CycleTree() {
    if (rootIndex == -1 || r_tree.empty()) return;

    // Cycle a budget of leaves (up to 4 leaves per update call) to continuously optimize tree structure
    size_t count = 0;
    size_t maxToCycle = std::min<size_t>(4, r_tree.size());
    size_t n = r_tree.size();

    while (count < maxToCycle && n > 0) {
        cycleIndex = cycleIndex % r_tree.size();
        int candidate = static_cast<int>(cycleIndex);
        cycleIndex++;
        n--;

        if (candidate != rootIndex && !r_tree[candidate].IsDead() && r_tree[candidate].IsLeaf()) {
            RemoveLeaf(candidate);
            InsertLeaf(candidate);
            count++;
        }
    }
}

void RTree::Insert(const uint32_t _uid, const AABB& targetBounds) {
    int newLeafIdx = AllocateNode();
    r_tree[newLeafIdx].ObjBounds = targetBounds;
    r_tree[newLeafIdx].uid = _uid;
    r_tree[newLeafIdx].left = -1;
    r_tree[newLeafIdx].right = -1;
    r_tree[newLeafIdx].parent = -1;

    InsertLeaf(newLeafIdx);
}

void RTree::Remove(const uint32_t _uid) {
    if (rootIndex == -1) return;

    int targetIdx = -1;
    for (size_t i = 0; i < r_tree.size(); ++i) {
        if (!r_tree[i].IsDead() && r_tree[i].IsLeaf() && r_tree[i].uid == _uid) {
            targetIdx = static_cast<int>(i);
            break;
        }
    }

    if (targetIdx == -1) return;
    RemoveLeaf(targetIdx);
}

void RTree::Update() {
    if (rootIndex == -1) return;

    // 1. Pass over internal nodes to apply balancing rotations
    for (size_t i = 0; i < r_tree.size(); ++i) {
        if (!r_tree[i].IsDead() && !r_tree[i].IsLeaf()) {
            Balance(static_cast<int>(i));
        }
    }

    // 2. Incremental tree cycling pass (round-robin leaf re-insertion)
    CycleTree();
}

void RTree::Update(const uint32_t _uid, const AABB& targetBounds) {
    if (rootIndex == -1) return;

    int targetIdx = -1;
    for (size_t i = 0; i < r_tree.size(); ++i) {
        if (!r_tree[i].IsDead() && r_tree[i].IsLeaf() && r_tree[i].uid == _uid) {
            targetIdx = static_cast<int>(i);
            break;
        }
    }

    if (targetIdx == -1) {
        Insert(_uid, targetBounds);
        return;
    }

    // If new bounds are already within current bounds, keep structure
    if (r_tree[targetIdx].ObjBounds.Contains(targetBounds.minX, targetBounds.minY) &&
        r_tree[targetIdx].ObjBounds.Contains(targetBounds.maxX, targetBounds.maxY)) {
        return;
    }

    RemoveLeaf(targetIdx);
    r_tree[targetIdx].ObjBounds = targetBounds;
    InsertLeaf(targetIdx);
}

std::vector<uint32_t> RTree::Query(const AABB& area) const {
    std::vector<uint32_t> results;
    if (rootIndex == -1) return results;

    std::vector<int> stack;
    stack.reserve(64);
    stack.push_back(rootIndex);

    while (!stack.empty()) {
        int current = stack.back();
        stack.pop_back();

        if (current < 0 || static_cast<size_t>(current) >= r_tree.size()) continue;
        const Node& node = r_tree[current];
        if (node.IsDead()) continue;

        // Culling step: if bounding box does not intersect query area, skip subtree
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

std::vector<uint32_t> RTree::GetAll() const {
    std::vector<uint32_t> results;
    for (const auto& node : r_tree) {
        if (!node.IsDead() && node.IsLeaf()) {
            results.push_back(node.uid);
        }
    }
    return results;
}
