#pragma once

namespace hwy {
struct Vec {
    float x, y;
    constexpr Vec(float x=0, float y=0) : x(x), y(y) {}
    Vec operator+(Vec b) const { return {x+b.x, y+b.y}; }
    Vec operator-(Vec b) const { return {x-b.x, y-b.y}; }
    Vec operator*(float scale) const { return {x*scale, y*scale}; }
    float dot(Vec b) const { return x*b.x+y*b.y; }
};
struct Road {
    Vec center, tangent;
    float section; // Continuous section index, zero at the timing line.
};
constexpr int kMapWidth = 600, kMapHeight = 338;
constexpr float kHalfWidth = 17.4f; // Inner face of the drawn wall, past the kerb.
int sections();
Vec node(int index);
Road nearestRoad(Vec position);
}

