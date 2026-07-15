#pragma once

#include "emilcae/core/NamedSelection.hpp"

#include <TopoDS_Shape.hxx>

#include <functional>
#include <optional>
#include <vector>

struct ResolvedNamedSelectionItem {
    emilcae::core::NamedSelectionItem reference;
    TopoDS_Shape shape;
    bool sourceVisible{false};
};

struct NamedSelectionResolveResult {
    std::vector<ResolvedNamedSelectionItem> validItems;
    std::vector<emilcae::core::NamedSelectionItem> invalidItems;
};

struct NamedSelectionGeometry {
    TopoDS_Shape shape;
    bool visible{false};
};

class NamedSelectionResolver {
public:
    using GeometryLookup = std::function<std::optional<NamedSelectionGeometry>(
        int geometryObjectId)>;

    explicit NamedSelectionResolver(GeometryLookup geometryLookup);

    NamedSelectionResolveResult resolve(
        const emilcae::core::NamedSelection& selection) const;

private:
    GeometryLookup geometryLookup_;
};
