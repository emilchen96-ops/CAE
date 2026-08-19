#pragma once

#include "ResultField.hpp"
#include "VtkResultSequence.hpp"

#include <vtkSmartPointer.h>
#include <vtkType.h>

#include <string>
#include <vector>

class vtkUnstructuredGrid;

struct VtkResultSequenceLoadedPart {
    int id{-1};
    std::string name;
    vtkIdType firstCell{0};
    vtkIdType cellCount{0};
};

struct VtkResultSequenceLoadResult {
    bool success{false};
    vtkSmartPointer<vtkUnstructuredGrid> grid;
    std::vector<ResultFieldInfo> fields;
    std::vector<std::string> loadedParts;
    std::vector<VtkResultSequenceLoadedPart> parts;
    std::vector<std::string> warnings;
    std::string errorMessage;
};

class VtkResultSequenceLoader {
public:
    VtkResultSequenceLoadResult load(
        const VtkResultSequenceFrame& frame) const;
};
