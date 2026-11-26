#include "games/hwydrift/course.h"
#include <cmath>

namespace hwy {
namespace {
constexpr Vec points[] = {
#include "course_data.inc"
};
}
int sections() { return sizeof(points)/sizeof(points[0])-1; }
Vec node(int i) { return points[i]; }
Road nearestRoad(Vec p) {
    Road result{};
    float best = 1e9f;
    for (int i=0; i<sections(); ++i) {
        const Vec offset = p-points[i];
        // Bound expensive projections once a nearby segment has been found.
        if (best<1600 && (std::fabs(offset.x)>56 || std::fabs(offset.y)>56)) continue;
        const Vec axis = points[i+1]-points[i];
        const float len2 = axis.dot(axis);
        const float t = std::fmax(0.f, std::fmin(1.f, offset.dot(axis)/len2));
        const Vec center = points[i]+axis*t;
        const Vec delta = p-center;
        const float d2 = delta.dot(delta);
        if (d2 < best) {
            best = d2;
            result = {center, axis, i+t};
        }
    }
    result.tangent = result.tangent*(1.f/std::sqrt(result.tangent.dot(result.tangent)));
    if (result.section>=sections()) result.section=0;
    return result;
}
}

