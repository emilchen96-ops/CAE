#pragma once

#include "MeshData.hpp"

#include <QString>

class TopoDS_Shape;

struct MeshResult {
    bool success{false};
    MeshData mesh;
    QString errorMessage;
};

class GmshMesher {
public:
    MeshResult generateTetrahedralMesh(const TopoDS_Shape& shape,
                                       double targetSize) const;
};
