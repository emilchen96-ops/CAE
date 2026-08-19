#include "VtkResultSequenceLoader.hpp"
#include "VtkResultSequenceScanner.hpp"
#include "ResultFieldProcessor.hpp"
#include "ResultHistoryExtractor.hpp"

#include <vtkUnstructuredGrid.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

int failureCount = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failureCount;
    }
}

void copyFile(const std::filesystem::path& source,
              const std::filesystem::path& destination) {
    std::filesystem::copy_file(
        source, destination,
        std::filesystem::copy_options::overwrite_existing);
}

int validateExternalSequence(int argumentCount, char** arguments) {
    const std::filesystem::path directory = arguments[1];
    const VtkResultSequenceScanner scanner;
    const auto scan = scanner.scan(directory);
    expect(scan.success, "实际 VTK 序列目录应扫描成功");
    if (!scan.success) {
        std::cerr << scan.errorMessage << '\n';
        return 1;
    }
    std::cout << "扫描帧数：" << scan.sequence.frames.size() << '\n';
    const VtkResultSequenceLoader loader;
    for (int argument = 2; argument < argumentCount; ++argument) {
        const int requested = std::stoi(arguments[argument]);
        const auto iterator = std::find_if(
            scan.sequence.frames.begin(), scan.sequence.frames.end(),
            [requested](const VtkResultSequenceFrame& frame) {
                return frame.frameNumber == requested;
            });
        expect(iterator != scan.sequence.frames.end(),
               "请求的实际帧应存在");
        if (iterator == scan.sequence.frames.end()) {
            continue;
        }
        const auto loaded = loader.load(*iterator);
        expect(loaded.success && loaded.grid != nullptr,
               "实际帧应按需加载成功");
        if (loaded.success && loaded.grid != nullptr) {
            std::cout << "帧 " << requested
                      << "：节点 " << loaded.grid->GetNumberOfPoints()
                      << "，单元 " << loaded.grid->GetNumberOfCells()
                      << "，公共字段 " << loaded.fields.size()
                      << '\n';
        } else {
            std::cerr << loaded.errorMessage << '\n';
        }
    }
    return failureCount == 0 ? 0 : 1;
}

} // namespace

int main(int argumentCount, char** arguments) {
    if (argumentCount > 1) {
        return validateExternalSequence(argumentCount, arguments);
    }

    const std::filesystem::path dataDirectory{QTCAE_TEST_DATA_DIR};
    const std::filesystem::path source =
        dataDirectory / "tetra_result.vtk";
    const std::filesystem::path temporary =
        std::filesystem::temp_directory_path() /
        "qtcae_vtk_result_sequence_tests";
    std::error_code error;
    std::filesystem::remove_all(temporary, error);
    std::filesystem::create_directories(temporary);
    for (int frame = 0; frame <= 2; ++frame) {
        copyFile(source, temporary /
            ("Body0_solid_0_" + std::to_string(frame) + ".vtk"));
        copyFile(source, temporary /
            ("Body0_bolt_0_" + std::to_string(frame) + ".vtk"));
    }
    const VtkResultSequenceScanner scanner;
    const auto scan = scanner.scan(temporary);
    expect(scan.success, "旧版 solid/bolt 序列应继续扫描成功");
    expect(scan.sequence.frames.size() == 3,
           "应识别三个帧编号");
    expect(scan.sequence.frames.front().frameNumber == 0 &&
               scan.sequence.frames.back().frameNumber == 2,
           "帧应按整数编号排序");
    expect(scan.sequence.frames.front().parts.size() == 2,
           "同一帧应包含 solid 和 bolt 两个对象");
    expect(scan.sequence.frames.front().parts[0].id > 0 &&
               scan.sequence.frames.front().parts[1].id > 0 &&
               scan.sequence.frames.front().parts[0].id !=
                   scan.sequence.frames.front().parts[1].id,
           "不同部件应具有不同的稳定 ID");
    expect(scan.sequence.frames[0].parts[0].id ==
               scan.sequence.frames[1].parts[0].id &&
               scan.sequence.frames[0].parts[1].id ==
                   scan.sequence.frames[1].parts[1].id,
           "同一部件的 ID 应在帧之间保持稳定");

    if (scan.success) {
        const VtkResultSequenceLoader loader;
        const auto frame0 = loader.load(scan.sequence.frames[0]);
        expect(frame0.success && frame0.grid != nullptr,
               "首帧应加载成功");
        if (frame0.grid != nullptr) {
            expect(frame0.grid->GetNumberOfPoints() == 8,
                   "关闭点合并后两个对象应有八个节点");
            expect(frame0.grid->GetNumberOfCells() == 2,
                   "两个对象合并后应有两个单元");
            expect(frame0.loadedParts.size() == 2,
                   "加载结果应记录两个对象");
            expect(frame0.parts.size() == 2 &&
                       frame0.parts[0].firstCell == 0 &&
                       frame0.parts[0].cellCount == 1 &&
                       frame0.parts[1].firstCell == 1 &&
                       frame0.parts[1].cellCount == 1,
                   "加载结果应记录各部件连续的单元范围");
            expect(!frame0.fields.empty(),
                   "兼容公共结果字段应保留");
        }
        const auto frame1 = loader.load(scan.sequence.frames[1]);
        expect(frame1.success && frame1.grid != nullptr,
               "切换到第二帧应加载成功");
        expect(frame0.grid != frame1.grid,
               "不同帧应生成独立网格而非预加载全部帧");
        VtkResultSequenceFrame afterRemoval = scan.sequence.frames[0];
        afterRemoval.parts.erase(afterRemoval.parts.begin());
        const auto remaining = loader.load(afterRemoval);
        expect(remaining.success && remaining.parts.size() == 1 &&
                   remaining.grid != nullptr &&
                   remaining.grid->GetNumberOfCells() == 1,
               "删除一个结果部件后应只重新加载剩余部件");
        VtkResultSequence sequenceAfterRemoval = scan.sequence;
        const int removedPartId =
            sequenceAfterRemoval.frames.front().parts.front().id;
        for (auto& frame : sequenceAfterRemoval.frames) {
            std::erase_if(
                frame.parts,
                [removedPartId](const VtkResultSequencePart& part) {
                    return part.id == removedPartId;
                });
        }
        bool allFramesKeepOnlyRemainingPart = true;
        for (const auto& frame : sequenceAfterRemoval.frames) {
            const auto reloaded = loader.load(frame);
            allFramesKeepOnlyRemainingPart =
                allFramesKeepOnlyRemainingPart && reloaded.success &&
                reloaded.parts.size() == 1 &&
                reloaded.parts.front().id != removedPartId;
        }
        expect(allFramesKeepOnlyRemainingPart,
               "从结果序列删除部件后应在所有帧移除相同稳定 ID");
        if (!frame0.fields.empty()) {
            const auto pointField = std::find_if(
                frame0.fields.begin(), frame0.fields.end(),
                [](const ResultFieldInfo& field) {
                    return field.association == ResultFieldAssociation::Point;
                });
            if (pointField != frame0.fields.end()) {
                const ResultFieldProcessor processor;
                const auto options = processor.scalarOptions(*pointField);
                if (!options.empty()) {
                    const ResultHistoryExtractor extractor;
                    const auto history = extractor.extractNodeHistory(
                        scan.sequence, options.front(), 10,
                        scan.sequence.frames.front().parts.front().id);
                    expect(history.success && history.curve.points.size() == 3,
                           "节点时间历程应按需读取全部三帧");
                    expect(history.curve.usesFrameNumberAsTime,
                           "缺少物理时间时应明确使用帧编号");
                }
            }
        }
    }

    const std::filesystem::path generic = temporary / "generic";
    std::filesystem::create_directories(generic);
    for (int frame = 0; frame <= 2; ++frame) {
        copyFile(source, generic /
            ("shell_" + std::to_string(frame) + ".vtk"));
        copyFile(source, generic /
            ("support_" + std::to_string(frame) + ".vtk"));
    }
    const auto genericScan = scanner.scan(generic);
    expect(genericScan.success &&
               genericScan.sequence.frames.size() == 3,
           "任意名称的多部件序列应扫描成功");
    expect(genericScan.sequence.frames.front().parts.size() == 2,
           "同一帧应保留全部通用部件");
    expect(genericScan.sequence.frames.front().parts[0].name == "shell" &&
               genericScan.sequence.frames.front().parts[1].name ==
                   "support",
           "通用部件名称应由帧号前的文件名得到");

    const std::filesystem::path single = temporary / "single";
    std::filesystem::create_directories(single);
    copyFile(source, single / "temperature_result.vtk");
    const auto singleScan = scanner.scan(single);
    expect(singleScan.success &&
               singleScan.sequence.frames.size() == 1 &&
               singleScan.sequence.frames.front().frameNumber == 0 &&
               singleScan.sequence.frames.front().parts.size() == 1,
           "没有数字后缀的普通 VTK 文件应作为帧 0 导入");
    expect(singleScan.sequence.frames.front().parts.front().name ==
               "temperature_result",
           "普通 VTK 文件名应作为部件名称");

    const std::filesystem::path numberedSingle =
        temporary / "numbered_single";
    std::filesystem::create_directories(numberedSingle);
    copyFile(source, numberedSingle / "result_7.vtk");
    const auto numberedSingleScan = scanner.scan(numberedSingle);
    expect(numberedSingleScan.success &&
               numberedSingleScan.sequence.frames.size() == 1 &&
               numberedSingleScan.sequence.frames.front().frameNumber == 7,
           "任意单部件数字后缀应识别为帧号");

    const std::filesystem::path duplicate =
        temporary / "duplicate";
    std::filesystem::create_directories(duplicate);
    copyFile(source, duplicate / "solid_0.vtk");
    copyFile(dataDirectory / "tetra_result.vtu",
             duplicate / "solid_0.vtu");
    expect(!scanner.scan(duplicate).success,
           "同帧同对象的重复文件应拒绝");
    const std::filesystem::path empty = temporary / "empty";
    std::filesystem::create_directories(empty);
    expect(!scanner.scan(empty).success,
           "没有 VTK/VTU 文件的目录应拒绝");
    expect(!scanner.scan(temporary / "not_found").success,
           "不存在目录应拒绝");

    std::filesystem::remove_all(temporary, error);
    if (failureCount == 0) {
        std::cout << "VTK 结果序列扫描和按帧加载测试全部通过。\n";
    }
    return failureCount == 0 ? 0 : 1;
}
