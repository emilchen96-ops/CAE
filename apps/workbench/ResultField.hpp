#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

enum class ResultFieldAssociation {
    Point,
    Cell
};
enum class ResultScalarOperation {
    Component,
    Magnitude,
    VonMises
};

struct ResultFieldInfo {
    std::string name;
    ResultFieldAssociation association{ResultFieldAssociation::Point};
    int componentCount{0};
    std::size_t tupleCount{0};
    std::vector<std::string> componentNames;
    bool stressTensor{false};
};

struct ResultScalarOption {
    std::string displayName;
    std::string sourceArrayName;
    std::string derivedArrayName;
    ResultFieldAssociation association{ResultFieldAssociation::Point};
    ResultScalarOperation operation{ResultScalarOperation::Component};
    int component{0};
};

struct ResultScalarStatistics {
    bool success{false};
    double minimum{0.0};
    double maximum{0.0};
    long long minimumId{-1};
    long long maximumId{-1};
    std::array<double, 3> minimumPosition{};
    std::array<double, 3> maximumPosition{};
    bool hasPositions{false};
    std::size_t ignoredValueCount{0};
    std::string errorMessage;
};
