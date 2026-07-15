#pragma once

#include <TopoDS_Shape.hxx>

enum class SelectionMode {
    Object,
    Vertex,
    Edge,
    Face,
    Solid
};

struct GeometrySelection {
    int objectId{-1};
    SelectionMode mode{SelectionMode::Object};
    TopoDS_Shape shape;
    int localIndex{-1};
};
