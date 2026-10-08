/**
 * =========================================================================================
 * @file test_layer_order.cpp
 * @brief Unit tests for CanvasPage layer stacking, deterministic painter's algorithm,
 *        and fragment adjacency during point erasing.
 * =========================================================================================
 */

#include <cassert>
#include <iostream>
#include <memory>
#include <vector>
#include "core/document/canvas_page.hpp"
#include "core/objects/canvas_object.hpp"

// Lightweight test object implementing CanvasObject
class TestCanvasObject : public CanvasObject {
public:
    std::string name;

    TestCanvasObject(std::string nameStr, double x, double y, double w, double h, int32_t z = 1)
        : name(std::move(nameStr)) {
        bounds = AABB(x, y, x + w, y + h);
        zOrder = z;
    }

    void Render(BLContext&, const Viewport&) const override {}
    std::unique_ptr<CanvasObject> Clone() const override {
        auto c = std::make_unique<TestCanvasObject>(name, bounds.minX, bounds.minY, bounds.Width(), bounds.Height(), zOrder);
        c->pageIndex = this->pageIndex;
        return c;
    }
};

void TestBasicDocumentStacking() {
    std::cout << "[Test] Running TestBasicDocumentStacking..." << std::endl;

    CanvasPage page("TestPage");

    auto obj1 = std::make_shared<TestCanvasObject>("Stroke1", 10.0, 10.0, 20.0, 20.0, 1);
    auto obj2 = std::make_shared<TestCanvasObject>("Stroke2", 15.0, 15.0, 20.0, 20.0, 1);
    auto obj3 = std::make_shared<TestCanvasObject>("Stroke3", 20.0, 20.0, 20.0, 20.0, 1);

    page.AddObject(obj1);
    page.AddObject(obj2);
    page.AddObject(obj3);

    assert(obj1->pageIndex == 0);
    assert(obj2->pageIndex == 1);
    assert(obj3->pageIndex == 2);

    Viewport vp;
    vp.bounds = AABB(0.0, 0.0, 100.0, 100.0);

    auto visible = page.QueryVisible(vp);
    assert(visible.size() == 3);
    assert(visible[0]->uid == obj1->uid);
    assert(visible[1]->uid == obj2->uid);
    assert(visible[2]->uid == obj3->uid);

    std::cout << "[Test] TestBasicDocumentStacking passed!" << std::endl;
}

void TestFragmentAdjacencyPreservation() {
    std::cout << "[Test] Running TestFragmentAdjacencyPreservation..." << std::endl;

    CanvasPage page("TestPage");

    // Scenario: User draws Stroke A, then Stroke B over Stroke A.
    // Stroke B is visually on top of Stroke A.
    auto strokeA = std::make_shared<TestCanvasObject>("StrokeA", 0.0, 0.0, 50.0, 10.0, 1);
    auto strokeB = std::make_shared<TestCanvasObject>("StrokeB", 20.0, -10.0, 10.0, 50.0, 1);

    page.AddObject(strokeA);
    page.AddObject(strokeB);

    assert(strokeA->pageIndex == 0);
    assert(strokeB->pageIndex == 1);

    // Now point eraser cuts Stroke A into Stroke A1 (stays in container) and Stroke A2 (new fragment).
    // Using InsertObjectAdjacent, Stroke A2 must be placed immediately adjacent to Stroke A.
    auto strokeA2 = std::make_shared<TestCanvasObject>("StrokeA2", 30.0, 0.0, 20.0, 10.0, 1);
    page.InsertObjectAdjacent(strokeA, strokeA2);

    // The document stacking sequence must now be: [StrokeA, StrokeA2, StrokeB]
    assert(page.objects.size() == 3);
    assert(page.objects[0] == strokeA);
    assert(page.objects[1] == strokeA2);
    assert(page.objects[2] == strokeB);

    assert(strokeA->pageIndex == 0);
    assert(strokeA2->pageIndex == 1);
    assert(strokeB->pageIndex == 2);

    // QueryVisible must strictly return Stroke B on top of both Stroke A and Stroke A2!
    Viewport vp;
    vp.bounds = AABB(-50.0, -50.0, 100.0, 100.0);
    auto visible = page.QueryVisible(vp);

    assert(visible.size() == 3);
    assert(visible[0] == strokeA);
    assert(visible[1] == strokeA2);
    assert(visible[2] == strokeB);

    std::cout << "[Test] TestFragmentAdjacencyPreservation passed!" << std::endl;
}

void TestReindexingOnRemovalAndStackShift() {
    std::cout << "[Test] Running TestReindexingOnRemovalAndStackShift..." << std::endl;

    CanvasPage page("TestPage");

    auto o1 = std::make_shared<TestCanvasObject>("O1", 0.0, 0.0, 10.0, 10.0, 1);
    auto o2 = std::make_shared<TestCanvasObject>("O2", 10.0, 0.0, 10.0, 10.0, 1);
    auto o3 = std::make_shared<TestCanvasObject>("O3", 20.0, 0.0, 10.0, 10.0, 1);

    page.AddObject(o1);
    page.AddObject(o2);
    page.AddObject(o3);

    // Remove middle object (o2)
    page.RemoveObject(o2);
    assert(page.objects.size() == 2);
    assert(o1->pageIndex == 0);
    assert(o3->pageIndex == 1);

    // Bring o1 to front
    page.BringToFront(o1->uid);
    assert(page.objects[0] == o3);
    assert(page.objects[1] == o1);
    assert(o3->pageIndex == 0);
    assert(o1->pageIndex == 1);

    // Send o1 back to back
    page.SendToBack(o1->uid);
    assert(page.objects[0] == o1);
    assert(page.objects[1] == o3);
    assert(o1->pageIndex == 0);
    assert(o3->pageIndex == 1);

    std::cout << "[Test] TestReindexingOnRemovalAndStackShift passed!" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Starting Canvas Layer Order Tests" << std::endl;
    std::cout << "========================================" << std::endl;

    TestBasicDocumentStacking();
    TestFragmentAdjacencyPreservation();
    TestReindexingOnRemovalAndStackShift();

    std::cout << "========================================" << std::endl;
    std::cout << "All Canvas Layer Order Tests Passed!" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}

