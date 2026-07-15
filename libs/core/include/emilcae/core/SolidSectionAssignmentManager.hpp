#pragma once

#include "emilcae/core/SolidSection.hpp"
#include "emilcae/core/SolidSectionManager.hpp"

#include <optional>
#include <vector>

namespace emilcae::core {

class SolidSectionAssignmentManager {
public:
    explicit SolidSectionAssignmentManager(
        const SolidSectionManager& sectionManager);

    SectionAssignmentResult assign(SectionAssignmentTargetType targetType,
                                   int targetId,
                                   SolidSectionId sectionId);
    SectionAssignmentResult unassign(SectionAssignmentTargetType targetType,
                                     int targetId);
    std::optional<SolidSectionId> assignedSection(
        SectionAssignmentTargetType targetType, int targetId) const;
    std::optional<ResolvedSolidSectionAssignment> resolvedForMesh(
        int meshObjectId, std::optional<int> sourceGeometryObjectId) const;

    bool isSectionReferenced(SolidSectionId sectionId) const;
    std::size_t referenceCount(SolidSectionId sectionId) const;
    std::size_t removeAssignmentsForSection(SolidSectionId sectionId);
    std::vector<SolidSectionAssignment> assignments() const;

private:
    const SolidSectionManager& sectionManager_;
    std::vector<SolidSectionAssignment> assignments_;
};

} // namespace emilcae::core
