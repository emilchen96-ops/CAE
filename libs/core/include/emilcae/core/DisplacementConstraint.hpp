#pragma once

#include "emilcae/core/NamedSelection.hpp"

#include <cstddef>
#include <string>

namespace emilcae::core {

using ConstraintId = int;

enum class ConstraintType {
    Fixed,
    Displacement
};

struct TranslationalDofConstraint {
    bool constrained{false};
    double value{0.0};
};

struct DisplacementConstraint {
    ConstraintId id{-1};
    std::string name;
    ConstraintType type{ConstraintType::Displacement};
    NamedSelectionId namedSelectionId{-1};
    TranslationalDofConstraint ux;
    TranslationalDofConstraint uy;
    TranslationalDofConstraint uz;
};

enum class ConstraintValidity {
    Valid,
    PartiallyInvalid,
    Invalid
};

struct NamedSelectionValiditySummary {
    std::size_t validItemCount{0};
    std::size_t invalidItemCount{0};
};

enum class DisplacementUnit {
    Meter,
    Millimeter
};

double displacementToMeters(double value, DisplacementUnit unit);
double displacementFromMeters(double valueInMeters, DisplacementUnit unit);
DisplacementConstraint makeFixedConstraint(
    std::string name, NamedSelectionId namedSelectionId);

enum class ConstraintError {
    None,
    InvalidId,
    NotFound,
    EmptyName,
    DuplicateName,
    InvalidNamedSelection,
    NoConstrainedDof,
    NonFiniteValue,
    InvalidFixedConstraint
};

struct ConstraintOperationResult {
    bool success{false};
    ConstraintId constraintId{-1};
    ConstraintError error{ConstraintError::None};
};

} // namespace emilcae::core
