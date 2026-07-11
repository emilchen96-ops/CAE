#pragma once

#include <emilcae/core/DomainTypes.hpp>

namespace emilcae::preprocessor {

class PreprocessorService {
public:
    [[nodiscard]] core::Model createEmptyModel() const;
};

} // namespace emilcae::preprocessor
