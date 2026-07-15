#include "GeometryObjectManager.hpp"

#include <AIS_DisplayMode.hxx>
#include <Graphic3d_AspectFillArea3d.hxx>
#include <Poly_Triangle.hxx>
#include <Poly_Triangulation.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <Quantity_Color.hxx>
#include <Standard_Failure.hxx>
#include <gp_Pnt.hxx>

#include <QDebug>

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <utility>

namespace {

Handle(Poly_Triangulation) createTriangulation(const MeshData& data) {
    if (data.nodes.empty() || data.surfaceTriangles.empty() ||
        data.nodes.size() >
            static_cast<std::size_t>(std::numeric_limits<Standard_Integer>::max()) ||
        data.surfaceTriangles.size() >
            static_cast<std::size_t>(std::numeric_limits<Standard_Integer>::max())) {
        return {};
    }

    Handle(Poly_Triangulation) triangulation = new Poly_Triangulation(
        static_cast<Standard_Integer>(data.nodes.size()),
        static_cast<Standard_Integer>(data.surfaceTriangles.size()),
        Standard_False);
    std::unordered_map<std::size_t, Standard_Integer> nodeIndices;
    nodeIndices.reserve(data.nodes.size());
    for (std::size_t index = 0; index < data.nodes.size(); ++index) {
        const MeshNode& node = data.nodes[index];
        const Standard_Integer occIndex =
            static_cast<Standard_Integer>(index + 1);
        triangulation->SetNode(occIndex, gp_Pnt(node.x, node.y, node.z));
        nodeIndices.emplace(node.id, occIndex);
    }

    for (std::size_t index = 0; index < data.surfaceTriangles.size(); ++index) {
        const MeshElement& triangle = data.surfaceTriangles[index];
        if (triangle.nodeIds.size() != 3) {
            return {};
        }
        const auto first = nodeIndices.find(triangle.nodeIds[0]);
        const auto second = nodeIndices.find(triangle.nodeIds[1]);
        const auto third = nodeIndices.find(triangle.nodeIds[2]);
        if (first == nodeIndices.end() || second == nodeIndices.end() ||
            third == nodeIndices.end()) {
            return {};
        }
        triangulation->SetTriangle(
            static_cast<Standard_Integer>(index + 1),
            Poly_Triangle(first->second, second->second, third->second));
    }
    return triangulation;
}

Handle(AIS_Triangulation) createMeshPresentation(const MeshData& data) {
    const Handle(Poly_Triangulation) triangulation =
        createTriangulation(data);
    if (triangulation.IsNull()) {
        return {};
    }
    Handle(AIS_Triangulation) presentation =
        new AIS_Triangulation(triangulation);
    presentation->Attributes()->SetupOwnShadingAspect();
    const Handle(Graphic3d_AspectFillArea3d) fillAspect =
        presentation->Attributes()->ShadingAspect()->Aspect();
    fillAspect->SetInteriorColor(
        Quantity_Color(0.95, 0.58, 0.16, Quantity_TOC_RGB));
    fillAspect->SetEdgeColor(
        Quantity_Color(0.12, 0.14, 0.18, Quantity_TOC_RGB));
    fillAspect->SetEdgeOn();
    return presentation;
}

} // namespace

GeometryObjectManager::GeometryObjectManager(
    const Handle(AIS_InteractiveContext)& context,
    const Handle(V3d_View)& view)
    : context_(context), view_(view) {}

GeometryObjectManager::~GeometryObjectManager() {
    clearAll();
}

int GeometryObjectManager::addObject(const QString& name,
                                     const QString& filePath,
                                     const TopoDS_Shape& shape) {
    if (shape.IsNull() || context_.IsNull() || view_.IsNull()) {
        return -1;
    }

    Handle(AIS_Shape) presentation = new AIS_Shape(shape);
    try {
        context_->SetDisplayMode(presentation, AIS_Shaded, Standard_False);
        context_->SetColor(presentation, Quantity_NOC_STEELBLUE,
                           Standard_False);
        context_->Display(presentation, Standard_False);
        context_->Activate(presentation, selectionMode_);

        const int objectId = nextObjectId_++;
        objects_.push_back(
            {objectId, name, filePath, shape, presentation, true});
        redrawView();
        return objectId;
    } catch (const Standard_Failure& failure) {
        if (!presentation.IsNull()) {
            context_->Remove(presentation, Standard_False);
        }
        qWarning() << "OpenCASCADE object display failed:"
                   << failure.GetMessageString();
        redrawView();
        return -1;
    }
}

bool GeometryObjectManager::setVisible(int objectId, bool visible) {
    GeometryObject* object = findMutableObject(objectId);
    if (object == nullptr || object->presentation.IsNull() ||
        context_.IsNull()) {
        return false;
    }
    if (object->visible == visible) {
        return true;
    }

    try {
        if (visible) {
            context_->Display(object->presentation, Standard_False);
            context_->Activate(object->presentation, selectionMode_);
            if (findMeshForGeometry(objectId) != nullptr) {
                context_->SetTransparency(object->presentation, 0.65,
                                          Standard_False);
            }
        } else {
            context_->Deactivate(object->presentation);
            context_->Erase(object->presentation, Standard_False);
        }
        object->visible = visible;
        redrawView();
        return true;
    } catch (const Standard_Failure& failure) {
        qWarning() << "OpenCASCADE visibility update failed:"
                   << failure.GetMessageString();
        return false;
    }
}

bool GeometryObjectManager::removeObject(int objectId) {
    const auto iterator = std::find_if(
        objects_.begin(), objects_.end(),
        [objectId](const GeometryObject& object) {
            return object.id == objectId;
        });
    if (iterator == objects_.end() || context_.IsNull()) {
        return false;
    }

    try {
        if (!removeMeshForGeometry(objectId)) {
            return false;
        }
        if (!iterator->presentation.IsNull()) {
            context_->Remove(iterator->presentation, Standard_False);
        }
        objects_.erase(iterator);
        redrawView();
        return true;
    } catch (const Standard_Failure& failure) {
        qWarning() << "OpenCASCADE object removal failed:"
                   << failure.GetMessageString();
        return false;
    }
}

bool GeometryObjectManager::renameObject(int objectId, const QString& name) {
    GeometryObject* object = findMutableObject(objectId);
    if (object == nullptr || name.trimmed().isEmpty()) {
        return false;
    }
    object->name = name.trimmed();
    return true;
}

int GeometryObjectManager::replaceMesh(int geometryObjectId,
                                       const QString& name,
                                       const MeshData& data,
                                       double targetSize) {
    GeometryObject* geometry = findMutableObject(geometryObjectId);
    if (geometry == nullptr || context_.IsNull() || view_.IsNull()) {
        return -1;
    }
    Handle(AIS_Triangulation) presentation = createMeshPresentation(data);
    if (presentation.IsNull()) {
        return -1;
    }
    MeshObject* existing = nullptr;
    for (MeshObject& mesh : meshes_) {
        if (mesh.geometryObjectId == geometryObjectId) {
            existing = &mesh;
            break;
        }
    }
    const int meshId = existing != nullptr ? existing->id : nextMeshId_;
    MeshObject replacement{meshId, geometryObjectId, name, {}, false, data,
                           targetSize, true, presentation};
    if (existing == nullptr) {
        meshes_.reserve(meshes_.size() + 1);
    }
    try {
        context_->Display(presentation, Standard_False);
        context_->Deactivate(presentation);
        context_->Deactivate(geometry->presentation);
        context_->Erase(geometry->presentation, Standard_False);
        geometry->visible = false;
        if (existing != nullptr) {
            if (!existing->presentation.IsNull()) {
                context_->Remove(existing->presentation, Standard_False);
            }
            *existing = std::move(replacement);
            redrawView();
            return meshId;
        }

        meshes_.push_back(std::move(replacement));
        ++nextMeshId_;
        redrawView();
        return meshId;
    } catch (const Standard_Failure& failure) {
        if (!presentation.IsNull()) {
            context_->Remove(presentation, Standard_False);
        }
        qWarning() << "OpenCASCADE mesh display failed:"
                   << failure.GetMessageString();
        redrawView();
        return -1;
    }
}

int GeometryObjectManager::addStandaloneMesh(
    const QString& name, const QString& sourceFilePath,
    const MeshData& data) {
    if (context_.IsNull() || view_.IsNull()) {
        return -1;
    }
    Handle(AIS_Triangulation) presentation = createMeshPresentation(data);
    if (presentation.IsNull()) {
        return -1;
    }
    try {
        context_->Display(presentation, Standard_False);
        context_->Deactivate(presentation);
        const int meshId = nextMeshId_++;
        meshes_.push_back({meshId, -1, name, sourceFilePath, true, data,
                           0.0, true, presentation});
        redrawView();
        return meshId;
    } catch (const Standard_Failure& failure) {
        context_->Remove(presentation, Standard_False);
        qWarning() << "OpenCASCADE standalone mesh display failed:"
                   << failure.GetMessageString();
        redrawView();
        return -1;
    }
}

bool GeometryObjectManager::setMeshVisible(int meshId, bool visible) {
    MeshObject* mesh = findMutableMesh(meshId);
    if (mesh == nullptr || mesh->presentation.IsNull() || context_.IsNull()) {
        return false;
    }
    if (mesh->visible == visible) {
        return true;
    }
    try {
        if (visible) {
            context_->Display(mesh->presentation, Standard_False);
            context_->Deactivate(mesh->presentation);
        } else {
            context_->Erase(mesh->presentation, Standard_False);
        }
        mesh->visible = visible;
        redrawView();
        return true;
    } catch (const Standard_Failure& failure) {
        qWarning() << "OpenCASCADE mesh visibility update failed:"
                   << failure.GetMessageString();
        return false;
    }
}

bool GeometryObjectManager::removeMesh(int meshId) {
    const auto iterator = std::find_if(
        meshes_.begin(), meshes_.end(),
        [meshId](const MeshObject& mesh) { return mesh.id == meshId; });
    if (iterator == meshes_.end() || context_.IsNull()) {
        return false;
    }
    const int geometryObjectId = iterator->geometryObjectId;
    try {
        if (!iterator->presentation.IsNull()) {
            context_->Remove(iterator->presentation, Standard_False);
        }
        meshes_.erase(iterator);
        if (geometryObjectId >= 0) {
            restoreGeometryAppearance(geometryObjectId);
        }
        redrawView();
        return true;
    } catch (const Standard_Failure& failure) {
        qWarning() << "OpenCASCADE mesh removal failed:"
                   << failure.GetMessageString();
        return false;
    }
}

bool GeometryObjectManager::removeMeshForGeometry(int geometryObjectId) {
    const MeshObject* mesh = findMeshForGeometry(geometryObjectId);
    return mesh == nullptr || removeMesh(mesh->id);
}

bool GeometryObjectManager::renameMesh(int meshId, const QString& name) {
    MeshObject* mesh = findMutableMesh(meshId);
    if (mesh == nullptr || name.trimmed().isEmpty()) {
        return false;
    }
    mesh->name = name.trimmed();
    return true;
}

bool GeometryObjectManager::setSelectionMode(
    Standard_Integer selectionMode) {
    if (context_.IsNull()) {
        return false;
    }
    try {
        for (const GeometryObject& object : objects_) {
            if (object.presentation.IsNull()) {
                continue;
            }
            context_->Deactivate(object.presentation);
            if (object.visible) {
                context_->Activate(object.presentation, selectionMode);
            }
        }
        selectionMode_ = selectionMode;
        return true;
    } catch (const Standard_Failure& failure) {
        qWarning() << "OpenCASCADE selection mode update failed:"
                   << failure.GetMessageString();
        return false;
    }
}

void GeometryObjectManager::clearAll() {
    clearAllMeshes();
    if (!context_.IsNull()) {
        for (const GeometryObject& object : objects_) {
            if (!object.presentation.IsNull()) {
                try {
                    context_->Remove(object.presentation, Standard_False);
                } catch (const Standard_Failure& failure) {
                    qWarning() << "OpenCASCADE object cleanup failed:"
                               << failure.GetMessageString();
                } catch (...) {
                    qWarning() << "Unknown OpenCASCADE object cleanup failure";
                }
            }
        }
    }
    objects_.clear();
    redrawView();
}

void GeometryObjectManager::clearGeometryObjects() {
    std::vector<int> objectIds;
    objectIds.reserve(objects_.size());
    for (const GeometryObject& object : objects_) {
        objectIds.push_back(object.id);
    }
    for (const int objectId : objectIds) {
        if (!removeObject(objectId)) {
            qWarning() << "OpenCASCADE geometry collection cleanup failed:"
                       << objectId;
        }
    }
    redrawView();
}

void GeometryObjectManager::clearAllMeshes() {
    std::vector<int> geometryObjectIds;
    geometryObjectIds.reserve(meshes_.size());
    for (const MeshObject& mesh : meshes_) {
        geometryObjectIds.push_back(mesh.geometryObjectId);
    }
    if (!context_.IsNull()) {
        for (const MeshObject& mesh : meshes_) {
            if (!mesh.presentation.IsNull()) {
                try {
                    context_->Remove(mesh.presentation, Standard_False);
                } catch (const Standard_Failure& failure) {
                    qWarning() << "OpenCASCADE mesh cleanup failed:"
                               << failure.GetMessageString();
                } catch (...) {
                    qWarning() << "Unknown OpenCASCADE mesh cleanup failure";
                }
            }
        }
    }
    meshes_.clear();
    for (const int geometryObjectId : geometryObjectIds) {
        restoreGeometryAppearance(geometryObjectId);
    }
    redrawView();
}

const GeometryObject*
GeometryObjectManager::findObject(int objectId) const {
    const auto iterator = std::find_if(
        objects_.cbegin(), objects_.cend(),
        [objectId](const GeometryObject& object) {
            return object.id == objectId;
        });
    return iterator != objects_.cend() ? &*iterator : nullptr;
}

const GeometryObject* GeometryObjectManager::findObjectByPresentation(
    const Handle(AIS_InteractiveObject)& presentation) const {
    if (presentation.IsNull()) {
        return nullptr;
    }
    const auto iterator = std::find_if(
        objects_.cbegin(), objects_.cend(),
        [&presentation](const GeometryObject& object) {
            return !object.presentation.IsNull() &&
                   object.presentation.get() == presentation.get();
        });
    return iterator != objects_.cend() ? &*iterator : nullptr;
}

const MeshObject* GeometryObjectManager::findMesh(int meshId) const {
    const auto iterator = std::find_if(
        meshes_.cbegin(), meshes_.cend(),
        [meshId](const MeshObject& mesh) { return mesh.id == meshId; });
    return iterator != meshes_.cend() ? &*iterator : nullptr;
}

const MeshObject* GeometryObjectManager::findMeshForGeometry(
    int geometryObjectId) const {
    if (geometryObjectId < 0) {
        return nullptr;
    }
    const auto iterator = std::find_if(
        meshes_.cbegin(), meshes_.cend(),
        [geometryObjectId](const MeshObject& mesh) {
            return mesh.geometryObjectId == geometryObjectId;
        });
    return iterator != meshes_.cend() ? &*iterator : nullptr;
}

std::size_t GeometryObjectManager::objectCount() const {
    return objects_.size();
}

std::size_t GeometryObjectManager::meshCount() const {
    return meshes_.size();
}

GeometryObject* GeometryObjectManager::findMutableObject(int objectId) {
    const auto iterator = std::find_if(
        objects_.begin(), objects_.end(),
        [objectId](const GeometryObject& object) {
            return object.id == objectId;
        });
    return iterator != objects_.end() ? &*iterator : nullptr;
}

MeshObject* GeometryObjectManager::findMutableMesh(int meshId) {
    const auto iterator = std::find_if(
        meshes_.begin(), meshes_.end(),
        [meshId](const MeshObject& mesh) { return mesh.id == meshId; });
    return iterator != meshes_.end() ? &*iterator : nullptr;
}

void GeometryObjectManager::restoreGeometryAppearance(int geometryObjectId) {
    GeometryObject* geometry = findMutableObject(geometryObjectId);
    if (geometry != nullptr && !geometry->presentation.IsNull() &&
        !context_.IsNull()) {
        try {
            context_->UnsetTransparency(geometry->presentation,
                                        Standard_False);
            context_->Display(geometry->presentation, Standard_False);
            context_->Activate(geometry->presentation, selectionMode_);
            geometry->visible = true;
        } catch (const Standard_Failure& failure) {
            qWarning() << "OpenCASCADE geometry appearance restore failed:"
                       << failure.GetMessageString();
        } catch (...) {
            qWarning() << "Unknown geometry appearance restore failure";
        }
    }
}

void GeometryObjectManager::redrawView() {
    if (!view_.IsNull()) {
        view_->Redraw();
    }
}
