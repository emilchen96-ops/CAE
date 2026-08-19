#pragma once

#include <string>
#include <vector>

struct HistoryPoint {
    double time{0.0};
    double value{0.0};
};

struct HistoryCurve {
    std::string name;
    std::vector<HistoryPoint> points;
    bool usesFrameNumberAsTime{true};
    std::vector<std::string> warnings;
};

struct HistoryResult {
    bool success{false};
    HistoryCurve curve;
    std::string errorMessage;
};
