#pragma once

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <AIS_Triangulation.hxx>
#include <TopoDS_Shape.hxx>
#include <V3d_View.hxx>

#include <QString>

#include <cstddef>
#include <vector>

#include "MeshData.hpp"

struct GeometryObject {
    int id{-1};
    QString name;
    QString filePath;
    TopoDS_Shape shape;
    Handle(AIS_Shape) presentation;
    bool visible{true};
};

struct MeshObject {
    int id{-1};
    int geometryObjectId{-1};
    QString name;
    QString sourceFilePath;
    bool importedHmAscii{false};
    MeshData data;
    double targetSize{0.0};
    bool visible{true};
    Handle(AIS_Triangulation) presentation;
};

class GeometryObjectManager {
public:
    GeometryObjectManager(
        const Handle(AIS_InteractiveContext)& context,
        const Handle(V3d_View)& view);
    ~GeometryObjectManager();

    int addObject(const QString& name,
                  const QString& filePath,
                  const TopoDS_Shape& shape);
    bool setVisible(int objectId, bool visible);
    bool removeObject(int objectId);
    bool renameObject(int objectId, const QString& name);
    int replaceMesh(int geometryObjectId, const QString& name,
                    const MeshData& data, double targetSize);
    int addStandaloneMesh(const QString& name,
                          const QString& sourceFilePath,
                          const MeshData& data);
    bool setMeshVisible(int meshId, bool visible);
    bool removeMesh(int meshId);
    bool removeMeshForGeometry(int geometryObjectId);
    bool renameMesh(int meshId, const QString& name);
    bool setSelectionMode(Standard_Integer selectionMode);
    void clearGeometryObjects();
    void clearAll();
    void clearAllMeshes();

    const GeometryObject* findObject(int objectId) const;
    const GeometryObject* findObjectByPresentation(
        const Handle(AIS_InteractiveObject)& presentation) const;
    const MeshObject* findMesh(int meshId) const;
    const MeshObject* findMeshForGeometry(int geometryObjectId) const;
    const std::vector<MeshObject>& meshes() const;
    std::size_t objectCount() const;
    std::size_t meshCount() const;

private:
    GeometryObject* findMutableObject(int objectId);
    MeshObject* findMutableMesh(int meshId);
    void restoreGeometryAppearance(int geometryObjectId);
    void redrawView();

    Handle(AIS_InteractiveContext) context_;
    Handle(V3d_View) view_;
    std::vector<GeometryObject> objects_;
    std::vector<MeshObject> meshes_;
    int nextObjectId_{1};
    int nextMeshId_{1};
    Standard_Integer selectionMode_{0};
};
