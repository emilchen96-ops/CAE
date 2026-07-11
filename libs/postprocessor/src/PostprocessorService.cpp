#include <emilcae/postprocessor/PostprocessorService.hpp>

namespace emilcae::postprocessor {

bool PostprocessorService::hasResult(const core::Result& result) const noexcept {
    return result.available;
}

} // namespace emilcae::postprocessor
