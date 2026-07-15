#include "GmshMesher.hpp"

#include <BRepTools.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Shape.hxx>

#include <gmsh.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QDebug>
#include <QTemporaryFile>
#include <QUuid>

#include <cmath>
#include <exception>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

QString translated(const char* text) {
    return QCoreApplication::translate("GmshMesher", text);
}

class GmshSession final {
public:
    GmshSession() {
        if (gmsh::isInitialized() != 0) {
            throw std::runtime_error(
                "Gmsh is already initialized by another operation.");
        }
        gmsh::initialize(0, nullptr, false, false);
        initialized_ = true;
        gmsh::option::setNumber("General.Terminal", 0.0);
    }

    ~GmshSession() {
        if (initialized_ && gmsh::isInitialized() != 0) {
            try {
                gmsh::finalize();
            } catch (const std::exception& exception) {
                qWarning() << "Gmsh finalize failed:" << exception.what();
            } catch (...) {
                qWarning() << "Gmsh finalize failed with an unknown error";
            }
        }
    }

    GmshSession(const GmshSession&) = delete;
    GmshSession& operator=(const GmshSession&) = delete;

private:
    bool initialized_{false};
};

class GmshModel final {
public:
    explicit GmshModel(std::string name) : name_(std::move(name)) {
        gmsh::model::add(name_);
        added_ = true;
    }

    ~GmshModel() {
        if (!added_ || gmsh::isInitialized() == 0) {
            return;
        }
        try {
            gmsh::model::setCurrent(name_);
            gmsh::model::remove();
        } catch (const std::exception& exception) {
            qWarning() << "Gmsh model cleanup failed:" << exception.what();
        } catch (...) {
            qWarning() << "Gmsh model cleanup failed with an unknown error";
        }
    }

    GmshModel(const GmshModel&) = delete;
    GmshModel& operator=(const GmshModel&) = delete;

private:
    std::string name_;
    bool added_{false};
};

void appendElements(MeshData& mesh, int dimension,
                    bool tetrahedra) {
    std::vector<int> elementTypes;
    std::vector<std::vector<std::size_t>> elementTags;
    std::vector<std::vector<std::size_t>> elementNodeTags;
    gmsh::model::mesh::getElements(elementTypes, elementTags,
                                   elementNodeTags, dimension, -1);

    for (std::size_t typeIndex = 0;
         typeIndex < elementTypes.size(); ++typeIndex) {
        std::string elementName;
        int elementDimension = -1;
        int order = 0;
        int nodeCount = 0;
        int primaryNodeCount = 0;
        std::vector<double> localCoordinates;
        gmsh::model::mesh::getElementProperties(
            elementTypes[typeIndex], elementName, elementDimension,
            order, nodeCount, localCoordinates, primaryNodeCount);

        const bool expectedType = tetrahedra
            ? elementDimension == 3 && order == 1 && nodeCount == 4 &&
                  elementName.find("Tetrahedron") != std::string::npos
            : elementDimension == 2 && order == 1 && nodeCount == 3 &&
                  elementName.find("Triangle") != std::string::npos;
        if (!expectedType) {
            continue;
        }

        const auto& tags = elementTags.at(typeIndex);
        const auto& nodes = elementNodeTags.at(typeIndex);
        if (tags.empty() || nodes.size() != tags.size() *
                                      static_cast<std::size_t>(nodeCount)) {
            continue;
        }

        std::vector<MeshElement>& destination = tetrahedra
            ? mesh.tetrahedra
            : mesh.surfaceTriangles;
        destination.reserve(destination.size() + tags.size());
        for (std::size_t elementIndex = 0;
             elementIndex < tags.size(); ++elementIndex) {
            const auto first = nodes.begin() +
                static_cast<std::ptrdiff_t>(elementIndex * nodeCount);
            destination.push_back({
                tags[elementIndex], elementTypes[typeIndex],
                std::vector<std::size_t>(
                    first, first + static_cast<std::ptrdiff_t>(nodeCount))
            });
        }
    }
}

MeshData extractMesh() {
    MeshData mesh;
    std::vector<std::size_t> nodeTags;
    std::vector<double> coordinates;
    std::vector<double> parametricCoordinates;
    gmsh::model::mesh::getNodes(nodeTags, coordinates,
                                parametricCoordinates, -1, -1,
                                false, false);
    if (nodeTags.empty() || coordinates.size() != nodeTags.size() * 3) {
        throw std::runtime_error("Gmsh returned invalid node data.");
    }

    mesh.nodes.reserve(nodeTags.size());
    for (std::size_t index = 0; index < nodeTags.size(); ++index) {
        mesh.nodes.push_back({nodeTags[index], coordinates[index * 3],
                              coordinates[index * 3 + 1],
                              coordinates[index * 3 + 2]});
    }

    appendElements(mesh, 3, true);
    appendElements(mesh, 2, false);
    if (mesh.tetrahedra.empty()) {
        throw std::runtime_error("Gmsh did not generate tetrahedra.");
    }
    if (mesh.surfaceTriangles.empty()) {
        throw std::runtime_error(
            "Gmsh did not generate surface triangles.");
    }
    return mesh;
}

QString exceptionMessage(const std::exception& exception) {
    return QString::fromUtf8(exception.what());
}

} // namespace

MeshResult GmshMesher::generateTetrahedralMesh(
    const TopoDS_Shape& shape, double targetSize) const {
    if (shape.IsNull()) {
        return {false, {}, translated("几何体为空，无法生成网格。")};
    }
    if (!std::isfinite(targetSize) || targetSize <= 0.0) {
        return {false, {}, translated("目标网格尺寸必须大于零。")};
    }
    TopExp_Explorer solidExplorer(shape, TopAbs_SOLID);
    if (!solidExplorer.More()) {
        return {false, {},
                translated("当前几何体不包含可网格化的三维实体。")};
    }

    QTemporaryFile temporaryFile(
        QDir::temp().filePath(QStringLiteral("qtcae_mesh_XXXXXX.brep")));
    temporaryFile.setAutoRemove(true);
    if (!temporaryFile.open()) {
        return {false, {}, translated("无法创建临时 BREP 文件。")};
    }
    const QString temporaryPath = temporaryFile.fileName();
    temporaryFile.close();
    const QByteArray encodedPath =
        QFile::encodeName(QDir::toNativeSeparators(temporaryPath));

    try {
        if (!BRepTools::Write(shape, encodedPath.constData())) {
            return {false, {}, translated("临时 BREP 文件写入失败。")};
        }

        GmshSession session;
        const std::string modelName =
            QUuid::createUuid().toString(QUuid::WithoutBraces)
                .toStdString();
        GmshModel model(modelName);

        gmsh::vectorpair importedEntities;
        gmsh::model::occ::importShapes(
            QDir::fromNativeSeparators(temporaryPath).toStdString(),
            importedEntities, true, "brep");
        if (importedEntities.empty()) {
            return {false, {}, translated("Gmsh 未能导入几何体。")};
        }
        bool containsVolume = false;
        for (const auto& entity : importedEntities) {
            containsVolume = containsVolume || entity.first == 3;
        }
        if (!containsVolume) {
            return {false, {},
                    translated("导入到 Gmsh 的几何体不包含三维实体。")};
        }

        gmsh::model::occ::synchronize();
        gmsh::vectorpair points;
        gmsh::model::getEntities(points, 0);
        if (!points.empty()) {
            gmsh::model::mesh::setSize(points, targetSize);
        }
        gmsh::option::setNumber("Mesh.CharacteristicLengthMin",
                                targetSize);
        gmsh::option::setNumber("Mesh.CharacteristicLengthMax",
                                targetSize);
        gmsh::option::setNumber("Mesh.ElementOrder", 1.0);
        gmsh::option::setNumber("Mesh.RecombineAll", 0.0);
        gmsh::model::mesh::generate(3);

        MeshData mesh = extractMesh();
        return {true, std::move(mesh), {}};
    } catch (const Standard_Failure& failure) {
        const char* message = failure.GetMessageString();
        return {false, {}, translated("OpenCASCADE 网格准备异常：") +
            (message != nullptr ? QString::fromLocal8Bit(message)
                                : translated("未知错误"))};
    } catch (const std::exception& exception) {
        return {false, {}, translated("Gmsh 网格生成异常：") +
            exceptionMessage(exception)};
    } catch (...) {
        return {false, {}, translated("网格生成发生未知异常。")};
    }
}
