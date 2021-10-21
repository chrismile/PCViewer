#pragma once
#include <vector>
#include <map>
#include <string>

struct Polygon{
    bool change{false};
    int attr1, attr2;
    std::vector<ImVec2> borderPoints;
};
using Polygons = std::vector<Polygon>;