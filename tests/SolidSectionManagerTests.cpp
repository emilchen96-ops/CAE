#include "emilcae/core/MaterialManager.hpp"
#include "emilcae/core/SolidSectionAssignmentManager.hpp"
#include "emilcae/core/SolidSectionManager.hpp"

#include <iostream>
#include <string>

using namespace emilcae::core;

namespace {

Material material(std::string name) {
    Material value;
    value.name = std::move(name);
    value.density = 7850.0;
    value.elasticity.youngsModulus = 210.0e9;
    value.elasticity.poissonRatio = 0.3;
    return value;
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

    MaterialManager materials;
    const int steelId = materials.createMaterial(material("Steel")).materialId;
    const int aluminumId =
        materials.createMaterial(material("Aluminum")).materialId;
    SolidSectionManager sections(materials);
    SolidSectionAssignmentManager assignments(sections);

    const auto steelSection = sections.createSection(" Steel Section ", steelId);
    const auto aluminumSection =
        sections.createSection("Aluminum Section", aluminumId);
    check(steelSection.success && aluminumSection.success,
          "create sections with valid materials");
    check(steelSection.sectionId != aluminumSection.sectionId,
          "generated section IDs are unique");
    check(sections.findSection(steelSection.sectionId)->name == "Steel Section",
          "trim section name");
    check(sections.createSection("Steel Section", aluminumId).error ==
              SolidSectionError::DuplicateName,
          "reject duplicate section name");
    check(sections.createSection("Invalid Material", 999).error ==
              SolidSectionError::InvalidMaterial,
          "reject missing material reference");
    check(sections.updateSection(steelSection.sectionId, "Renamed Section",
                                 aluminumId).success,
          "update section name and material");
    check(sections.findSection(steelSection.sectionId)->materialId == aluminumId,
          "updated material reference persisted");
    check(sections.isMaterialReferenced(aluminumId) &&
              sections.sectionsUsingMaterial(aluminumId).size() == 2,
          "report material references");

    constexpr int geometryId = 7;
    constexpr int meshId = 11;
    check(assignments.assign(SectionAssignmentTargetType::GeometryObject,
                             geometryId, steelSection.sectionId).success,
          "assign section to geometry");
    auto inherited = assignments.resolvedForMesh(meshId, geometryId);
    check(inherited && inherited->sectionId == steelSection.sectionId &&
              inherited->inheritedFromGeometry,
          "mesh inherits geometry assignment");
    check(assignments.assign(SectionAssignmentTargetType::MeshObject,
                             meshId, aluminumSection.sectionId).success,
          "assign section directly to mesh");
    auto direct = assignments.resolvedForMesh(meshId, geometryId);
    check(direct && direct->sectionId == aluminumSection.sectionId &&
              !direct->inheritedFromGeometry,
          "direct mesh assignment overrides inherited assignment");
    check(assignments.assign(SectionAssignmentTargetType::MeshObject,
                             meshId, steelSection.sectionId).success &&
              assignments.assignments().size() == 2,
          "reassignment replaces one target assignment");
    check(assignments.unassign(SectionAssignmentTargetType::MeshObject,
                               meshId).success &&
              assignments.resolvedForMesh(meshId, geometryId)
                  ->inheritedFromGeometry,
          "unassign direct mesh section restores inheritance");
    check(assignments.assign(SectionAssignmentTargetType::MeshObject,
                             meshId, aluminumSection.sectionId).success &&
              assignments.referenceCount(aluminumSection.sectionId) == 1,
          "count section references");
    check(assignments.removeAssignmentsForSection(
              aluminumSection.sectionId) == 1 &&
              !assignments.isSectionReferenced(aluminumSection.sectionId),
          "remove assignments before section deletion");
    check(sections.removeSection(aluminumSection.sectionId).success &&
              sections.findSection(aluminumSection.sectionId) == nullptr,
          "delete unreferenced section");
    check(!assignments.assign(SectionAssignmentTargetType::GeometryObject,
                              8, aluminumSection.sectionId).success,
          "reject assignment to deleted section");

    MaterialManager guardedMaterials;
    const int guardedMaterialId =
        guardedMaterials.createMaterial(material("Guarded Steel")).materialId;
    SolidSectionManager guardedSections(
        guardedMaterials, [](SolidSectionId) { return true; });
    const auto guardedCreate = guardedSections.createSection(
        "Guarded Section", guardedMaterialId);
    check(guardedCreate.success, "guarded manager creates sections");
    const auto guardedRemove =
        guardedSections.removeSection(guardedCreate.sectionId);
    check(!guardedRemove.success &&
              guardedRemove.error == SolidSectionError::InUse,
          "removeSection reports InUse when referenced");

    if (failures == 0) {
        std::cout << "Solid section tests passed: sections="
                  << sections.sections().size()
                  << ", assignments=" << assignments.assignments().size()
                  << '\n';
    }
    return failures == 0 ? 0 : 1;
}
