#pragma once

#include <QString>
#include <TopoDS_Shape.hxx>

class GeometryImporter {
public:
    struct ImportResult {
        bool success{false};
        TopoDS_Shape shape;
        QString errorMessage;
    };

    ImportResult importFile(const QString& filePath) const;
};
