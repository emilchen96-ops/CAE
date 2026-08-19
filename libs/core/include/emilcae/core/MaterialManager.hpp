#pragma once

#include "emilcae/core/Material.hpp"

#include <functional>
#include <vector>

namespace emilcae::core {

class MaterialManager {
public:
    using ReferenceChecker = std::function<bool(int)>;

    explicit MaterialManager(ReferenceChecker referenceChecker = {});

    MaterialOperationResult createMaterial(const Material& material);
    MaterialOperationResult updateMaterial(int materialId,
                                           const Material& material);
    MaterialOperationResult removeMaterial(int materialId);

    const Material* findMaterial(int materialId) const;
    std::vector<Material> materials() const;

private:
    MaterialError validate(const Material& material,
                           int excludedMaterialId = -1) const;

    ReferenceChecker referenceChecker_;
    std::vector<Material> materials_;
    int nextId_{1};
};

} // namespace emilcae::core
