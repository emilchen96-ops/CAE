#pragma once

#include "ResultField.hpp"
#include "ResultHistory.hpp"
#include "VtkResultSequence.hpp"

class ResultHistoryExtractor {
public:
    HistoryResult extractNodeHistory(
        const VtkResultSequence& sequence,
        const ResultScalarOption& option,
        long long nodeId, int partId = -1) const;
};
