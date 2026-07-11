#pragma once

#include <string>

namespace emilcae::solver_interface {

struct SolverJob {
    std::string id;
};

enum class SolverStatus {
    pending,
    running,
    completed,
    failed
};

struct SolverResult {
    SolverStatus status{SolverStatus::pending};
    std::string message;
};

class ISolverBackend {
public:
    virtual ~ISolverBackend() = default;
    [[nodiscard]] virtual SolverResult run(const SolverJob& job) = 0;
};

} // namespace emilcae::solver_interface
