#include "ResultFieldProcessor.hpp"
#include "VtkResultReader.hpp"

#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkDoubleArray.h>
#include <vtkDataObject.h>
#include <vtkPointData.h>
#include <vtkPointSet.h>
#include <vtkUnstructuredGrid.h>
#include <vtkWarpVector.h>
#include <vtkUnstructuredGridWriter.h>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>

namespace {

int failureCount = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failureCount;
    }
}

bool closeTo(double actual, double expected,
             double tolerance = 1.0e-10) {
    return std::abs(actual - expected) <= tolerance;
}

const ResultFieldInfo* findField(
    const std::vector<ResultFieldInfo>& fields,
    const std::string& name,
    ResultFieldAssociation association) {
    for (const ResultFieldInfo& field : fields) {
        if (field.name == name && field.association == association) {
            return &field;
        }
    }
    return nullptr;
}

const ResultScalarOption* findOption(
    const std::vector<ResultScalarOption>& options,
    ResultScalarOperation operation) {
    for (const ResultScalarOption& option : options) {
        if (option.operation == operation) {
            return &option;
        }
    }
    return nullptr;
}

} // namespace

int main() {
    const std::filesystem::path dataDirectory{
        QTCAE_TEST_DATA_DIR};
    const VtkResultReader reader;
    const VtkReadResult readResult =
        reader.read(dataDirectory / "tetra_result.vtu");
    expect(readResult.success, "有效 VTU 应读取成功");
    expect(readResult.grid != nullptr, "读取结果应包含网格");
    if (!readResult.success || readResult.grid == nullptr) {
        return 1;
    }

    expect(readResult.grid->GetNumberOfPoints() == 4,
           "节点数应为 4");
    expect(readResult.grid->GetNumberOfCells() == 1,
           "单元数应为 1");
    expect(readResult.fields.size() == 4,
           "应枚举四个数值结果字段且忽略全局 ID");

    const ResultFieldInfo* temperature = findField(
        readResult.fields, "Temperature",
        ResultFieldAssociation::Point);
    const ResultFieldInfo* displacement = findField(
        readResult.fields, "Displacement",
        ResultFieldAssociation::Point);
    const ResultFieldInfo* cellEnergy = findField(
        readResult.fields, "CellEnergy",
        ResultFieldAssociation::Cell);
    const ResultFieldInfo* stress = findField(
        readResult.fields, "Stress",
        ResultFieldAssociation::Cell);
    expect(temperature != nullptr && temperature->componentCount == 1,
           "应识别节点标量");
    expect(displacement != nullptr &&
               displacement->componentCount == 3,
           "应识别节点三分量矢量");
    expect(cellEnergy != nullptr && cellEnergy->tupleCount == 1,
           "应识别单元标量");
    expect(stress != nullptr && stress->stressTensor,
           "应根据名称及六分量识别应力");

    const ResultFieldProcessor processor;
    if (displacement != nullptr) {
        const auto options = processor.scalarOptions(*displacement);
        expect(options.size() == 4,
               "三分量矢量应提供 X、Y、Z 和模");
        const ResultScalarOption* magnitude = findOption(
            options, ResultScalarOperation::Magnitude);
        expect(magnitude != nullptr, "应提供矢量模");
        if (magnitude != nullptr) {
            const int arraysBefore =
                readResult.grid->GetPointData()->GetNumberOfArrays();
            const auto magnitudeResult =
                processor.buildScalar(readResult.grid, *magnitude);
            expect(magnitudeResult.success, "矢量模计算应成功");
            expect(closeTo(magnitudeResult.statistics.minimum, 0.0),
                   "矢量模最小值应为 0");
            expect(closeTo(magnitudeResult.statistics.maximum, 3.0),
                   "矢量模最大值应为 3");
            expect(magnitudeResult.statistics.minimumId == 10,
                   "极小值应使用非连续全局节点 ID 10");
            expect(magnitudeResult.statistics.maximumId == 40,
                   "极大值应使用非连续全局节点 ID 40");
            expect(magnitudeResult.statistics.hasPositions &&
                       closeTo(
                           magnitudeResult.statistics
                               .maximumPosition[2],
                           1.0),
                   "节点极值应包含正确坐标");
            const auto repeated =
                processor.buildScalar(readResult.grid, *magnitude);
            expect(repeated.success, "重复派生应成功");
            expect(readResult.grid->GetPointData()
                       ->GetNumberOfArrays() == arraysBefore + 1,
                   "重复派生不应无限增加同名数组");
        }
    }

    if (cellEnergy != nullptr) {
        const auto options = processor.scalarOptions(*cellEnergy);
        const auto scalarResult =
            processor.buildScalar(readResult.grid, options.front());
        expect(scalarResult.success &&
                   closeTo(scalarResult.statistics.minimum, 42.0) &&
                   closeTo(scalarResult.statistics.maximum, 42.0),
               "单元标量极值应为 42");
        expect(scalarResult.statistics.minimumId == 1,
               "无单元全局 ID 时应使用从 1 开始的序号");
        expect(!scalarResult.statistics.hasPositions,
               "本阶段单元极值不计算中心坐标");
    }

    if (stress != nullptr) {
        const auto options = processor.scalarOptions(*stress);
        expect(options.size() == 7,
               "明确应力应提供六分量和 Von Mises");
        const ResultScalarOption* vonMises = findOption(
            options, ResultScalarOperation::VonMises);
        expect(vonMises != nullptr, "应提供 Von Mises");
        if (vonMises != nullptr) {
            const auto stressResult =
                processor.buildScalar(readResult.grid, *vonMises);
            expect(stressResult.success,
                   "Von Mises 计算应成功");
            expect(closeTo(stressResult.statistics.maximum,
                           std::sqrt(4777.0)),
                   "Von Mises 数值应符合人工计算");
        }
    }

    expect(processor.validateDisplacementField(
               readResult.grid, "Displacement").success,
           "三分量节点位移应通过校验");
    expect(!processor.validateDisplacementField(
                readResult.grid, "Temperature").success,
           "单分量数组不能作为位移");
    expect(processor.validateDeformationScale(0.0).success,
           "零倍率应有效");
    expect(processor.validateDeformationScale(2.5).success,
           "有限正倍率应有效");
    expect(!processor.validateDeformationScale(-1.0).success,
           "负倍率应拒绝");
    expect(!processor.validateDeformationScale(
                std::numeric_limits<double>::infinity()).success,
           "无穷倍率应拒绝");

    double originalPoint[3]{};
    readResult.grid->GetPoint(1, originalPoint);
    auto warp = vtkSmartPointer<vtkWarpVector>::New();
    warp->SetInputData(readResult.grid);
    warp->SetInputArrayToProcess(
        0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_POINTS,
        "Displacement");
    warp->SetScaleFactor(2.0);
    warp->Update();
    double warpedPoint[3]{};
    warp->GetOutput()->GetPoint(1, warpedPoint);
    double unchangedPoint[3]{};
    readResult.grid->GetPoint(1, unchangedPoint);
    expect(closeTo(warpedPoint[0], 3.0),
           "两倍位移应生成正确变形坐标");
    expect(closeTo(unchangedPoint[0], originalPoint[0]) &&
               closeTo(unchangedPoint[1], originalPoint[1]) &&
               closeTo(unchangedPoint[2], originalPoint[2]),
           "位移变形不得修改原始节点坐标");
    warp->SetScaleFactor(0.0);
    warp->Update();
    warp->GetOutput()->GetPoint(1, warpedPoint);
    expect(closeTo(warpedPoint[0], originalPoint[0]),
           "零倍率应恢复未变形坐标");

    auto partialInvalid = vtkSmartPointer<vtkDoubleArray>::New();
    partialInvalid->SetName("PartialInvalid");
    partialInvalid->SetNumberOfComponents(1);
    partialInvalid->SetNumberOfTuples(4);
    partialInvalid->SetValue(0, 1.0);
    partialInvalid->SetValue(
        1, std::numeric_limits<double>::quiet_NaN());
    partialInvalid->SetValue(2, 3.0);
    partialInvalid->SetValue(3, 2.0);
    readResult.grid->GetPointData()->AddArray(partialInvalid);
    ResultScalarOption invalidOption{
        .displayName = "PartialInvalid",
        .sourceArrayName = "PartialInvalid",
        .derivedArrayName = "QTCAE.Derived.PartialInvalid.Component0",
        .association = ResultFieldAssociation::Point,
        .operation = ResultScalarOperation::Component,
        .component = 0
    };
    const auto partialResult =
        processor.buildScalar(readResult.grid, invalidOption);
    expect(partialResult.success &&
               partialResult.statistics.ignoredValueCount == 1 &&
               closeTo(partialResult.statistics.minimum, 1.0) &&
               closeTo(partialResult.statistics.maximum, 3.0),
           "部分非法值应忽略并计算有限极值");

    for (vtkIdType index = 0; index < 4; ++index) {
        partialInvalid->SetValue(
            index, std::numeric_limits<double>::quiet_NaN());
    }
    const auto allInvalidResult =
        processor.buildScalar(readResult.grid, invalidOption);
    expect(!allInvalidResult.success,
           "全部非法值的字段应拒绝显示");

    auto mismatch = vtkSmartPointer<vtkDoubleArray>::New();
    mismatch->SetName("Mismatch");
    mismatch->SetNumberOfComponents(1);
    mismatch->SetNumberOfTuples(3);
    readResult.grid->GetPointData()->AddArray(mismatch);
    ResultScalarOption mismatchOption{
        .displayName = "Mismatch",
        .sourceArrayName = "Mismatch",
        .derivedArrayName = "QTCAE.Derived.Mismatch.Component0",
        .association = ResultFieldAssociation::Point,
        .operation = ResultScalarOperation::Component,
        .component = 0
    };
    expect(!processor.buildScalar(
                readResult.grid, mismatchOption).success,
           "元组数量不匹配应拒绝");

    const VtkReadResult invalidFile = reader.read(
        dataDirectory / "invalid_tuple_count.vtu");
    expect(!invalidFile.success,
           "非法元组数量 VTU 应拒绝");
    expect(!reader.read(dataDirectory / "missing.vtu").success,
           "不存在的 VTU 应拒绝");
    expect(!reader.read(dataDirectory / "tetra_result.txt").success,
           "不支持的扩展名应拒绝");

    const VtkReadResult legacyResult =
        reader.read(dataDirectory / "tetra_result.vtk");
    expect(legacyResult.success,
           "有效 ASCII Legacy VTK 应读取成功");
    expect(legacyResult.grid != nullptr &&
               legacyResult.grid->GetNumberOfPoints() == 4 &&
               legacyResult.grid->GetNumberOfCells() == 1,
           "Legacy VTK 网格规模应正确");
    expect(findField(
               legacyResult.fields, "Temperature",
               ResultFieldAssociation::Point) != nullptr,
           "Legacy VTK 应枚举节点标量");
    expect(findField(
               legacyResult.fields, "Displacement",
               ResultFieldAssociation::Point) != nullptr,
           "Legacy VTK 应枚举节点矢量");
    const ResultFieldInfo* legacyStress = findField(
        legacyResult.fields, "Stress",
        ResultFieldAssociation::Cell);
    expect(legacyStress != nullptr && legacyStress->stressTensor,
           "Legacy VTK 应识别明确命名的应力数组");

    const std::filesystem::path binaryPath =
        std::filesystem::temp_directory_path() /
        "qtcae_tetra_result_binary.vtk";
    if (legacyResult.grid != nullptr) {
        auto writer =
            vtkSmartPointer<vtkUnstructuredGridWriter>::New();
        writer->SetFileName(binaryPath.string().c_str());
        writer->SetInputData(legacyResult.grid);
        writer->SetFileTypeToBinary();
        expect(writer->Write() == 1,
               "应能创建 Binary Legacy VTK 测试文件");
        const VtkReadResult binaryResult =
            reader.read(binaryPath);
        expect(binaryResult.success &&
                   binaryResult.grid != nullptr &&
                   binaryResult.grid->GetNumberOfPoints() == 4 &&
                   binaryResult.grid->GetNumberOfCells() == 1,
               "Binary Legacy VTK 应读取成功");
    }
    std::error_code removeError;
    std::filesystem::remove(binaryPath, removeError);
    expect(!reader.read(
                dataDirectory / "polydata_result.vtk").success,
           "Legacy VTK 非非结构网格数据集应拒绝");

    if (failureCount == 0) {
        std::cout
            << "VTU/Legacy VTK 结果读取和字段处理测试全部通过。\n";
    }
    return failureCount == 0 ? 0 : 1;
}
