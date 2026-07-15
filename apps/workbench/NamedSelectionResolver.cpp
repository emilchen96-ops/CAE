#include "NamedSelectionResolver.hpp"

#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>

namespace {

TopAbs_ShapeEnum shapeType(
    emilcae::core::NamedSelectionEntityType entityType) {
    using emilcae::core::NamedSelectionEntityType;
    switch (entityType) {
    case NamedSelectionEntityType::Object:
        return TopAbs_SHAPE;
    case NamedSelectionEntityType::Vertex:
        return TopAbs_VERTEX;
    case NamedSelectionEntityType::Edge:
        return TopAbs_EDGE;
    case NamedSelectionEntityType::Face:
        return TopAbs_FACE;
    case NamedSelectionEntityType::Solid:
        return TopAbs_SOLID;
    }
    return TopAbs_SHAPE;
}

} // namespace

NamedSelectionResolver::NamedSelectionResolver(
    GeometryLookup geometryLookup)
    : geometryLookup_(std::move(geometryLookup)) {}

NamedSelectionResolveResult NamedSelectionResolver::resolve(
    const emilcae::core::NamedSelection& selection) const {
    NamedSelectionResolveResult result;
    for (const emilcae::core::NamedSelectionItem& item : selection.items) {
        const std::optional<NamedSelectionGeometry> geometry = geometryLookup_
            ? geometryLookup_(item.geometryObjectId)
            : std::nullopt;
        if (!geometry || geometry->shape.IsNull() ||
            item.entityType != selection.entityType) {
            result.invalidItems.push_back(item);
            continue;
        }
        if (item.entityType ==
            emilcae::core::NamedSelectionEntityType::Object) {
            if (item.localIndex != 0) {
                result.invalidItems.push_back(item);
                continue;
            }
            result.validItems.push_back(
                {item, geometry->shape, geometry->visible});
            continue;
        }

        const TopAbs_ShapeEnum expectedType = shapeType(item.entityType);
        TopTools_IndexedMapOfShape shapes;
        TopExp::MapShapes(geometry->shape, expectedType, shapes);
        if (item.localIndex <= 0 || item.localIndex > shapes.Extent()) {
            result.invalidItems.push_back(item);
            continue;
        }
        const TopoDS_Shape resolvedShape = shapes.FindKey(item.localIndex);
        if (resolvedShape.IsNull() ||
            resolvedShape.ShapeType() != expectedType) {
            result.invalidItems.push_back(item);
            continue;
        }
        result.validItems.push_back(
            {item, resolvedShape, geometry->visible});
    }
    return result;
}
