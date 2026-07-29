#include "VtkResultSequenceScanner.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <map>
#include <regex>
#include <system_error>

namespace {

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return value;
}

bool parseSequenceName(const std::string& stem, std::string& part,
                       int& frameNumber) {
    static const std::regex fullPattern(
        R"(^.+_(solid|bolt)_[0-9]+_([0-9]+)$)",
        std::regex::icase);
    static const std::regex compactPattern(
        R"(^(solid|bolt)_([0-9]+)$)", std::regex::icase);
    static const std::regex prefixedPattern(
        R"(^.+_(solid|bolt)_([0-9]+)$)", std::regex::icase);
    std::smatch match;
    if (!std::regex_match(stem, match, fullPattern) &&
        !std::regex_match(stem, match, compactPattern) &&
        !std::regex_match(stem, match, prefixedPattern)) {
        return false;
    }
    try {
        part = lowerAscii(match[1].str());
        frameNumber = std::stoi(match[2].str());
        return frameNumber >= 0;
    } catch (...) {
        return false;
    }
}

int partRank(const std::string& name) {
    return name == "solid" ? 0 : 1;
}

} // namespace

VtkResultSequenceScanResult VtkResultSequenceScanner::scan(
    const std::filesystem::path& directory) const {
    VtkResultSequenceScanResult result;
    try {
        if (directory.empty()) {
            result.errorMessage = "VTK 结果序列目录为空。";
            return result;
        }
        std::error_code error;
        if (!std::filesystem::exists(directory, error) || error) {
            result.errorMessage = "VTK 结果序列目录不存在。";
            return result;
        }
        if (!std::filesystem::is_directory(directory, error) || error) {
            result.errorMessage = "选择的路径不是目录。";
            return result;
        }

        std::map<int, std::map<std::string, std::filesystem::path>>
            indexedParts;
        for (const auto& entry :
             std::filesystem::directory_iterator(directory)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const std::string extension =
                lowerAscii(entry.path().extension().string());
            if (extension != ".vtk" && extension != ".vtu") {
                continue;
            }
            std::string part;
            int frameNumber = -1;
            if (!parseSequenceName(
                    entry.path().stem().string(), part, frameNumber)) {
                result.sequence.warnings.push_back(
                    "已忽略无法识别帧编号或对象类型的文件：" +
                    entry.path().filename().string());
                continue;
            }
            auto& frameParts = indexedParts[frameNumber];
            if (frameParts.contains(part)) {
                result.errorMessage =
                    "同一帧存在重复的“" + part + "”结果文件：帧 " +
                    std::to_string(frameNumber) + "。";
                return result;
            }
            frameParts.emplace(part, entry.path());
        }
        if (indexedParts.empty()) {
            result.errorMessage =
                "目录中没有识别到 solid 或 bolt 的 VTK 结果帧。";
            return result;
        }

        result.sequence.directory = directory;
        for (auto& [frameNumber, parts] : indexedParts) {
            VtkResultSequenceFrame frame;
            frame.frameNumber = frameNumber;
            for (auto& [name, path] : parts) {
                frame.parts.push_back({name, std::move(path)});
            }
            std::sort(
                frame.parts.begin(), frame.parts.end(),
                [](const auto& left, const auto& right) {
                    return partRank(left.name) < partRank(right.name);
                });
            if (!parts.contains("solid")) {
                result.sequence.warnings.push_back(
                    "帧 " + std::to_string(frameNumber) +
                    " 缺少 solid 结果文件。");
            }
            if (!parts.contains("bolt")) {
                result.sequence.warnings.push_back(
                    "帧 " + std::to_string(frameNumber) +
                    " 缺少 bolt 结果文件。");
            }
            result.sequence.frames.push_back(std::move(frame));
        }
        result.success = true;
    } catch (const std::exception& exception) {
        result.errorMessage =
            std::string("扫描 VTK 结果序列异常：") + exception.what();
    } catch (...) {
        result.errorMessage = "扫描 VTK 结果序列发生未知异常。";
    }
    return result;
}
