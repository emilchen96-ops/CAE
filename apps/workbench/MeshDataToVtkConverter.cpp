#include "MeshDataToVtkConverter.hpp"

#include <vtkCellType.h>
#include <vtkPoints.h>
#include <vtkUnstructuredGrid.h>

#include <array>
#include <cmath>
#include <exception>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace {

MeshDataToVtkConverter::ConversionResult failure(std::string message) {
    return {.errorMessage = std::move(message)};
}

} // namespace

MeshDataToVtkConverter::ConversionResult
MeshDataToVtkConverter::convert(const MeshData& mesh) const {
    if (mesh.nodes.empty()) {
        return failure("网格没有节点。");
    }
    if (mesh.tetrahedra.empty()) {
        return failure("网格没有四面体单元。");
    }

    try {
        auto points = vtkSmartPointer<vtkPoints>::New();
        points->SetDataTypeToDouble();
        points->Allocate(static_cast<vtkIdType>(mesh.nodes.size()));

        std::unordered_map<std::size_t, vtkIdType> nodeIdToPointId;
        nodeIdToPointId.reserve(mesh.nodes.size());
        for (const MeshNode& node : mesh.nodes) {
            if (!std::isfinite(node.x) || !std::isfinite(node.y) ||
                !std::isfinite(node.z)) {
                return failure("网格节点包含 NaN 或无穷坐标。");
            }
            if (nodeIdToPointId.contains(node.id)) {
                return failure("网格包含重复节点 ID。");
            }
            const vtkIdType pointId =
                points->InsertNextPoint(node.x, node.y, node.z);
            nodeIdToPointId.emplace(node.id, pointId);
        }

        auto grid = vtkSmartPointer<vtkUnstructuredGrid>::New();
        grid->SetPoints(points);
        grid->Allocate(static_cast<vtkIdType>(mesh.tetrahedra.size()));

        std::unordered_set<std::size_t> elementIds;
        elementIds.reserve(mesh.tetrahedra.size());
        for (const MeshElement& element : mesh.tetrahedra) {
            if (!elementIds.insert(element.id).second) {
                return failure("网格包含重复四面体单元 ID。");
            }
            if (element.type != 4) {
                return failure("网格包含非一阶四面体单元。");
            }
            if (element.nodeIds.size() != 4) {
                return failure("一阶四面体必须恰好引用 4 个节点。");
            }

            std::array<vtkIdType, 4> pointIds{};
            std::unordered_set<std::size_t> referencedNodeIds;
            for (std::size_t index = 0; index < pointIds.size(); ++index) {
                if (!referencedNodeIds.insert(
                        element.nodeIds[index]).second) {
                    return failure(
                        "四面体不能重复引用同一个节点 ID。");
                }
                const auto iterator =
                    nodeIdToPointId.find(element.nodeIds[index]);
                if (iterator == nodeIdToPointId.end()) {
                    return failure("四面体引用了不存在的节点 ID。");
                }
                pointIds[index] = iterator->second;
            }
            grid->InsertNextCell(VTK_TETRA,
                                 static_cast<vtkIdType>(pointIds.size()),
                                 pointIds.data());
        }

        return {.success = true, .grid = std::move(grid)};
    } catch (const std::exception& exception) {
        return failure(std::string("VTK 网格转换异常：") + exception.what());
    } catch (...) {
        return failure("VTK 网格转换发生未知异常。");
    }
}
