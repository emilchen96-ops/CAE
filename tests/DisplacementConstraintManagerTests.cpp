#include "emilcae/core/DisplacementConstraintManager.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <unordered_set>

using namespace emilcae::core;

int main() {
    int failures = 0;
    auto check = [&failures](bool condition, const char* name) {
        if (!condition) {
            ++failures;
            std::cerr << "FAILED: " << name << '\n';
        }
    };

    std::unordered_set<int> geometryObjects{1, 2};
    NamedSelectionManager selections(
        [&geometryObjects](int id) { return geometryObjects.contains(id); });
    const auto region = selections.create(
        "Fixed Faces", NamedSelectionEntityType::Face,
        {{1, NamedSelectionEntityType::Face, 1},
         {2, NamedSelectionEntityType::Face, 2}});
    check(region.success, "create referenced named selection");

    DisplacementConstraintManager constraints(selections);
    const DisplacementConstraint fixed = makeFixedConstraint(
        "Fixed-1", region.namedSelectionId);
    const auto fixedResult = constraints.createConstraint(fixed);
    check(fixedResult.success && fixedResult.constraintId == 1,
          "create fixed constraint");
    const DisplacementConstraint* storedFixed =
        constraints.findConstraint(fixedResult.constraintId);
    check(storedFixed != nullptr && storedFixed->ux.constrained &&
              storedFixed->uy.constrained && storedFixed->uz.constrained &&
              storedFixed->ux.value == 0.0 &&
              storedFixed->uy.value == 0.0 &&
              storedFixed->uz.value == 0.0,
          "fixed constraint locks all translations at zero");

    DisplacementConstraint displacement;
    displacement.name = "Displacement-1";
    displacement.namedSelectionId = region.namedSelectionId;
    displacement.uy = {true, 0.0};
    displacement.uz = {
        true, displacementToMeters(-2.5, DisplacementUnit::Millimeter)};
    const auto displacementResult =
        constraints.createConstraint(displacement);
    check(displacementResult.success &&
              displacementResult.constraintId != fixedResult.constraintId &&
              std::abs(constraints.findConstraint(
                           displacementResult.constraintId)->uz.value +
                       0.0025) < 1.0e-15,
          "create displacement constraint and convert mm to m");
    check(displacementFromMeters(-0.0025,
                                 DisplacementUnit::Millimeter) == -2.5,
          "convert internal meters back to millimeters");

    DisplacementConstraint updated =
        *constraints.findConstraint(displacementResult.constraintId);
    updated.ux = {true,
                  displacementToMeters(
                      1.25, DisplacementUnit::Millimeter)};
    const auto updateResult = constraints.updateConstraint(
        displacementResult.constraintId, updated);
    check(updateResult.success &&
              std::abs(constraints.findConstraint(
                           displacementResult.constraintId)->ux.value -
                       0.00125) < 1.0e-15,
          "update positive displacement and preserve id");

    DisplacementConstraint emptyName = displacement;
    emptyName.name = "  \t";
    check(constraints.createConstraint(emptyName).error ==
              ConstraintError::EmptyName,
          "reject empty constraint name");

    DisplacementConstraint noDof = displacement;
    noDof.name = "No DOF";
    noDof.uy = {};
    noDof.uz = {};
    check(constraints.createConstraint(noDof).error ==
              ConstraintError::NoConstrainedDof,
          "reject displacement without constrained direction");

    DisplacementConstraint invalidFixed = fixed;
    invalidFixed.name = "Invalid Fixed";
    invalidFixed.uz.constrained = false;
    check(constraints.createConstraint(invalidFixed).error ==
              ConstraintError::InvalidFixedConstraint,
          "reject fixed constraint with free direction");
    invalidFixed = fixed;
    invalidFixed.name = "Nonzero Fixed";
    invalidFixed.ux.value = 1.0e-3;
    check(constraints.createConstraint(invalidFixed).error ==
              ConstraintError::InvalidFixedConstraint,
          "reject nonzero fixed displacement");

    DisplacementConstraint nonFinite = displacement;
    nonFinite.name = "NaN";
    nonFinite.ux.value = std::numeric_limits<double>::quiet_NaN();
    check(constraints.createConstraint(nonFinite).error ==
              ConstraintError::NonFiniteValue,
          "reject NaN");
    nonFinite.name = "Infinity";
    nonFinite.ux.value = std::numeric_limits<double>::infinity();
    check(constraints.createConstraint(nonFinite).error ==
              ConstraintError::NonFiniteValue,
          "reject infinity");

    DisplacementConstraint duplicate = displacement;
    check(constraints.createConstraint(duplicate).error ==
              ConstraintError::DuplicateName,
          "reject duplicate constraint name");
    DisplacementConstraint missingRegion = displacement;
    missingRegion.name = "Missing Region";
    missingRegion.namedSelectionId = 999;
    check(constraints.createConstraint(missingRegion).error ==
              ConstraintError::InvalidNamedSelection,
          "reject missing named selection");

    check(constraints.isNamedSelectionReferenced(region.namedSelectionId) &&
              constraints.constraintsUsingNamedSelection(
                  region.namedSelectionId).size() == 2,
          "report named selection references");
    auto validityProvider = [&selections](NamedSelectionId id) {
        const NamedSelection* selection = selections.find(id);
        if (selection == nullptr) {
            return NamedSelectionValiditySummary{};
        }
        const std::size_t invalid =
            selections.invalidGeometryReferenceCount(id);
        return NamedSelectionValiditySummary{
            selection->items.size() - invalid, invalid};
    };
    check(constraints.validity(fixedResult.constraintId,
                               validityProvider) ==
              ConstraintValidity::Valid,
          "valid constraint region");
    geometryObjects.erase(2);
    check(constraints.validity(fixedResult.constraintId,
                               validityProvider) ==
              ConstraintValidity::PartiallyInvalid,
          "partially invalid after one geometry deletion");
    geometryObjects.erase(1);
    check(constraints.validity(fixedResult.constraintId,
                               validityProvider) ==
              ConstraintValidity::Invalid,
          "invalid after all referenced geometry is deleted");

    check(constraints.removeConstraint(fixedResult.constraintId).success &&
              constraints.findConstraint(fixedResult.constraintId) == nullptr,
          "delete constraint without deleting named selection");
    check(selections.find(region.namedSelectionId) != nullptr,
          "named selection remains after constraint deletion");

    if (failures == 0) {
        std::cout << "Displacement constraint tests passed: constraints="
                  << constraints.constraints().size() << '\n';
    }
    return failures == 0 ? 0 : 1;
}
