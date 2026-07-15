#include "HmAsciiMeshIo.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include <cmath>
#include <iostream>
#include <vector>

namespace {

bool writeTextFile(const QString& filePath, const QString& content) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << content;
    stream.flush();
    return stream.status() == QTextStream::Ok;
}

bool coordinatesMatch(const MeshData& left, const MeshData& right) {
    if (left.nodes.size() != right.nodes.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.nodes.size(); ++index) {
        const MeshNode& a = left.nodes[index];
        const MeshNode& b = right.nodes[index];
        if (a.id != b.id || std::abs(a.x - b.x) > 1.0e-14 ||
            std::abs(a.y - b.y) > 1.0e-14 ||
            std::abs(a.z - b.z) > 1.0e-14) {
            return false;
        }
    }
    return true;
}

bool elementsMatch(const std::vector<MeshElement>& left,
                   const std::vector<MeshElement>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index].id != right[index].id ||
            left[index].nodeIds != right[index].nodeIds) {
            return false;
        }
    }
    return true;
}

bool containsWarning(const QStringList& warnings, const QString& text) {
    for (const QString& warning : warnings) {
        if (warning.contains(text)) {
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) {
        std::cerr << "Could not create temporary test directory.\n";
        return 1;
    }

    const HmAsciiMeshIo meshIo;
    int failures = 0;
    auto check = [&failures](bool condition, const char* name) {
        if (!condition) {
            ++failures;
            std::cerr << "FAILED: " << name << '\n';
        }
    };

    const QString noTrianglesPath = directory.filePath("no_triangles.hm");
    const QString validSubset = QStringLiteral(
        "*component(1,\"Component, A\",0,3,0)\n"
        "*material(10,\"ignored\")\n"
        "*node(10,0,0,0,0,0,0,0,0)\n"
        "*node(20,1,0,0,0,0,0,0,0)\n"
        "*node(30,0,1,0,0,0,0,0,0)\n"
        "*node(40,0,0,1,0,0,0,0,0)\n"
        "*node(50,0,0,-1,0,0,0,0,0)\n"
        "*tetra4(100,0,10,20,30,40,0)\n"
        "*tetra4(200,0,10,30,20,50,0)\n");
    check(writeTextFile(noTrianglesPath, validSubset), "write valid subset");
    const HmAsciiImportResult firstImport =
        meshIo.importFile(noTrianglesPath);
    check(firstImport.success, "import without tria3");
    check(firstImport.componentName == QStringLiteral("Component, A"),
          "quoted component with comma");
    check(firstImport.mesh.nodes.size() == 5, "node count");
    check(firstImport.mesh.tetrahedra.size() == 2, "tetrahedron count");
    check(firstImport.mesh.surfaceTriangles.size() == 6,
          "boundary extraction excludes shared face");
    check(containsWarning(firstImport.warnings, QStringLiteral("tria3")),
          "missing tria3 warning");
    check(containsWarning(firstImport.warnings, QStringLiteral("*material")),
          "unsupported command warning");

    const QString roundTripPath = directory.filePath("round_trip.hm");
    const HmAsciiIoResult exportResult = meshIo.exportFile(
        roundTripPath, firstImport.mesh, firstImport.componentName);
    check(exportResult.success, "export subset");
    const HmAsciiImportResult roundTrip = meshIo.importFile(roundTripPath);
    check(roundTrip.success, "reimport exported subset");
    check(coordinatesMatch(firstImport.mesh, roundTrip.mesh),
          "round-trip coordinates and node IDs");
    check(elementsMatch(firstImport.mesh.tetrahedra,
                        roundTrip.mesh.tetrahedra),
          "round-trip tetrahedron connectivity");
    check(elementsMatch(firstImport.mesh.surfaceTriangles,
                        roundTrip.mesh.surfaceTriangles),
          "round-trip surface triangle connectivity");

    for (int iteration = 0; iteration < 3; ++iteration) {
        const QString repeatedPath = directory.filePath(
            QStringLiteral("repeated_%1.hm").arg(iteration));
        const HmAsciiIoResult repeatedExport = meshIo.exportFile(
            repeatedPath, roundTrip.mesh, roundTrip.componentName);
        const HmAsciiImportResult repeatedImport =
            meshIo.importFile(repeatedPath);
        check(repeatedExport.success && repeatedImport.success,
              "repeated import/export");
    }

    const QString duplicateNodePath = directory.filePath("duplicate.hm");
    check(writeTextFile(duplicateNodePath, QStringLiteral(
        "*node(1,0,0,0,0,0,0,0,0)\n"
        "*node(1,1,0,0,0,0,0,0,0)\n"
        "*tetra4(1,0,1,2,3,4,0)\n")), "write duplicate input");
    check(!meshIo.importFile(duplicateNodePath).success,
          "reject duplicate node ID");

    const QString missingNodePath = directory.filePath("missing_node.hm");
    check(writeTextFile(missingNodePath, QStringLiteral(
        "*node(1,0,0,0,0,0,0,0,0)\n"
        "*node(2,1,0,0,0,0,0,0,0)\n"
        "*node(3,0,1,0,0,0,0,0,0)\n"
        "*tetra4(5,0,1,2,3,99,0)\n")), "write missing-node input");
    const HmAsciiImportResult missingNode =
        meshIo.importFile(missingNodePath);
    check(!missingNode.success &&
              missingNode.errorMessage.contains(QStringLiteral("99")),
          "reject missing node reference");

    const QString coordinateSystemPath =
        directory.filePath("coordinate_system.hm");
    check(writeTextFile(coordinateSystemPath, QStringLiteral(
        "*node(1,0,0,0,0,7,0,0,0)\n"
        "*tetra4(1,0,1,2,3,4,0)\n")), "write coordinate-system input");
    const HmAsciiImportResult coordinateSystem =
        meshIo.importFile(coordinateSystemPath);
    check(!coordinateSystem.success &&
              coordinateSystem.errorMessage.contains(
                  QStringLiteral("非全局坐标系")),
          "reject non-global coordinate system");

    const QString malformedPath = directory.filePath("malformed.hm");
    check(writeTextFile(malformedPath, QStringLiteral(
        "*node(1,0,0,0,0,0,0,0,0\n")), "write malformed input");
    const HmAsciiImportResult malformed = meshIo.importFile(malformedPath);
    check(!malformed.success &&
              malformed.errorMessage.contains(QStringLiteral("第 1 行")),
          "malformed command includes line number");

    const QString invalidElementFieldPath =
        directory.filePath("invalid_element_field.hm");
    check(writeTextFile(invalidElementFieldPath, QStringLiteral(
        "*node(1,0,0,0,0,0,0,0,0)\n"
        "*node(2,1,0,0,0,0,0,0,0)\n"
        "*node(3,0,1,0,0,0,0,0,0)\n"
        "*node(4,0,0,1,0,0,0,0,0)\n"
        "*tetra4(1,invalid,1,2,3,4,0)\n")),
          "write invalid element field input");
    const HmAsciiImportResult invalidElementField =
        meshIo.importFile(invalidElementFieldPath);
    check(!invalidElementField.success &&
              invalidElementField.errorMessage.contains(
                  QStringLiteral("单元类型或属性编号")),
          "reject non-integer element type or property");

    MeshData renumberedMesh = firstImport.mesh;
    renumberedMesh.nodes.front().id = 0;
    for (MeshElement& element : renumberedMesh.tetrahedra) {
        for (std::size_t& nodeId : element.nodeIds) {
            if (nodeId == 10) {
                nodeId = 0;
            }
        }
    }
    for (MeshElement& element : renumberedMesh.surfaceTriangles) {
        for (std::size_t& nodeId : element.nodeIds) {
            if (nodeId == 10) {
                nodeId = 0;
            }
        }
    }
    renumberedMesh.tetrahedra.front().id = 0;
    const QString renumberedPath = directory.filePath("renumbered.hm");
    const HmAsciiIoResult renumberedExport = meshIo.exportFile(
        renumberedPath, renumberedMesh, QStringLiteral("Unsafe\"Name\n"));
    const HmAsciiImportResult renumberedImport =
        meshIo.importFile(renumberedPath);
    check(renumberedExport.success && renumberedImport.success,
          "renumber invalid IDs");
    check(containsWarning(renumberedExport.warnings,
                          QStringLiteral("重新编号")),
          "renumber warning");
    check(renumberedImport.mesh.nodes.front().id == 1,
          "node renumber starts at one");
    check(renumberedImport.mesh.tetrahedra.front().nodeIds.front() == 1,
          "renumbered connectivity follows node mapping");

    const QString impossiblePath =
        directory.filePath("missing_directory/output.hm");
    const HmAsciiIoResult impossibleExport = meshIo.exportFile(
        impossiblePath, firstImport.mesh, firstImport.componentName);
    check(!impossibleExport.success && !QFile::exists(impossiblePath),
          "failed export leaves no incomplete file");

    if (failures == 0) {
        std::cout << "HMASCII subset tests passed: round-trip nodes="
                  << roundTrip.mesh.nodes.size() << ", tetrahedra="
                  << roundTrip.mesh.tetrahedra.size() << ", triangles="
                  << roundTrip.mesh.surfaceTriangles.size() << '\n';
    }
    return failures == 0 ? 0 : 1;
}
