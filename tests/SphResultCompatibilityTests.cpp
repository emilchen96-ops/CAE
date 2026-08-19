#include "VtkResultSequenceLoader.hpp"
#include "VtkResultSequenceScanner.hpp"

#include <vtkUnstructuredGrid.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr
            << "SKIPPED: SPH 结果兼容性验证需要外部 SPH 结果目录参数；"
               "CTest 无参数运行不执行。\n";
        return 77; // CTest SKIP_RETURN_CODE
    }
    if (argc > 2) {
        std::cerr << "usage: SphResultCompatibilityTests <result-directory>\n";
        return EXIT_FAILURE;
    }
    const std::filesystem::path directory =
        std::filesystem::path(std::string(argv[1]));
    const VtkResultSequenceScanResult scan =
        VtkResultSequenceScanner().scan(directory);
    if (!scan.success || scan.sequence.frames.empty()) {
        std::cerr << "SPH output sequence scan failed: "
                  << scan.errorMessage << '\n';
        return EXIT_FAILURE;
    }
    const VtkResultSequenceLoadResult load =
        VtkResultSequenceLoader().load(scan.sequence.frames.back());
    if (!load.success || load.grid == nullptr ||
        load.grid->GetNumberOfPoints() <= 0 ||
        load.grid->GetNumberOfCells() <= 0) {
        std::cerr << "SPH result frame load failed: "
                  << load.errorMessage << '\n';
        return EXIT_FAILURE;
    }
    const auto hasField = [&load](const std::string& name, int components) {
        return std::any_of(
            load.fields.begin(), load.fields.end(),
            [&name, components](const ResultFieldInfo& field) {
                return field.name == name &&
                       field.componentCount == components &&
                       field.association == ResultFieldAssociation::Point;
            });
    };
    if (!hasField("displacement", 3) ||
        !hasField("von_mises_stress", 1)) {
        std::cerr << "SPH displacement or von-Mises field is unavailable\n";
        return EXIT_FAILURE;
    }
    std::cout << "frames=" << scan.sequence.frames.size()
              << " points=" << load.grid->GetNumberOfPoints()
              << " cells=" << load.grid->GetNumberOfCells()
              << " fields=" << load.fields.size() << '\n';
    return EXIT_SUCCESS;
}
