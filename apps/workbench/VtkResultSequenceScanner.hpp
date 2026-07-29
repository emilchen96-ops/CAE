#pragma once

#include "VtkResultSequence.hpp"

class VtkResultSequenceScanner {
public:
    VtkResultSequenceScanResult scan(
        const std::filesystem::path& directory) const;
};
