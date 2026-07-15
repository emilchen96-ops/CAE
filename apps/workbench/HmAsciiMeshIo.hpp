#pragma once

#include "MeshData.hpp"

#include <QString>
#include <QStringList>

struct HmAsciiIoResult {
    bool success{false};
    QString errorMessage;
    QStringList warnings;
};

struct HmAsciiImportResult : HmAsciiIoResult {
    MeshData mesh;
    QString componentName;
};

class HmAsciiMeshIo {
public:
    HmAsciiImportResult importFile(const QString& filePath) const;

    HmAsciiIoResult exportFile(const QString& filePath,
                               const MeshData& mesh,
                               const QString& componentName) const;
};
