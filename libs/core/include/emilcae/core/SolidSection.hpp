#pragma once

#include <string>

namespace emilcae::core {

using MaterialId = int;
using SolidSectionId = int;

struct SolidSection {
    SolidSectionId id{-1};
    std::string name;
    MaterialId materialId{-1};
};

enum class SolidSectionError {
    None,
    InvalidId,
    NotFound,
    EmptyName,
    DuplicateName,
    InvalidMaterial,
    InUse
};

struct SolidSectionOperationResult {
    bool success{false};
    SolidSectionId sectionId{-1};
    SolidSectionError error{SolidSectionError::None};
};

enum class SectionAssignmentTargetType {
    GeometryObject,
    MeshObject
};

struct SolidSectionAssignment {
    SectionAssignmentTargetType targetType{
        SectionAssignmentTargetType::GeometryObject};
    int targetId{-1};
    SolidSectionId sectionId{-1};
};

enum class SectionAssignmentError {
    None,
    InvalidTarget,
    InvalidSection,
    NotFound
};

struct SectionAssignmentResult {
    bool success{false};
    SectionAssignmentError error{SectionAssignmentError::None};
};

struct ResolvedSolidSectionAssignment {
    SolidSectionId sectionId{-1};
    bool inheritedFromGeometry{false};
};

} // namespace emilcae::core
