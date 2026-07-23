#pragma once

#include "MeshData.hpp"

#include <vtkSmartPointer.h>

#include <string>

class vtkUnstructuredGrid;

class MeshDataToVtkConverter {
public:
    struct ConversionResult {
        bool success{false};
        vtkSmartPointer<vtkUnstructuredGrid> grid;
        std::string errorMessage;
    };

    ConversionResult convert(const MeshData& mesh) const;
};
