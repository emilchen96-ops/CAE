#pragma once

#include "MeshData.hpp"
#include "ResultField.hpp"

#include <QColor>
#include <QString>
#include <QWidget>

#include <vtkSmartPointer.h>
#include <vtkType.h>

#include <memory>
#include <string>
#include <vector>

class QVTKOpenGLNativeWidget;
class QLabel;
class QResizeEvent;
class QShowEvent;
class vtkAxesActor;
class vtkGenericOpenGLRenderWindow;
class vtkInteractorStyleTrackballCamera;
class vtkLookupTable;
class vtkOrientationMarkerWidget;
class vtkRenderer;
class vtkScalarBarActor;
class vtkUnstructuredGrid;
class vtkWarpVector;

enum class VtkMeshDisplayMode {
    Points,
    Wireframe,
    SurfaceOnly,
    SurfaceWithEdges
};

struct VtkPostViewPart {
    int id{-1};
    vtkIdType firstCell{0};
    vtkIdType cellCount{0};
    bool visible{true};
};

enum class PostFilterType {
    Clip,
    Slice,
    Threshold,
    PointToCell,
    CellToPoint
};

enum class PostFilterAxis { X, Y, Z };

struct PostFilterResultInfo {
    int id{-1};
    std::string name;
    PostFilterType type{PostFilterType::Clip};
    bool visible{true};
    bool valid{false};
    int frameNumber{-1};
    std::string fieldName;
    ResultFieldAssociation association{ResultFieldAssociation::Cell};
    std::string parameters;
    vtkIdType pointCount{0};
    vtkIdType cellCount{0};
    std::string errorMessage;
};

class VtkPostViewWidget final : public QWidget {
    Q_OBJECT

public:
    explicit VtkPostViewWidget(QWidget* parent = nullptr);
    ~VtkPostViewWidget() override;

    bool displayMesh(const MeshData& mesh);
    bool displayResultGrid(vtkUnstructuredGrid* grid,
                           bool resetCamera = true);
    bool displayResultGrid(vtkUnstructuredGrid* grid,
                           const std::vector<VtkPostViewPart>& parts,
                           bool resetCamera = true);
    void clearScene();
    void fitAll();

    void setAxonometricView();
    void setFrontView();
    void setBackView();
    void setLeftView();
    void setRightView();
    void setTopView();
    void setBottomView();

    bool showScalarField(const std::string& arrayName,
                         ResultFieldAssociation association,
                         int component,
                         const std::string& displayName);
    void clearScalarField();
    bool useAutomaticScalarRange();
    bool setManualScalarRange(double minimum, double maximum);
    double scalarRangeMinimum() const;
    double scalarRangeMaximum() const;

    bool setDisplacementField(const std::string& arrayName);
    void clearDisplacementField();
    bool setDeformationEnabled(bool enabled);
    bool setDeformationScale(double scale);
    void setLegendVisible(bool visible);

    void setPoints();
    void setWireframe();
    void setSurfaceOnly();
    void setSurfaceWithEdges();
    bool setPointSize(double size);
    double pointSize() const;
    void setBackgroundColor(const QColor& color);
    QColor backgroundColor() const;

    bool setPartVisible(int partId, bool visible);
    bool partVisible(int partId) const;

    int createClipFilter(const std::string& name, PostFilterAxis axis,
                         double position, bool keepPositive,
                         int frameNumber);
    int createSliceFilter(const std::string& name, PostFilterAxis axis,
                          double position, int frameNumber);
    int createThresholdFilter(
        const std::string& name, const std::string& fieldName,
        ResultFieldAssociation association, int component,
        double minimum, double maximum, int frameNumber);
    int createPointToCellFilter(const std::string& name,
                                int frameNumber);
    int createCellToPointFilter(const std::string& name,
                                int frameNumber);
    bool setFilterVisible(int filterId, bool visible);
    bool renameFilter(int filterId, const std::string& name);
    bool removeFilter(int filterId);
    void clearFilters();
    void refreshFilters(int frameNumber);
    std::vector<PostFilterResultInfo> filterResults() const;

    bool saveCurrentViewToPng(const QString& filePath);

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
    struct PartPipeline;
    struct FilterPipeline;

    bool displayGridInternal(vtkUnstructuredGrid* grid,
                             const std::vector<VtkPostViewPart>& parts,
                             bool resetCamera);
    void rebuildPartPipelines(const std::vector<VtkPostViewPart>& parts);
    void configureMapperScalarState(PartPipeline& pipeline);
    void configureFilterScalarState(FilterPipeline& pipeline);
    void applyDisplayMode(VtkMeshDisplayMode mode);
    void applyDisplayModeToParts();
    bool applyScalarRange(double minimum, double maximum);
    void updateInputPipeline();
    void updateFilterPipeline(FilterPipeline& pipeline,
                              int frameNumber);
    void setStandardView(double directionX, double directionY,
                         double directionZ, double upX, double upY,
                         double upZ);
    void render();

    QVTKOpenGLNativeWidget* vtkWidget_{nullptr};
    QLabel* unitLabel_{nullptr};
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> renderWindow_;
    vtkSmartPointer<vtkRenderer> renderer_;
    vtkSmartPointer<vtkInteractorStyleTrackballCamera> interactionStyle_;
    vtkSmartPointer<vtkAxesActor> axesActor_;
    vtkSmartPointer<vtkOrientationMarkerWidget> orientationMarker_;
    vtkSmartPointer<vtkLookupTable> lookupTable_;
    vtkSmartPointer<vtkScalarBarActor> scalarBar_;
    vtkSmartPointer<vtkWarpVector> warpVector_;
    vtkSmartPointer<vtkUnstructuredGrid> currentGrid_;
    std::vector<std::unique_ptr<PartPipeline>> parts_;
    std::vector<std::unique_ptr<FilterPipeline>> filters_;
    QString lastError_;
    std::string scalarArrayName_;
    std::string scalarDisplayName_;
    ResultFieldAssociation scalarAssociation_{ResultFieldAssociation::Point};
    int scalarComponent_{0};
    std::string displacementArrayName_;
    bool scalarFieldVisible_{false};
    bool deformationEnabled_{false};
    bool legendRequestedVisible_{true};
    bool fitOnNextShow_{false};
    double deformationScale_{1.0};
    double automaticScalarMinimum_{0.0};
    double automaticScalarMaximum_{1.0};
    double scalarRangeMinimum_{0.0};
    double scalarRangeMaximum_{1.0};
    double pointSize_{5.0};
    QColor backgroundColor_{QColor::fromRgbF(0.06, 0.08, 0.12)};
    int nextFilterId_{1};
    VtkMeshDisplayMode displayMode_{VtkMeshDisplayMode::SurfaceWithEdges};
};
