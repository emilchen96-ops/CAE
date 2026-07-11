#pragma once

#include <emilcae/core/DomainTypes.hpp>

namespace emilcae::postprocessor {

class PostprocessorService {
public:
    [[nodiscard]] bool hasResult(const core::Result& result) const noexcept;
};

} // namespace emilcae::postprocessor
