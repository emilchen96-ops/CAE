#pragma once

#include <string>
#include <vector>

namespace emilcae::core {

using NamedSelectionId = int;

enum class NamedSelectionEntityType {
    Object,
    Vertex,
    Edge,
    Face,
    Solid
};

struct NamedSelectionItem {
    int geometryObjectId{-1};
    NamedSelectionEntityType entityType{NamedSelectionEntityType::Object};
    int localIndex{-1};

    bool operator==(const NamedSelectionItem&) const = default;
};

struct NamedSelection {
    NamedSelectionId id{-1};
    std::string name;
    NamedSelectionEntityType entityType{NamedSelectionEntityType::Object};
    std::vector<NamedSelectionItem> items;
};

enum class NamedSelectionError {
    None,
    NotFound,
    EmptyName,
    DuplicateName,
    EmptyItems,
    MixedEntityTypes,
    InvalidGeometryObject,
    InvalidLocalIndex,
    WouldBecomeEmpty,
    InUse
};

struct NamedSelectionOperationResult {
    bool success{false};
    NamedSelectionId namedSelectionId{-1};
    NamedSelectionError error{NamedSelectionError::None};
    std::size_t changedItemCount{0};
};

} // namespace emilcae::core
