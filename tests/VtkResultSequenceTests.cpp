#include "VtkResultSequenceLoader.hpp"
#include "VtkResultSequenceScanner.hpp"

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

bool containsWarning(const std::vector<std::string>& warnings,
                     const std::string& text) {
    for (const std::string& warning : warnings) {
        if (warning.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
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
    copyFile(source, temporary / "unrecognized_result.vtk");

    const VtkResultSequenceScanner scanner;
    const auto scan = scanner.scan(temporary);
    expect(scan.success, "三帧 solid/bolt 序列应扫描成功");
    expect(scan.sequence.frames.size() == 3,
           "应识别三个帧编号");
    expect(scan.sequence.frames.front().frameNumber == 0 &&
               scan.sequence.frames.back().frameNumber == 2,
           "帧应按整数编号排序");
    expect(scan.sequence.frames.front().parts.size() == 2,
           "同一帧应包含 solid 和 bolt 两个对象");
    expect(containsWarning(scan.sequence.warnings, "已忽略"),
           "无法识别命名的 VTK 文件应给出警告");

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
            expect(!frame0.fields.empty(),
                   "兼容公共结果字段应保留");
        }
        const auto frame1 = loader.load(scan.sequence.frames[1]);
        expect(frame1.success && frame1.grid != nullptr,
               "切换到第二帧应加载成功");
        expect(frame0.grid != frame1.grid,
               "不同帧应生成独立网格而非预加载全部帧");
    }

    const std::filesystem::path missingPart =
        temporary / "missing_part";
    std::filesystem::create_directories(missingPart);
    copyFile(source, missingPart / "solid_7.vtk");
    const auto missingScan = scanner.scan(missingPart);
    expect(missingScan.success &&
               missingScan.sequence.frames.size() == 1,
           "缺少一个对象时仍应识别可用帧");
    expect(containsWarning(missingScan.sequence.warnings, "缺少 bolt"),
           "缺少 bolt 应提供明确警告");

    const std::filesystem::path duplicate =
        temporary / "duplicate";
    std::filesystem::create_directories(duplicate);
    copyFile(source, duplicate / "solid_0.vtk");
    copyFile(dataDirectory / "tetra_result.vtu",
             duplicate / "solid_0.vtu");
    expect(!scanner.scan(duplicate).success,
           "同帧同对象的重复文件应拒绝");
    expect(!scanner.scan(temporary / "not_found").success,
           "不存在目录应拒绝");

    std::filesystem::remove_all(temporary, error);
    if (failureCount == 0) {
        std::cout << "VTK 结果序列扫描和按帧加载测试全部通过。\n";
    }
    return failureCount == 0 ? 0 : 1;
}
