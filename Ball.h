#pragma once
#include <cstdint>

class Ball {
public:
    float x, y;
    float vx, vy;
    int radius;
    uint16_t color;

    Ball(int width, int height);
    void update(int width, int height);
};