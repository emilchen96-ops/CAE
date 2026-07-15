#pragma once

#include "emilcae/core/Material.hpp"

#include <vector>

namespace emilcae::core {

class MaterialManager {
public:
    MaterialOperationResult createMaterial(const Material& material);
    MaterialOperationResult updateMaterial(int materialId,
                                           const Material& material);
    MaterialOperationResult removeMaterial(int materialId);

    const Material* findMaterial(int materialId) const;
    std::vector<Material> materials() const;

private:
    MaterialError validate(const Material& material,
                           int excludedMaterialId = -1) const;

    std::vector<Material> materials_;
    int nextId_{1};
};

} // namespace emilcae::core
