#pragma once
#include <cstdint>
#include <array>

struct PenState {
    float x = 0.0f;
    float y = 0.0f;
    float pressure = 0.0f;
    float distance = 0.0f;
    float tiltX = 0.0f;
    float tiltY = 0.0f;
    bool isDown = false;
    bool isHovering = false;
    bool barrel1 = false;
    bool barrel2 = false;
    bool barrel3 = false;
    bool barrel4 = false;
    bool barrel5 = false;
    bool eraserTip = false;
};

struct TouchPoint {
    float x = 0.0f;
    float y = 0.0f;
    float dx = 0.0f;
    float dy = 0.0f;
    float pressure = 0.0f;
};

struct TouchSlot {
    int64_t fingerID = -1;
    TouchPoint point;
};

struct MouseState {
    float x = 0.0f;
    float y = 0.0f;
    float dx = 0.0f;
    float dy = 0.0f;
    float wheelY = 0.0f;
    float wheelX = 0.0f;
    bool leftButton = false;
    bool rightButton = false;
    bool middleButton = false;
};

struct KeyboardState {
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
    bool space = false;
};