#pragma once

#include "MeshData.hpp"

#include <QString>
#include <QWidget>

#include <vtkSmartPointer.h>

class QVTKOpenGLNativeWidget;
class QShowEvent;
class vtkActor;
class vtkAxesActor;
class vtkDataSetMapper;
class vtkGenericOpenGLRenderWindow;
class vtkInteractorStyleTrackballCamera;
class vtkOrientationMarkerWidget;
class vtkRenderer;
class vtkUnstructuredGrid;

enum class VtkMeshDisplayMode {
    SurfaceWithEdges,
    SurfaceOnly,
    Wireframe
};

class VtkPostViewWidget final : public QWidget {
    Q_OBJECT

public:
    explicit VtkPostViewWidget(QWidget* parent = nullptr);
    ~VtkPostViewWidget() override;

    bool displayMesh(const MeshData& mesh);
    void clearScene();
    void fitAll();

    void setSurfaceWithEdges();
    void setSurfaceOnly();
    void setWireframe();

    bool hasMesh() const;
    QString lastError() const;
    VtkMeshDisplayMode displayMode() const;

protected:
    void showEvent(QShowEvent* event) override;

private:
    void applyDisplayMode(VtkMeshDisplayMode mode);
    void render();

    QVTKOpenGLNativeWidget* vtkWidget_{nullptr};
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> renderWindow_;
    vtkSmartPointer<vtkRenderer> renderer_;
    vtkSmartPointer<vtkDataSetMapper> mapper_;
    vtkSmartPointer<vtkActor> actor_;
    vtkSmartPointer<vtkInteractorStyleTrackballCamera> interactionStyle_;
    vtkSmartPointer<vtkAxesActor> axesActor_;
    vtkSmartPointer<vtkOrientationMarkerWidget> orientationMarker_;
    vtkSmartPointer<vtkUnstructuredGrid> currentGrid_;
    QString lastError_;
    VtkMeshDisplayMode displayMode_{
        VtkMeshDisplayMode::SurfaceWithEdges};
};
