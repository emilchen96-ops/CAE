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
    if (stem.empty()) {
        return false;
    }
    static const std::regex legacyPattern(
        R"(^.+_(solid|bolt)_[0-9]+_([0-9]+)$)",
        std::regex::icase);
    static const std::regex numberedPattern(
        R"(^(.+)[_-]([0-9]+)$)");
    std::smatch match;
    if (std::regex_match(stem, match, legacyPattern)) {
        try {
            part = lowerAscii(match[1].str());
            frameNumber = std::stoi(match[2].str());
            return frameNumber >= 0;
        } catch (...) {
            return false;
        }
    }
    if (!std::regex_match(stem, match, numberedPattern)) {
        part = stem;
        frameNumber = 0;
        return true;
    }
    try {
        part = match[1].str();
        frameNumber = std::stoi(match[2].str());
        return !part.empty() && frameNumber >= 0;
    } catch (...) {
        return false;
    }
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
            const std::string partKey = lowerAscii(part);
            if (frameParts.contains(partKey)) {
                result.errorMessage =
                    "同一帧存在重复的“" + part + "”结果文件：帧 " +
                    std::to_string(frameNumber) + "。";
                return result;
            }
            frameParts.emplace(partKey, entry.path());
        }
        if (indexedParts.empty()) {
            result.errorMessage =
                "目录中没有找到可导入的 VTK 或 VTU 结果文件。";
            return result;
        }

        std::map<std::string, int> partIds;
        int nextPartId = 1;
        for (const auto& [frameNumber, parts] : indexedParts) {
            (void)frameNumber;
            for (const auto& [partKey, path] : parts) {
                (void)path;
                if (!partIds.contains(partKey)) {
                    partIds.emplace(partKey, nextPartId++);
                }
            }
        }

        result.sequence.directory = directory;
        for (auto& [frameNumber, parts] : indexedParts) {
            VtkResultSequenceFrame frame;
            frame.frameNumber = frameNumber;
            for (auto& [name, path] : parts) {
                std::string displayName;
                int parsedFrameNumber = -1;
                if (!parseSequenceName(path.stem().string(),
                                       displayName,
                                       parsedFrameNumber)) {
                    displayName = path.stem().string();
                }
                frame.parts.push_back(
                    {partIds.at(name), displayName, std::move(path)});
            }
            std::sort(
                frame.parts.begin(), frame.parts.end(),
                [](const auto& left, const auto& right) {
                    return lowerAscii(left.name) <
                           lowerAscii(right.name);
                });
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
