#include "MeshDataToVtkConverter.hpp"

#include <vtkCell.h>
#include <vtkUnstructuredGrid.h>

#include <iostream>
#include <limits>
#include <string>

namespace {

MeshData validMesh() {
    MeshData mesh;
    mesh.nodes = {
        {10, 0.0, 0.0, 0.0},
        {20, 1.0, 0.0, 0.0},
        {40, 0.0, 1.0, 0.0},
        {80, 0.0, 0.0, 1.0}
    };
    mesh.tetrahedra = {{100, 4, {10, 20, 40, 80}}};
    return mesh;
}

} // namespace

int main() {
    int failures = 0;
    auto check = [&failures](bool condition, const char* name) {
        if (!condition) {
            ++failures;
            std::cerr << "FAILED: " << name << '\n';
        }
    };

    const MeshDataToVtkConverter converter;
    const auto valid = converter.convert(validMesh());
    check(valid.success && valid.grid != nullptr &&
              valid.grid->GetNumberOfPoints() == 4 &&
              valid.grid->GetNumberOfCells() == 1 &&
              valid.grid->GetCell(0)->GetNumberOfPoints() == 4,
          "convert one tetrahedron with non-contiguous node ids");

    MeshData missingNode = validMesh();
    missingNode.tetrahedra[0].nodeIds[3] = 999;
    check(!converter.convert(missingNode).success,
          "reject missing node reference");

    MeshData duplicateNode = validMesh();
    duplicateNode.nodes.push_back({10, 2.0, 2.0, 2.0});
    check(!converter.convert(duplicateNode).success,
          "reject duplicate node id");

    MeshData duplicateCell = validMesh();
    duplicateCell.tetrahedra.push_back(
        {100, 4, {10, 20, 40, 80}});
    check(!converter.convert(duplicateCell).success,
          "reject duplicate element id");

    MeshData wrongNodeCount = validMesh();
    wrongNodeCount.tetrahedra[0].nodeIds.pop_back();
    check(!converter.convert(wrongNodeCount).success,
          "reject tetrahedron with wrong node count");

    MeshData wrongElementType = validMesh();
    wrongElementType.tetrahedra[0].type = 11;
    check(!converter.convert(wrongElementType).success,
          "reject non-linear-tetrahedron element type");

    MeshData repeatedNodeReference = validMesh();
    repeatedNodeReference.tetrahedra[0].nodeIds[3] = 40;
    check(!converter.convert(repeatedNodeReference).success,
          "reject tetrahedron with repeated node reference");

    check(!converter.convert({}).success, "reject empty mesh");

    MeshData noTetrahedra = validMesh();
    noTetrahedra.tetrahedra.clear();
    check(!converter.convert(noTetrahedra).success,
          "reject mesh without tetrahedra");

    MeshData nonFinite = validMesh();
    nonFinite.nodes[0].x =
        std::numeric_limits<double>::quiet_NaN();
    check(!converter.convert(nonFinite).success,
          "reject non-finite coordinates");

    MeshData infinite = validMesh();
    infinite.nodes[0].z =
        std::numeric_limits<double>::infinity();
    check(!converter.convert(infinite).success,
          "reject infinite coordinates");

    if (failures == 0) {
        std::cout << "MeshData to VTK conversion tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
