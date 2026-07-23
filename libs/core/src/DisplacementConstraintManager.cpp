#include "emilcae/core/DisplacementConstraintManager.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace emilcae::core {

namespace {

std::string_view trimAscii(std::string_view text) {
    constexpr std::string_view whitespace = " \t\r\n\f\v";
    const std::size_t first = text.find_first_not_of(whitespace);
    if (first == std::string_view::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(whitespace);
    return text.substr(first, last - first + 1);
}

bool isZero(double value) {
    return value == 0.0;
}

} // namespace

DisplacementConstraintManager::DisplacementConstraintManager(
    const NamedSelectionManager& namedSelectionManager)
    : namedSelectionManager_(namedSelectionManager) {}

ConstraintOperationResult
DisplacementConstraintManager::createConstraint(
    const DisplacementConstraint& constraint) {
    if (constraint.id >= 0) {
        return {.error = ConstraintError::InvalidId};
    }
    if (const ConstraintError error = validate(constraint);
        error != ConstraintError::None) {
        return {.error = error};
    }
    DisplacementConstraint stored = constraint;
    stored.id = nextId_++;
    stored.name = std::string(trimAscii(stored.name));
    constraints_.push_back(stored);
    return {.success = true, .constraintId = stored.id};
}

ConstraintOperationResult
DisplacementConstraintManager::updateConstraint(
    ConstraintId constraintId,
    const DisplacementConstraint& constraint) {
    auto iterator = std::find_if(
        constraints_.begin(), constraints_.end(),
        [constraintId](const DisplacementConstraint& current) {
            return current.id == constraintId;
        });
    if (iterator == constraints_.end()) {
        return {.error = ConstraintError::NotFound};
    }
    if (constraint.id >= 0 && constraint.id != constraintId) {
        return {.error = ConstraintError::InvalidId};
    }
    if (const ConstraintError error = validate(constraint, constraintId);
        error != ConstraintError::None) {
        return {.error = error};
    }
    *iterator = constraint;
    iterator->id = constraintId;
    iterator->name = std::string(trimAscii(iterator->name));
    return {.success = true, .constraintId = constraintId};
}

ConstraintOperationResult
DisplacementConstraintManager::removeConstraint(
    ConstraintId constraintId) {
    const auto iterator = std::find_if(
        constraints_.begin(), constraints_.end(),
        [constraintId](const DisplacementConstraint& constraint) {
            return constraint.id == constraintId;
        });
    if (iterator == constraints_.end()) {
        return {.error = ConstraintError::NotFound};
    }
    constraints_.erase(iterator);
    return {.success = true, .constraintId = constraintId};
}

const DisplacementConstraint*
DisplacementConstraintManager::findConstraint(
    ConstraintId constraintId) const {
    const auto iterator = std::find_if(
        constraints_.cbegin(), constraints_.cend(),
        [constraintId](const DisplacementConstraint& constraint) {
            return constraint.id == constraintId;
        });
    return iterator != constraints_.cend() ? &*iterator : nullptr;
}

std::vector<DisplacementConstraint>
DisplacementConstraintManager::constraints() const {
    return constraints_;
}

bool DisplacementConstraintManager::isNamedSelectionReferenced(
    NamedSelectionId namedSelectionId) const {
    return !constraintsUsingNamedSelection(namedSelectionId).empty();
}

std::vector<ConstraintId>
DisplacementConstraintManager::constraintsUsingNamedSelection(
    NamedSelectionId namedSelectionId) const {
    std::vector<ConstraintId> result;
    for (const DisplacementConstraint& constraint : constraints_) {
        if (constraint.namedSelectionId == namedSelectionId) {
            result.push_back(constraint.id);
        }
    }
    return result;
}

ConstraintValidity DisplacementConstraintManager::validity(
    ConstraintId constraintId,
    const ValidityProvider& validityProvider) const {
    const DisplacementConstraint* constraint =
        findConstraint(constraintId);
    if (constraint == nullptr || !validityProvider) {
        return ConstraintValidity::Invalid;
    }
    const NamedSelection* selection = namedSelectionManager_.find(
        constraint->namedSelectionId);
    if (selection == nullptr || selection->items.empty()) {
        return ConstraintValidity::Invalid;
    }
    const NamedSelectionValiditySummary summary =
        validityProvider(constraint->namedSelectionId);
    if (summary.validItemCount == 0) {
        return ConstraintValidity::Invalid;
    }
    return summary.invalidItemCount > 0
        ? ConstraintValidity::PartiallyInvalid
        : ConstraintValidity::Valid;
}

ConstraintError DisplacementConstraintManager::validate(
    const DisplacementConstraint& constraint,
    ConstraintId excludedId) const {
    const std::string_view normalizedName = trimAscii(constraint.name);
    if (normalizedName.empty()) {
        return ConstraintError::EmptyName;
    }
    for (const DisplacementConstraint& current : constraints_) {
        if (current.id != excludedId && current.name == normalizedName) {
            return ConstraintError::DuplicateName;
        }
    }
    const NamedSelection* selection = namedSelectionManager_.find(
        constraint.namedSelectionId);
    if (selection == nullptr || selection->items.empty()) {
        return ConstraintError::InvalidNamedSelection;
    }
    if (!std::isfinite(constraint.ux.value) ||
        !std::isfinite(constraint.uy.value) ||
        !std::isfinite(constraint.uz.value)) {
        return ConstraintError::NonFiniteValue;
    }
    if (constraint.type == ConstraintType::Fixed) {
        if (!constraint.ux.constrained || !constraint.uy.constrained ||
            !constraint.uz.constrained ||
            !isZero(constraint.ux.value) ||
            !isZero(constraint.uy.value) ||
            !isZero(constraint.uz.value)) {
            return ConstraintError::InvalidFixedConstraint;
        }
    } else if (!constraint.ux.constrained &&
               !constraint.uy.constrained &&
               !constraint.uz.constrained) {
        return ConstraintError::NoConstrainedDof;
    }
    return ConstraintError::None;
}

} // namespace emilcae::core
