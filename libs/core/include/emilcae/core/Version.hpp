#pragma once

#include <string_view>

namespace emilcae::core {

struct Version {
    int major;
    int minor;
    int patch;

    [[nodiscard]] static constexpr Version current() noexcept {
        return {0, 1, 0};
    }

    [[nodiscard]] std::string_view text() const noexcept;
};

} // namespace emilcae::core
