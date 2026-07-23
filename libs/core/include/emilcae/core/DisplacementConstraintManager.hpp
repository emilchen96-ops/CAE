#pragma once

#include "emilcae/core/DisplacementConstraint.hpp"
#include "emilcae/core/NamedSelectionManager.hpp"

#include <functional>
#include <vector>

namespace emilcae::core {

class DisplacementConstraintManager {
public:
    using ValidityProvider =
        std::function<NamedSelectionValiditySummary(NamedSelectionId)>;

    explicit DisplacementConstraintManager(
        const NamedSelectionManager& namedSelectionManager);

    ConstraintOperationResult createConstraint(
        const DisplacementConstraint& constraint);
    ConstraintOperationResult updateConstraint(
        ConstraintId constraintId,
        const DisplacementConstraint& constraint);
    ConstraintOperationResult removeConstraint(ConstraintId constraintId);

    const DisplacementConstraint* findConstraint(
        ConstraintId constraintId) const;
    std::vector<DisplacementConstraint> constraints() const;
    bool isNamedSelectionReferenced(
        NamedSelectionId namedSelectionId) const;
    std::vector<ConstraintId> constraintsUsingNamedSelection(
        NamedSelectionId namedSelectionId) const;
    ConstraintValidity validity(
        ConstraintId constraintId,
        const ValidityProvider& validityProvider) const;

private:
    ConstraintError validate(
        const DisplacementConstraint& constraint,
        ConstraintId excludedId = -1) const;

    const NamedSelectionManager& namedSelectionManager_;
    std::vector<DisplacementConstraint> constraints_;
    ConstraintId nextId_{1};
};

} // namespace emilcae::core
