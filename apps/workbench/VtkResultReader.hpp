#pragma once

#include "ResultField.hpp"

#include <vtkSmartPointer.h>

#include <filesystem>
#include <string>
#include <vector>

class vtkUnstructuredGrid;

struct VtkReadResult {
    bool success{false};
    vtkSmartPointer<vtkUnstructuredGrid> grid;
    std::vector<ResultFieldInfo> fields;
    std::string errorMessage;
    std::vector<std::string> warnings;
};

class VtkResultReader {
public:
    VtkReadResult read(const std::filesystem::path& filePath) const;
};
