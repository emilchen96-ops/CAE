#pragma once

#include "ResultField.hpp"

#include <vtkSmartPointer.h>

#include <string>
#include <vector>

class vtkDataArray;
class vtkUnstructuredGrid;

struct ResultScalarBuildResult {
    bool success{false};
    std::string arrayName;
    ResultScalarStatistics statistics;
    std::vector<std::string> warnings;
    std::string errorMessage;
};
struct ResultValidationResult {
    bool success{false};
    std::string errorMessage;
};

class ResultFieldProcessor {
public:
    std::vector<ResultScalarOption> scalarOptions(
        const ResultFieldInfo& field) const;

    ResultScalarBuildResult buildScalar(
        vtkUnstructuredGrid* grid,
        const ResultScalarOption& option) const;

    ResultValidationResult validateDisplacementField(
        vtkUnstructuredGrid* grid,
        const std::string& arrayName) const;

    ResultValidationResult validateDeformationScale(double scale) const;

private:
    vtkDataArray* findArray(
        vtkUnstructuredGrid* grid,
        const std::string& name,
        ResultFieldAssociation association) const;
};
