#include "emilcae/core/Material.hpp"

#include <utility>

namespace emilcae::core {

namespace {

double unitScale(ElasticModulusUnit unit) {
    switch (unit) {
    case ElasticModulusUnit::Pascal:
        return 1.0;
    case ElasticModulusUnit::Megapascal:
        return 1.0e6;
    case ElasticModulusUnit::Gigapascal:
        return 1.0e9;
    }
    return 1.0;
}

} // namespace

double elasticModulusToPascals(double value, ElasticModulusUnit unit) {
    return value * unitScale(unit);
}

double elasticModulusFromPascals(double value, ElasticModulusUnit unit) {
    return value / unitScale(unit);
}

bool isNearlyIncompressible(double poissonRatio) {
    return poissonRatio >= 0.49 && poissonRatio < 0.5;
}

Material makeStructuralSteelMaterial(std::string name) {
    Material material;
    material.name = std::move(name);
    material.density = 7850.0;
    material.elasticity.youngsModulus =
        elasticModulusToPascals(210.0, ElasticModulusUnit::Gigapascal);
    material.elasticity.poissonRatio = 0.30;
    return material;
}

Material makeAluminumAlloyMaterial(std::string name) {
    Material material;
    material.name = std::move(name);
    material.density = 2700.0;
    material.elasticity.youngsModulus =
        elasticModulusToPascals(70.0, ElasticModulusUnit::Gigapascal);
    material.elasticity.poissonRatio = 0.33;
    return material;
}

} // namespace emilcae::core
