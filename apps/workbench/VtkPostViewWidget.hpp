#pragma once

#include "MeshData.hpp"
#include "ResultField.hpp"

#include <QString>
#include <QWidget>

#include <vtkSmartPointer.h>

#include <string>

class QVTKOpenGLNativeWidget;
class QLabel;
class QResizeEvent;
class QShowEvent;
class vtkActor;
class vtkAxesActor;
class vtkDataSetMapper;
class vtkGenericOpenGLRenderWindow;
class vtkInteractorStyleTrackballCamera;
class vtkLookupTable;
class vtkOrientationMarkerWidget;
class vtkRenderer;
class vtkScalarBarActor;
class vtkUnstructuredGrid;
class vtkWarpVector;

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
    bool displayResultGrid(vtkUnstructuredGrid* grid,
                           bool resetCamera = true);
    void clearScene();
    void fitAll();

    bool showScalarField(
        const std::string& arrayName,
        ResultFieldAssociation association,
        int component,
        const std::string& displayName);
    void clearScalarField();

    bool setDisplacementField(const std::string& arrayName);
    void clearDisplacementField();
    bool setDeformationEnabled(bool enabled);
    bool setDeformationScale(double scale);
    void setLegendVisible(bool visible);

    void setSurfaceWithEdges();
    void setSurfaceOnly();
    void setWireframe();

    bool hasMesh() const;
    bool scalarFieldVisible() const;
    bool deformationEnabled() const;
    bool legendVisible() const;
    double deformationScale() const;
    QString lastError() const;
    VtkMeshDisplayMode displayMode() const;

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void applyDisplayMode(VtkMeshDisplayMode mode);
    void updateInputPipeline();
    void render();

    QVTKOpenGLNativeWidget* vtkWidget_{nullptr};
    QLabel* unitLabel_{nullptr};
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> renderWindow_;
    vtkSmartPointer<vtkRenderer> renderer_;
    vtkSmartPointer<vtkDataSetMapper> mapper_;
    vtkSmartPointer<vtkActor> actor_;
    vtkSmartPointer<vtkInteractorStyleTrackballCamera> interactionStyle_;
    vtkSmartPointer<vtkAxesActor> axesActor_;
    vtkSmartPointer<vtkOrientationMarkerWidget> orientationMarker_;
    vtkSmartPointer<vtkLookupTable> lookupTable_;
    vtkSmartPointer<vtkScalarBarActor> scalarBar_;
    vtkSmartPointer<vtkWarpVector> warpVector_;
    vtkSmartPointer<vtkUnstructuredGrid> currentGrid_;
    QString lastError_;
    std::string scalarArrayName_;
    ResultFieldAssociation scalarAssociation_{
        ResultFieldAssociation::Point};
    int scalarComponent_{0};
    std::string displacementArrayName_;
    bool scalarFieldVisible_{false};
    bool deformationEnabled_{false};
    bool legendRequestedVisible_{true};
    bool fitOnNextShow_{false};
    double deformationScale_{1.0};
    VtkMeshDisplayMode displayMode_{
        VtkMeshDisplayMode::SurfaceWithEdges};
};
