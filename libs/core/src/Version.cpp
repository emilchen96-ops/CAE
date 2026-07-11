#include <emilcae/core/Version.hpp>

namespace emilcae::core {

std::string_view Version::text() const noexcept {
    if (major == 0 && minor == 1 && patch == 0) {
        return "0.1.0";
    }
    return "unknown";
}

} // namespace emilcae::core
