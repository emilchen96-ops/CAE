#include "emilcae/core/SolidSectionAssignmentManager.hpp"

#include <algorithm>

namespace emilcae::core {

SolidSectionAssignmentManager::SolidSectionAssignmentManager(
    const SolidSectionManager& sectionManager)
    : sectionManager_(sectionManager) {}

SectionAssignmentResult SolidSectionAssignmentManager::assign(
    SectionAssignmentTargetType targetType, int targetId,
    SolidSectionId sectionId) {
    if (targetId < 0) {
        return {.error = SectionAssignmentError::InvalidTarget};
    }
    if (sectionManager_.findSection(sectionId) == nullptr) {
        return {.error = SectionAssignmentError::InvalidSection};
    }
    for (SolidSectionAssignment& assignment : assignments_) {
        if (assignment.targetType == targetType &&
            assignment.targetId == targetId) {
            assignment.sectionId = sectionId;
            return {.success = true};
        }
    }
    assignments_.push_back(
        {.targetType = targetType,
         .targetId = targetId,
         .sectionId = sectionId});
    return {.success = true};
}

SectionAssignmentResult SolidSectionAssignmentManager::unassign(
    SectionAssignmentTargetType targetType, int targetId) {
    const auto iterator = std::find_if(
        assignments_.begin(), assignments_.end(),
        [targetType, targetId](const SolidSectionAssignment& assignment) {
            return assignment.targetType == targetType &&
                   assignment.targetId == targetId;
        });
    if (iterator == assignments_.end()) {
        return {.error = SectionAssignmentError::NotFound};
    }
    assignments_.erase(iterator);
    return {.success = true};
}

std::optional<SolidSectionId>
SolidSectionAssignmentManager::assignedSection(
    SectionAssignmentTargetType targetType, int targetId) const {
    for (const SolidSectionAssignment& assignment : assignments_) {
        if (assignment.targetType == targetType &&
            assignment.targetId == targetId) {
            return assignment.sectionId;
        }
    }
    return std::nullopt;
}

std::optional<ResolvedSolidSectionAssignment>
SolidSectionAssignmentManager::resolvedForMesh(
    int meshObjectId, std::optional<int> sourceGeometryObjectId) const {
    if (const auto direct = assignedSection(
            SectionAssignmentTargetType::MeshObject, meshObjectId)) {
        return ResolvedSolidSectionAssignment{
            .sectionId = *direct, .inheritedFromGeometry = false};
    }
    if (sourceGeometryObjectId) {
        if (const auto inherited = assignedSection(
                SectionAssignmentTargetType::GeometryObject,
                *sourceGeometryObjectId)) {
            return ResolvedSolidSectionAssignment{
                .sectionId = *inherited,
                .inheritedFromGeometry = true};
        }
    }
    return std::nullopt;
}

bool SolidSectionAssignmentManager::isSectionReferenced(
    SolidSectionId sectionId) const {
    return referenceCount(sectionId) > 0;
}

std::size_t SolidSectionAssignmentManager::referenceCount(
    SolidSectionId sectionId) const {
    return static_cast<std::size_t>(std::count_if(
        assignments_.begin(), assignments_.end(),
        [sectionId](const SolidSectionAssignment& assignment) {
            return assignment.sectionId == sectionId;
        }));
}

std::size_t SolidSectionAssignmentManager::removeAssignmentsForSection(
    SolidSectionId sectionId) {
    const std::size_t before = assignments_.size();
    std::erase_if(assignments_,
                  [sectionId](const SolidSectionAssignment& assignment) {
                      return assignment.sectionId == sectionId;
                  });
    return before - assignments_.size();
}

std::vector<SolidSectionAssignment>
SolidSectionAssignmentManager::assignments() const {
    return assignments_;
}

} // namespace emilcae::core
