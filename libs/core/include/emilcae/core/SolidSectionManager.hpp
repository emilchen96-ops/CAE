#pragma once

#include "emilcae/core/MaterialManager.hpp"
#include "emilcae/core/SolidSection.hpp"

#include <functional>
#include <vector>

namespace emilcae::core {

class SolidSectionManager {
public:
    using ReferenceChecker = std::function<bool(SolidSectionId)>;

    explicit SolidSectionManager(
        const MaterialManager& materialManager,
        ReferenceChecker referenceChecker = {});

    SolidSectionOperationResult createSection(const std::string& name,
                                               MaterialId materialId);
    SolidSectionOperationResult updateSection(SolidSectionId sectionId,
                                               const std::string& name,
                                               MaterialId materialId);
    SolidSectionOperationResult removeSection(SolidSectionId sectionId);

    const SolidSection* findSection(SolidSectionId sectionId) const;
    std::vector<SolidSection> sections() const;
    std::vector<SolidSection> sectionsUsingMaterial(MaterialId materialId) const;
    bool isMaterialReferenced(MaterialId materialId) const;

private:
    SolidSectionError validate(const std::string& name,
                               MaterialId materialId,
                               SolidSectionId excludedSectionId = -1) const;

    const MaterialManager& materialManager_;
    ReferenceChecker referenceChecker_;
    std::vector<SolidSection> sections_;
    SolidSectionId nextId_{1};
};

} // namespace emilcae::core
