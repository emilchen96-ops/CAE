#pragma once

#include <string>

namespace emilcae::core {

struct IsotropicElasticProperties {
    double youngsModulus{0.0};
    double poissonRatio{0.0};
};

struct Material {
    int id{-1};
    std::string name;
    double density{0.0};
    IsotropicElasticProperties elasticity;
};

enum class ElasticModulusUnit {
    Pascal,
    Megapascal,
    Gigapascal
};

double elasticModulusToPascals(double value, ElasticModulusUnit unit);
double elasticModulusFromPascals(double value, ElasticModulusUnit unit);
bool isNearlyIncompressible(double poissonRatio);
Material makeStructuralSteelMaterial(std::string name);
Material makeAluminumAlloyMaterial(std::string name);

enum class MaterialError {
    None,
    InvalidId,
    NotFound,
    EmptyName,
    DuplicateName,
    InvalidDensity,
    InvalidYoungsModulus,
    InvalidPoissonRatio,
    InUse
};

struct MaterialOperationResult {
    bool success{false};
    int materialId{-1};
    MaterialError error{MaterialError::None};
};

} // namespace emilcae::core
