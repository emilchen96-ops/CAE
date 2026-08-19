#pragma once

#include "emilcae/core/NamedSelection.hpp"

#include <functional>
#include <vector>

namespace emilcae::core {

class NamedSelectionManager {
public:
    using GeometryObjectExists = std::function<bool(int)>;
    using ReferenceChecker = std::function<bool(NamedSelectionId)>;

    explicit NamedSelectionManager(
        GeometryObjectExists geometryObjectExists,
        ReferenceChecker referenceChecker = {});

    NamedSelectionOperationResult create(
        const std::string& name, NamedSelectionEntityType entityType,
        const std::vector<NamedSelectionItem>& items);
    NamedSelectionOperationResult update(
        NamedSelectionId id, const std::string& name,
        NamedSelectionEntityType entityType,
        const std::vector<NamedSelectionItem>& items);
    NamedSelectionOperationResult rename(NamedSelectionId id,
                                         const std::string& name);
    NamedSelectionOperationResult remove(NamedSelectionId id);
    NamedSelectionOperationResult addItems(
        NamedSelectionId id,
        const std::vector<NamedSelectionItem>& items);
    NamedSelectionOperationResult removeItems(
        NamedSelectionId id,
        const std::vector<NamedSelectionItem>& items);
    NamedSelectionOperationResult replaceItems(
        NamedSelectionId id,
        const std::vector<NamedSelectionItem>& items);

    const NamedSelection* find(NamedSelectionId id) const;
    std::vector<NamedSelection> namedSelections() const;
    std::vector<NamedSelectionId> selectionsReferencingGeometry(
        int geometryObjectId) const;
    std::size_t invalidGeometryReferenceCount(
        NamedSelectionId id) const;

private:
    NamedSelectionError validateName(
        const std::string& name,
        NamedSelectionId excludedId = -1) const;
    NamedSelectionError normalizeItems(
        NamedSelectionEntityType entityType,
        const std::vector<NamedSelectionItem>& items,
        std::vector<NamedSelectionItem>& normalized) const;
    NamedSelection* findMutable(NamedSelectionId id);

    GeometryObjectExists geometryObjectExists_;
    ReferenceChecker referenceChecker_;
    std::vector<NamedSelection> namedSelections_;
    NamedSelectionId nextId_{1};
};

} // namespace emilcae::core
