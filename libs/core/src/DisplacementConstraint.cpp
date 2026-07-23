#include "emilcae/core/DisplacementConstraint.hpp"

#include <utility>

namespace emilcae::core {

double displacementToMeters(double value, DisplacementUnit unit) {
    return unit == DisplacementUnit::Millimeter ? value * 1.0e-3 : value;
}

double displacementFromMeters(double valueInMeters, DisplacementUnit unit) {
    return unit == DisplacementUnit::Millimeter
        ? valueInMeters * 1.0e3
        : valueInMeters;
}

DisplacementConstraint makeFixedConstraint(
    std::string name, NamedSelectionId namedSelectionId) {
    return {
        .name = std::move(name),
        .type = ConstraintType::Fixed,
        .namedSelectionId = namedSelectionId,
        .ux = {true, 0.0},
        .uy = {true, 0.0},
        .uz = {true, 0.0}
    };
}

} // namespace emilcae::core
