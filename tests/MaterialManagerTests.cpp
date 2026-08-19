#include "emilcae/core/MaterialManager.hpp"

#include <cmath>
#include <iostream>
#include <limits>

using emilcae::core::ElasticModulusUnit;
using emilcae::core::Material;
using emilcae::core::MaterialError;
using emilcae::core::MaterialManager;

namespace {

Material validMaterial(std::string name = "Structural Steel") {
    Material material;
    material.name = std::move(name);
    material.density = 7850.0;
    material.elasticity.youngsModulus =
        emilcae::core::elasticModulusToPascals(
            210.0, ElasticModulusUnit::Gigapascal);
    material.elasticity.poissonRatio = 0.30;
    return material;
}

} // namespace

int main() {
    int failures = 0;
    auto check = [&failures](bool condition, const char* name) {
        if (!condition) {
            ++failures;
            std::cerr << "FAILED: " << name << '\n';
        }
    };

    MaterialManager manager;
    const auto steelResult = manager.createMaterial(validMaterial());
    check(steelResult.success && steelResult.materialId == 1,
          "create material with generated ID");
    const Material* steel = manager.findMaterial(steelResult.materialId);
    check(steel != nullptr && steel->density == 7850.0,
          "find created material");

    const auto aluminumResult = manager.createMaterial(
        validMaterial("Aluminum Alloy"));
    check(aluminumResult.success &&
              aluminumResult.materialId != steelResult.materialId,
          "generated IDs are unique");

    const auto duplicateName = manager.createMaterial(validMaterial());
    check(!duplicateName.success &&
              duplicateName.error == MaterialError::DuplicateName,
          "reject exact duplicate name");
    const auto caseVariant = manager.createMaterial(
        validMaterial("structural steel"));
    check(caseVariant.success,
          "name comparison is explicitly case-sensitive");

    Material explicitId = validMaterial("Explicit ID");
    explicitId.id = 50;
    check(manager.createMaterial(explicitId).success,
          "accept unused explicit ID");
    explicitId.name = "Duplicate Explicit ID";
    const auto duplicateId = manager.createMaterial(explicitId);
    check(!duplicateId.success &&
              duplicateId.error == MaterialError::InvalidId,
          "reject duplicate explicit ID");

    Material updated = *manager.findMaterial(aluminumResult.materialId);
    updated.density = 2700.0;
    updated.elasticity.youngsModulus =
        emilcae::core::elasticModulusToPascals(
            70.0, ElasticModulusUnit::Gigapascal);
    updated.elasticity.poissonRatio = 0.33;
    check(manager.updateMaterial(aluminumResult.materialId, updated).success,
          "update material");
    check(manager.findMaterial(aluminumResult.materialId)->density == 2700.0,
          "updated value persisted");

    updated.name = "Structural Steel";
    const auto duplicateUpdate = manager.updateMaterial(
        aluminumResult.materialId, updated);
    check(!duplicateUpdate.success &&
              duplicateUpdate.error == MaterialError::DuplicateName,
          "reject duplicate name during update");

    Material invalid = validMaterial("Invalid");
    invalid.name = "   ";
    check(manager.createMaterial(invalid).error == MaterialError::EmptyName,
          "reject blank name");
    invalid = validMaterial("Invalid Density");
    invalid.density = 0.0;
    check(manager.createMaterial(invalid).error ==
              MaterialError::InvalidDensity,
          "reject non-positive density");
    invalid = validMaterial("Invalid Modulus");
    invalid.elasticity.youngsModulus =
        std::numeric_limits<double>::infinity();
    check(manager.createMaterial(invalid).error ==
              MaterialError::InvalidYoungsModulus,
          "reject non-finite modulus");
    invalid = validMaterial("Invalid Poisson");
    invalid.elasticity.poissonRatio = 0.5;
    check(manager.createMaterial(invalid).error ==
              MaterialError::InvalidPoissonRatio,
          "reject invalid Poisson ratio");
    invalid.elasticity.poissonRatio =
        std::numeric_limits<double>::quiet_NaN();
    check(manager.createMaterial(invalid).error ==
              MaterialError::InvalidPoissonRatio,
          "reject NaN Poisson ratio");

    const double pascals = emilcae::core::elasticModulusToPascals(
        210.0, ElasticModulusUnit::Gigapascal);
    check(pascals == 210.0e9, "GPa to Pa conversion");
    check(std::abs(emilcae::core::elasticModulusFromPascals(
                       pascals, ElasticModulusUnit::Megapascal) -
                   210000.0) < 1.0e-9,
          "Pa to MPa conversion");
    check(emilcae::core::isNearlyIncompressible(0.499) &&
              !emilcae::core::isNearlyIncompressible(0.48),
          "near-incompressible warning threshold");

    const Material steelTemplate =
        emilcae::core::makeStructuralSteelMaterial("Steel Template");
    const Material aluminumTemplate =
        emilcae::core::makeAluminumAlloyMaterial("Aluminum Template");
    check(steelTemplate.density == 7850.0 &&
              steelTemplate.elasticity.youngsModulus == 210.0e9 &&
              steelTemplate.elasticity.poissonRatio == 0.30,
          "structural steel preset values");
    check(aluminumTemplate.density == 2700.0 &&
              aluminumTemplate.elasticity.youngsModulus == 70.0e9 &&
              aluminumTemplate.elasticity.poissonRatio == 0.33,
          "aluminum alloy preset values");

    check(manager.removeMaterial(steelResult.materialId).success,
          "remove material");
    check(manager.findMaterial(steelResult.materialId) == nullptr,
          "removed material cannot be found");
    const auto missingRemove = manager.removeMaterial(steelResult.materialId);
    check(!missingRemove.success &&
              missingRemove.error == MaterialError::NotFound,
          "remove missing material reports explicit error");

    MaterialManager guardedManager([](int materialId) {
        return materialId == 1;
    });
    const auto guardedCreate = guardedManager.createMaterial(validMaterial());
    check(guardedCreate.success && guardedCreate.materialId == 1,
          "guarded manager creates materials");
    const auto guardedRemove =
        guardedManager.removeMaterial(guardedCreate.materialId);
    check(!guardedRemove.success &&
              guardedRemove.error == MaterialError::InUse,
          "removeMaterial reports InUse when referenced");

    if (failures == 0) {
        std::cout << "Material manager tests passed: materials="
                  << manager.materials().size()
                  << ", 210 GPa=" << pascals << " Pa\n";
    }
    return failures == 0 ? 0 : 1;
}
