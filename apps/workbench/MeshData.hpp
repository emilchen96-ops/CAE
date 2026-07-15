#pragma once

#include <cstddef>
#include <vector>

struct MeshNode {
    std::size_t id{0};
    double x{0.0};
    double y{0.0};
    double z{0.0};
};

struct MeshElement {
    std::size_t id{0};
    int type{0};
    std::vector<std::size_t> nodeIds;
};

struct MeshData {
    std::vector<MeshNode> nodes;
    std::vector<MeshElement> tetrahedra;
    std::vector<MeshElement> surfaceTriangles;
};
