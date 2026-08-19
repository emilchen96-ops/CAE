#include "VtkPostViewWidget.hpp"
#include "VtkResultReader.hpp"

#include <QApplication>
#include <QColor>
#include <QCoreApplication>

#include <vtkAppendFilter.h>
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

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    const VtkResultReader reader;
    const auto read = reader.read(
        std::filesystem::path{QTCAE_TEST_DATA_DIR} / "tetra_result.vtu");
    expect(read.success && read.grid != nullptr, "测试网格应读取成功");
    if (!read.success || read.grid == nullptr) return 1;

    VtkPostViewWidget view;
    view.resize(800, 600);
    view.show();
    application.processEvents();
    auto append = vtkSmartPointer<vtkAppendFilter>::New();
    append->MergePointsOff();
    append->AddInputData(read.grid);
    append->AddInputData(read.grid);
    append->Update();
    auto combined = vtkSmartPointer<vtkUnstructuredGrid>::New();
    combined->DeepCopy(append->GetOutput());
    const vtkIdType cellsPerPart = read.grid->GetNumberOfCells();
    expect(view.displayResultGrid(
               combined,
               {{7, 0, cellsPerPart, true},
                {8, cellsPerPart, cellsPerPart, true}}, true),
           "分部件结果网格应显示成功");
    expect(view.setPartVisible(7, false) && !view.partVisible(7),
           "部件应可独立隐藏");
    expect(view.setPartVisible(7, true) && view.partVisible(7),
           "部件应可独立显示");
    expect(view.partVisible(8), "追加的第二个部件应同时保留并显示");
    const QColor lightBackground(235, 240, 245);
    view.setBackgroundColor(lightBackground);
    expect(view.backgroundColor() == lightBackground,
           "后处理背景颜色应可设置和读取");
    expect(view.setPointSize(9.0) && view.pointSize() == 9.0,
           "点大小应可在有效范围内调整");
    expect(!view.setPointSize(0.0), "无效点大小应被拒绝");
    expect(view.showScalarField("Temperature", ResultFieldAssociation::Point,
                                0, "Temperature"),
           "点模式前应能启用节点云图");
    view.setPoints();
    expect(view.displayMode() == VtkMeshDisplayMode::Points,
           "点显示模式应生效");
    expect(view.pointSize() == 9.0,
           "切换到点显示后应保持用户设置的点大小");
    view.setWireframe(); view.setSurfaceOnly(); view.setSurfaceWithEdges();
    view.setAxonometricView(); view.setFrontView(); view.setBackView();
    view.setLeftView(); view.setRightView(); view.setTopView();
    view.setBottomView(); view.fitAll();

    expect(view.createClipFilter("X剖切", PostFilterAxis::X, 0.2, true, 0) > 0,
           "X 剖切派生对象应创建成功");
    expect(view.createSliceFilter("X切片", PostFilterAxis::X, 0.2, 0) > 0,
           "X 切片派生对象应创建成功");
    expect(view.createThresholdFilter(
               "能量阈值", "CellEnergy", ResultFieldAssociation::Cell,
               0, 40.0, 50.0, 0) > 0,
           "单元阈值派生对象应创建成功");
    expect(view.createPointToCellFilter("节点转单元", 0) > 0,
           "节点转单元派生对象应创建成功");
    expect(view.createCellToPointFilter("单元转节点", 0) > 0,
           "单元转节点派生对象应创建成功");
    expect(view.filterResults().size() == 5,
           "五个派生对象应能同时存在");

    const std::filesystem::path png =
        std::filesystem::temp_directory_path() / "qtcae_post_view_test.png";
    std::error_code error;
    std::filesystem::remove(png, error);
    expect(view.saveCurrentViewToPng(
               QString::fromStdWString(png.wstring())),
           "当前后处理视图应能导出 PNG");
    expect(std::filesystem::exists(png) &&
               std::filesystem::file_size(png) > 0,
           "PNG 文件应存在且非空");
    std::filesystem::remove(png, error);
    view.clearFilters();
    expect(view.filterResults().empty(), "过滤结果应可安全清空");
    expect(view.displayResultGrid(
               read.grid,
               {{8, 0, read.grid->GetNumberOfCells(), true}}, false),
           "删除一个部件后剩余部件应可重新显示");
    expect(!view.setPartVisible(7, false) && view.partVisible(8),
           "重新构建部件管线后被删除部件不应残留 Actor 映射");
    view.close();
    application.processEvents();
    if (failures == 0) std::cout << "后处理视窗集成测试全部通过。\n";
    return failures == 0 ? 0 : 1;
}
