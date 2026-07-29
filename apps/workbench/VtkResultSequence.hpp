#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct VtkResultSequencePart {
    std::string name;
    std::filesystem::path filePath;
};

struct VtkResultSequenceFrame {
    int frameNumber{-1};
    std::vector<VtkResultSequencePart> parts;
};

struct VtkResultSequence {
    std::filesystem::path directory;
    std::vector<VtkResultSequenceFrame> frames;
    std::vector<std::string> warnings;
};

struct VtkResultSequenceScanResult {
    bool success{false};
    VtkResultSequence sequence;
    std::string errorMessage;
};
