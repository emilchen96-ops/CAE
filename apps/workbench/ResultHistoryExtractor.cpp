#include "ResultHistoryExtractor.hpp"

#include "ResultFieldProcessor.hpp"
#include "VtkResultSequenceLoader.hpp"
#include "VtkResultReader.hpp"

#include <vtkDataArray.h>
#include <vtkPointData.h>
#include <vtkUnstructuredGrid.h>

#include <cmath>
#include <exception>
#include <algorithm>

HistoryResult ResultHistoryExtractor::extractNodeHistory(
    const VtkResultSequence& sequence, const ResultScalarOption& option,
    long long nodeId, int partId) const {
    HistoryResult result;
    if (sequence.frames.empty()) {
        result.errorMessage = "当前没有结果帧。";
        return result;
    }
    if (option.association != ResultFieldAssociation::Point) {
        result.errorMessage = "时间历程第一版只支持节点结果。";
        return result;
    }
    if (nodeId < 0) {
        result.errorMessage = "节点 ID 无效。";
        return result;
    }
    try {
        const VtkResultSequenceLoader loader;
        const VtkResultReader reader;
        const ResultFieldProcessor processor;
        for (const auto& frame : sequence.frames) {
            vtkSmartPointer<vtkUnstructuredGrid> grid;
            if (partId >= 0) {
                const auto part = std::find_if(
                    frame.parts.begin(), frame.parts.end(),
                    [partId](const auto& candidate) {
                        return candidate.id == partId;
                    });
                if (part == frame.parts.end()) {
                    result.curve.warnings.push_back(
                        "帧 " + std::to_string(frame.frameNumber) +
                        " 缺少指定部件，已跳过。" );
                    continue;
                }
                auto read = reader.read(part->filePath);
                if (read.success) grid = read.grid;
            } else {
                auto loaded = loader.load(frame);
                if (loaded.success) grid = loaded.grid;
            }
            if (grid == nullptr) {
                result.curve.warnings.push_back(
                    "帧 " + std::to_string(frame.frameNumber) +
                    " 加载失败，已跳过。" );
                continue;
            }
            const auto built = processor.buildScalar(grid, option);
            if (!built.success) {
                result.curve.warnings.push_back(
                    "帧 " + std::to_string(frame.frameNumber) +
                    " 缺少兼容字段，已跳过。" );
                continue;
            }
            vtkDataArray* values = grid->GetPointData()->GetArray(
                built.arrayName.c_str());
            if (values == nullptr) continue;
            vtkIdType tuple = -1;
            vtkDataArray* ids = vtkDataArray::SafeDownCast(
                grid->GetPointData()->GetGlobalIds());
            if (ids != nullptr) {
                for (vtkIdType index = 0; index < ids->GetNumberOfTuples(); ++index) {
                    if (std::llround(ids->GetComponent(index, 0)) == nodeId) {
                        tuple = index; break;
                    }
                }
            } else if (nodeId > 0 && nodeId <= grid->GetNumberOfPoints()) {
                tuple = static_cast<vtkIdType>(nodeId - 1);
            }
            if (tuple < 0) {
                result.curve.warnings.push_back(
                    "帧 " + std::to_string(frame.frameNumber) +
                    " 不包含节点 " + std::to_string(nodeId) + "，已跳过。" );
                continue;
            }
            const double value = values->GetComponent(tuple, 0);
            if (!std::isfinite(value)) {
                result.curve.warnings.push_back(
                    "帧 " + std::to_string(frame.frameNumber) +
                    " 的节点结果无效，已跳过。" );
                continue;
            }
            result.curve.points.push_back(
                {static_cast<double>(frame.frameNumber), value});
        }
        if (result.curve.points.empty()) {
            result.errorMessage = "所有帧都没有可用的节点时间历程数据。";
            return result;
        }
        result.curve.name = "节点 " + std::to_string(nodeId) + " - " +
                            option.displayName;
        result.curve.warnings.push_back(
            "结果文件未提供物理时间，横轴使用帧编号。" );
        result.success = true;
    } catch (const std::exception& exception) {
        result.errorMessage = std::string("提取时间历程异常：") + exception.what();
    } catch (...) {
        result.errorMessage = "提取时间历程发生未知异常。";
    }
    return result;
}
