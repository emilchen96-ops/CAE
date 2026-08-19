#include "emilcae/core/SolidSectionManager.hpp"

#include <algorithm>
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

SolidSectionManager::SolidSectionManager(
    const MaterialManager& materialManager,
    ReferenceChecker referenceChecker)
    : materialManager_(materialManager),
      referenceChecker_(std::move(referenceChecker)) {}

SolidSectionOperationResult SolidSectionManager::createSection(
    const std::string& name, MaterialId materialId) {
    const SolidSectionError error = validate(name, materialId);
    if (error != SolidSectionError::None) {
        return {.error = error};
    }
    const SolidSectionId id = nextId_++;
    sections_.push_back(
        {.id = id,
         .name = std::string(trimAscii(name)),
         .materialId = materialId});
    return {.success = true, .sectionId = id};
}

SolidSectionOperationResult SolidSectionManager::updateSection(
    SolidSectionId sectionId, const std::string& name,
    MaterialId materialId) {
    auto iterator = std::find_if(
        sections_.begin(), sections_.end(),
        [sectionId](const SolidSection& section) {
            return section.id == sectionId;
        });
    if (iterator == sections_.end()) {
        return {.error = SolidSectionError::NotFound};
    }
    const SolidSectionError error = validate(name, materialId, sectionId);
    if (error != SolidSectionError::None) {
        return {.error = error};
    }
    iterator->name = std::string(trimAscii(name));
    iterator->materialId = materialId;
    return {.success = true, .sectionId = sectionId};
}

SolidSectionOperationResult SolidSectionManager::removeSection(
    SolidSectionId sectionId) {
    const auto iterator = std::find_if(
        sections_.begin(), sections_.end(),
        [sectionId](const SolidSection& section) {
            return section.id == sectionId;
        });
    if (iterator == sections_.end()) {
        return {.error = SolidSectionError::NotFound};
    }
    if (referenceChecker_ && referenceChecker_(sectionId)) {
        return {.error = SolidSectionError::InUse};
    }
    sections_.erase(iterator);
    return {.success = true, .sectionId = sectionId};
}

const SolidSection* SolidSectionManager::findSection(
    SolidSectionId sectionId) const {
    const auto iterator = std::find_if(
        sections_.begin(), sections_.end(),
        [sectionId](const SolidSection& section) {
            return section.id == sectionId;
        });
    return iterator != sections_.end() ? &*iterator : nullptr;
}

std::vector<SolidSection> SolidSectionManager::sections() const {
    return sections_;
}

std::vector<SolidSection> SolidSectionManager::sectionsUsingMaterial(
    MaterialId materialId) const {
    std::vector<SolidSection> result;
    for (const SolidSection& section : sections_) {
        if (section.materialId == materialId) {
            result.push_back(section);
        }
    }
    return result;
}

bool SolidSectionManager::isMaterialReferenced(MaterialId materialId) const {
    return std::any_of(
        sections_.begin(), sections_.end(),
        [materialId](const SolidSection& section) {
            return section.materialId == materialId;
        });
}

SolidSectionError SolidSectionManager::validate(
    const std::string& name, MaterialId materialId,
    SolidSectionId excludedSectionId) const {
    const std::string_view normalizedName = trimAscii(name);
    if (normalizedName.empty()) {
        return SolidSectionError::EmptyName;
    }
    if (materialId <= 0 ||
        materialManager_.findMaterial(materialId) == nullptr) {
        return SolidSectionError::InvalidMaterial;
    }
    for (const SolidSection& section : sections_) {
        if (section.id != excludedSectionId &&
            section.name == normalizedName) {
            return SolidSectionError::DuplicateName;
        }
    }
    return SolidSectionError::None;
}

} // namespace emilcae::core
