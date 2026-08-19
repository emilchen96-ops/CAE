#include "VtkResultReader.hpp"

#include <vtkCellData.h>
#include <vtkCellDataToPointData.h>
#include <vtkClipDataSet.h>
#include <vtkCutter.h>
#include <vtkDataObject.h>
#include <vtkPlane.h>
#include <vtkPointData.h>
#include <vtkPointDataToCellData.h>
#include <vtkThreshold.h>
#include <vtkUnstructuredGrid.h>

#include <filesystem>
#include <iostream>
#include <string>

namespace {
int failures = 0;
void expect(bool condition, const std::string& message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int main() {
    const VtkResultReader reader;
    const auto read = reader.read(
        std::filesystem::path{QTCAE_TEST_DATA_DIR} / "tetra_result.vtu");
    expect(read.success && read.grid != nullptr, "测试结果应读取成功");
    if (!read.success || read.grid == nullptr) return 1;
    const vtkIdType originalPoints = read.grid->GetNumberOfPoints();
    const vtkIdType originalCells = read.grid->GetNumberOfCells();

    auto plane = vtkSmartPointer<vtkPlane>::New();
    plane->SetOrigin(0.2, 0.0, 0.0);
    plane->SetNormal(1.0, 0.0, 0.0);
    auto clip = vtkSmartPointer<vtkClipDataSet>::New();
    clip->SetInputData(read.grid); clip->SetClipFunction(plane); clip->Update();
    expect(clip->GetOutput()->GetNumberOfCells() > 0, "X 剖切应产生输出");
    clip->InsideOutOn(); clip->Update();
    expect(clip->GetOutput()->GetNumberOfCells() > 0, "反向剖切应产生输出");

    auto cutter = vtkSmartPointer<vtkCutter>::New();
    cutter->SetInputData(read.grid); cutter->SetCutFunction(plane); cutter->Update();
    expect(cutter->GetOutput()->GetNumberOfCells() > 0, "X 平面切片应产生输出");

    auto threshold = vtkSmartPointer<vtkThreshold>::New();
    threshold->SetInputData(read.grid);
    threshold->SetInputArrayToProcess(0, 0, 0,
        vtkDataObject::FIELD_ASSOCIATION_CELLS, "CellEnergy");
    threshold->SetLowerThreshold(40.0); threshold->SetUpperThreshold(50.0);
    threshold->SetThresholdFunction(vtkThreshold::THRESHOLD_BETWEEN);
    threshold->Update();
    expect(threshold->GetOutput()->GetNumberOfCells() == 1,
           "单元标量阈值应保留匹配单元");

    auto pointToCell = vtkSmartPointer<vtkPointDataToCellData>::New();
    pointToCell->SetInputData(read.grid); pointToCell->Update();
    expect(pointToCell->GetOutput()->GetCellData()->GetArray("Temperature") != nullptr,
           "节点到单元转换应生成同名单元字段");

    auto cellToPoint = vtkSmartPointer<vtkCellDataToPointData>::New();
    cellToPoint->SetInputData(read.grid); cellToPoint->Update();
    expect(cellToPoint->GetOutput()->GetPointData()->GetArray("CellEnergy") != nullptr,
           "单元到节点转换应生成同名节点字段");

    expect(read.grid->GetNumberOfPoints() == originalPoints &&
               read.grid->GetNumberOfCells() == originalCells,
           "过滤操作不得修改原始结果网格");
    if (failures == 0) std::cout << "后处理过滤管线测试全部通过。\n";
    return failures == 0 ? 0 : 1;
}
