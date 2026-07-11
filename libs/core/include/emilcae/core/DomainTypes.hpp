#pragma once

#include <string>

namespace emilcae::core {

struct Project {
    std::string name;
};

struct Model {
    std::string name;
};

struct Analysis {
    std::string name;
};

struct SolverJob {
    std::string id;
};

struct Result {
    bool available{false};
};

struct Error {
    int code{0};
    std::string message;
};

} // namespace emilcae::core
