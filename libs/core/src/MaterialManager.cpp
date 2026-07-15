#include "emilcae/core/MaterialManager.hpp"

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

} // namespace

MaterialOperationResult MaterialManager::createMaterial(
    const Material& material) {
    const MaterialError error = validate(material);
    if (error != MaterialError::None) {
        return {.error = error};
    }

    int id = material.id;
    if (id > 0) {
        if (findMaterial(id) != nullptr) {
            return {.error = MaterialError::InvalidId};
        }
        nextId_ = std::max(nextId_, id + 1);
    } else {
        while (findMaterial(nextId_) != nullptr) {
            ++nextId_;
        }
        id = nextId_++;
    }

    Material stored = material;
    stored.id = id;
    stored.name = std::string(trimAscii(stored.name));
    materials_.push_back(std::move(stored));
    return {.success = true, .materialId = id};
}

MaterialOperationResult MaterialManager::updateMaterial(
    int materialId, const Material& material) {
    Material* stored = nullptr;
    for (Material& candidate : materials_) {
        if (candidate.id == materialId) {
            stored = &candidate;
            break;
        }
    }
    if (stored == nullptr) {
        return {.error = MaterialError::NotFound};
    }

    const MaterialError error = validate(material, materialId);
    if (error != MaterialError::None) {
        return {.error = error};
    }

    *stored = material;
    stored->id = materialId;
    stored->name = std::string(trimAscii(stored->name));
    return {.success = true, .materialId = materialId};
}

MaterialOperationResult MaterialManager::removeMaterial(int materialId) {
    const auto iterator = std::find_if(
        materials_.begin(), materials_.end(),
        [materialId](const Material& material) {
            return material.id == materialId;
        });
    if (iterator == materials_.end()) {
        return {.error = MaterialError::NotFound};
    }
    materials_.erase(iterator);
    return {.success = true, .materialId = materialId};
}

const Material* MaterialManager::findMaterial(int materialId) const {
    const auto iterator = std::find_if(
        materials_.begin(), materials_.end(),
        [materialId](const Material& material) {
            return material.id == materialId;
        });
    return iterator != materials_.end() ? &*iterator : nullptr;
}

std::vector<Material> MaterialManager::materials() const {
    return materials_;
}

MaterialError MaterialManager::validate(const Material& material,
                                        int excludedMaterialId) const {
    const std::string_view name = trimAscii(material.name);
    if (name.empty()) {
        return MaterialError::EmptyName;
    }
    for (const Material& existing : materials_) {
        if (existing.id != excludedMaterialId && existing.name == name) {
            return MaterialError::DuplicateName;
        }
    }
    if (!std::isfinite(material.density) || material.density <= 0.0) {
        return MaterialError::InvalidDensity;
    }
    if (!std::isfinite(material.elasticity.youngsModulus) ||
        material.elasticity.youngsModulus <= 0.0) {
        return MaterialError::InvalidYoungsModulus;
    }
    if (!std::isfinite(material.elasticity.poissonRatio) ||
        material.elasticity.poissonRatio <= -1.0 ||
        material.elasticity.poissonRatio >= 0.5) {
        return MaterialError::InvalidPoissonRatio;
    }
    return MaterialError::None;
}

} // namespace emilcae::core
