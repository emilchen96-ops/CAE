#pragma once

#include "ResultField.hpp"
#include "VtkResultSequence.hpp"

#include <vtkSmartPointer.h>

#include <string>
#include <vector>

class vtkUnstructuredGrid;

struct VtkResultSequenceLoadResult {
    bool success{false};
    vtkSmartPointer<vtkUnstructuredGrid> grid;
    std::vector<ResultFieldInfo> fields;
    std::vector<std::string> loadedParts;
    std::vector<std::string> warnings;
    std::string errorMessage;
};

class VtkResultSequenceLoader {
public:
    VtkResultSequenceLoadResult load(
        const VtkResultSequenceFrame& frame) const;
};
