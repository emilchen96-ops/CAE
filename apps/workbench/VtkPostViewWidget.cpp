#include "VtkPostViewWidget.hpp"

#include "MeshDataToVtkConverter.hpp"

#include <QVTKOpenGLNativeWidget.h>

#include <QLabel>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>

#include <vtkActor.h>
#include <vtkAxesActor.h>
#include <vtkCamera.h>
#include <vtkCellData.h>
#include <vtkDataSetMapper.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkLookupTable.h>
#include <vtkOrientationMarkerWidget.h>
#include <vtkPointData.h>
#include <vtkProperty.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkScalarBarActor.h>
#include <vtkTextProperty.h>
#include <vtkUnstructuredGrid.h>
#include <vtkWarpVector.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>

VtkPostViewWidget::VtkPostViewWidget(QWidget* parent)
    : QWidget(parent),
      renderWindow_(vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New()),
      renderer_(vtkSmartPointer<vtkRenderer>::New()),
      mapper_(vtkSmartPointer<vtkDataSetMapper>::New()),
      actor_(vtkSmartPointer<vtkActor>::New()),
      interactionStyle_(
          vtkSmartPointer<vtkInteractorStyleTrackballCamera>::New()),
      axesActor_(vtkSmartPointer<vtkAxesActor>::New()),
      orientationMarker_(
          vtkSmartPointer<vtkOrientationMarkerWidget>::New()),
      lookupTable_(vtkSmartPointer<vtkLookupTable>::New()),
      scalarBar_(vtkSmartPointer<vtkScalarBarActor>::New()),
      warpVector_(vtkSmartPointer<vtkWarpVector>::New()) {
    setObjectName(QStringLiteral("VtkPostViewWidget"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    vtkWidget_ = new QVTKOpenGLNativeWidget(this);
    layout->addWidget(vtkWidget_);
    unitLabel_ = new QLabel(tr("单位：未提供"), vtkWidget_);
    unitLabel_->setStyleSheet(
        QStringLiteral("color: white; background: transparent;"));
    unitLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
    unitLabel_->hide();

    vtkWidget_->setRenderWindow(renderWindow_);
    renderWindow_->AddRenderer(renderer_);
    renderer_->SetBackground(0.06, 0.08, 0.12);
    renderer_->SetBackground2(0.18, 0.23, 0.31);
    renderer_->GradientBackgroundOn();

    actor_->SetMapper(mapper_);
    actor_->SetVisibility(false);
    actor_->GetProperty()->SetColor(0.72, 0.78, 0.86);
    actor_->GetProperty()->SetEdgeColor(0.12, 0.16, 0.22);
    actor_->GetProperty()->SetLineWidth(1.0);
    renderer_->AddActor(actor_);

    lookupTable_->SetHueRange(0.667, 0.0);
    lookupTable_->SetSaturationRange(1.0, 1.0);
    lookupTable_->SetValueRange(1.0, 1.0);
    lookupTable_->SetNumberOfTableValues(256);
    lookupTable_->SetNanColor(0.45, 0.45, 0.45, 1.0);
    lookupTable_->Build();
    mapper_->SetLookupTable(lookupTable_);

    scalarBar_->SetLookupTable(lookupTable_);
    scalarBar_->SetNumberOfLabels(5);
    scalarBar_->SetMaximumWidthInPixels(120);
    scalarBar_->SetPosition(0.84, 0.12);
    scalarBar_->SetWidth(0.13);
    scalarBar_->SetHeight(0.76);
    scalarBar_->GetTitleTextProperty()->SetColor(0.95, 0.95, 0.95);
    scalarBar_->GetLabelTextProperty()->SetColor(0.95, 0.95, 0.95);
    scalarBar_->SetVisibility(false);
    renderer_->AddActor2D(scalarBar_);

    vtkWidget_->interactor()->SetInteractorStyle(interactionStyle_);
    orientationMarker_->SetOrientationMarker(axesActor_);
    orientationMarker_->SetInteractor(vtkWidget_->interactor());
    orientationMarker_->SetViewport(0.0, 0.0, 0.18, 0.18);
    orientationMarker_->SetEnabled(1);
    orientationMarker_->InteractiveOff();
    applyDisplayMode(VtkMeshDisplayMode::SurfaceWithEdges);
}

VtkPostViewWidget::~VtkPostViewWidget() {
    if (orientationMarker_ != nullptr) {
        orientationMarker_->SetEnabled(0);
        orientationMarker_->SetInteractor(nullptr);
    }
}

bool VtkPostViewWidget::displayMesh(const MeshData& mesh) {
    const MeshDataToVtkConverter converter;
    const auto result = converter.convert(mesh);
    if (!result.success || result.grid == nullptr) {
        lastError_ = QString::fromUtf8(
            result.errorMessage.data(),
            static_cast<qsizetype>(result.errorMessage.size()));
        return false;
    }

    return displayResultGrid(result.grid);
}

bool VtkPostViewWidget::displayResultGrid(vtkUnstructuredGrid* grid,
                                          bool resetCamera) {
    if (grid == nullptr) {
        lastError_ = tr("VTK 结果网格为空。");
        return false;
    }
    if (grid->GetNumberOfPoints() <= 0) {
        lastError_ = tr("VTK 结果网格没有节点。");
        return false;
    }
    if (grid->GetNumberOfCells() <= 0) {
        lastError_ = tr("VTK 结果网格没有单元。");
        return false;
    }

    const auto previousGrid = currentGrid_;
    const bool previousVisibility = actor_->GetVisibility() != 0;
    const VtkMeshDisplayMode previousMode = displayMode_;
    try {
        currentGrid_ = grid;
        scalarArrayName_.clear();
        displacementArrayName_.clear();
        scalarFieldVisible_ = false;
        deformationEnabled_ = false;
        deformationScale_ = 1.0;
        mapper_->SetInputData(currentGrid_);
        mapper_->ScalarVisibilityOff();
        scalarBar_->SetVisibility(false);
        unitLabel_->hide();
        actor_->SetVisibility(true);
        applyDisplayMode(
            resetCamera ? VtkMeshDisplayMode::SurfaceWithEdges
                        : previousMode);
        fitOnNextShow_ = resetCamera && !isVisible();

        if (resetCamera) {
            renderer_->ResetCamera();
            vtkCamera* camera = renderer_->GetActiveCamera();
            camera->Azimuth(45.0);
            camera->Elevation(30.0);
            camera->OrthogonalizeViewUp();
            renderer_->ResetCamera();
        }
        renderer_->ResetCameraClippingRange();
        lastError_.clear();
        render();
        return true;
    } catch (const std::exception& exception) {
        lastError_ = tr("VTK 渲染异常：%1")
                         .arg(QString::fromUtf8(exception.what()));
    } catch (...) {
        lastError_ = tr("VTK 渲染发生未知异常。");
    }
    currentGrid_ = previousGrid;
    mapper_->SetInputData(currentGrid_);
    actor_->SetVisibility(previousVisibility);
    applyDisplayMode(previousMode);
    return false;
}

void VtkPostViewWidget::clearScene() {
    currentGrid_ = nullptr;
    scalarArrayName_.clear();
    displacementArrayName_.clear();
    scalarFieldVisible_ = false;
    deformationEnabled_ = false;
    mapper_->SetInputData(
        static_cast<vtkUnstructuredGrid*>(nullptr));
    mapper_->ScalarVisibilityOff();
    scalarBar_->SetVisibility(false);
    unitLabel_->hide();
    actor_->SetVisibility(false);
    lastError_.clear();
    render();
}

void VtkPostViewWidget::fitAll() {
    if (currentGrid_ == nullptr) {
        return;
    }
    renderer_->ResetCamera();
    renderer_->ResetCameraClippingRange();
    render();
}

bool VtkPostViewWidget::showScalarField(
    const std::string& arrayName,
    ResultFieldAssociation association,
    int component,
    const std::string& displayName) {
    if (currentGrid_ == nullptr) {
        lastError_ = tr("当前没有可显示的结果网格。");
        return false;
    }
    vtkDataArray* array =
        association == ResultFieldAssociation::Point
        ? currentGrid_->GetPointData()->GetArray(arrayName.c_str())
        : currentGrid_->GetCellData()->GetArray(arrayName.c_str());
    if (array == nullptr) {
        lastError_ = tr("找不到指定标量结果数组。");
        return false;
    }
    const vtkIdType expectedTuples =
        association == ResultFieldAssociation::Point
        ? currentGrid_->GetNumberOfPoints()
        : currentGrid_->GetNumberOfCells();
    if (array->GetNumberOfTuples() != expectedTuples) {
        lastError_ = tr("标量结果元组数量与网格不匹配。");
        return false;
    }
    if (component < 0 ||
        component >= array->GetNumberOfComponents()) {
        lastError_ = tr("标量结果分量无效。");
        return false;
    }

    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    for (vtkIdType tuple = 0; tuple < array->GetNumberOfTuples();
         ++tuple) {
        const double value = array->GetComponent(tuple, component);
        if (!std::isfinite(value)) {
            continue;
        }
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }
    if (!std::isfinite(minimum) || !std::isfinite(maximum)) {
        lastError_ = tr("当前标量结果没有有效有限数值。");
        return false;
    }

    scalarArrayName_ = arrayName;
    scalarAssociation_ = association;
    scalarComponent_ = component;
    scalarFieldVisible_ = true;
    if (association == ResultFieldAssociation::Point) {
        mapper_->SetScalarModeToUsePointFieldData();
    } else {
        mapper_->SetScalarModeToUseCellFieldData();
    }
    mapper_->SelectColorArray(scalarArrayName_.c_str());
    mapper_->SetColorModeToMapScalars();
    lookupTable_->SetVectorModeToComponent();
    lookupTable_->SetVectorComponent(component);
    if (minimum == maximum) {
        const double offset =
            std::max(std::abs(minimum) * 1.0e-9, 1.0e-12);
        lookupTable_->SetRange(minimum - offset, maximum + offset);
        mapper_->SetScalarRange(minimum - offset, maximum + offset);
    } else {
        lookupTable_->SetRange(minimum, maximum);
        mapper_->SetScalarRange(minimum, maximum);
    }
    lookupTable_->Build();
    mapper_->ScalarVisibilityOn();
    scalarBar_->SetTitle(displayName.c_str());
    scalarBar_->SetComponentTitle(nullptr);
    scalarBar_->SetVisibility(legendRequestedVisible_);
    unitLabel_->setVisible(legendRequestedVisible_);
    unitLabel_->raise();
    lastError_.clear();
    render();
    return true;
}

void VtkPostViewWidget::clearScalarField() {
    scalarFieldVisible_ = false;
    scalarArrayName_.clear();
    mapper_->ScalarVisibilityOff();
    scalarBar_->SetVisibility(false);
    unitLabel_->hide();
    render();
}

bool VtkPostViewWidget::setDisplacementField(
    const std::string& arrayName) {
    if (currentGrid_ == nullptr) {
        lastError_ = tr("当前没有可变形的结果网格。");
        return false;
    }
    vtkDataArray* array =
        currentGrid_->GetPointData()->GetArray(arrayName.c_str());
    if (array == nullptr) {
        lastError_ = tr("找不到指定节点位移字段。");
        return false;
    }
    if (array->GetNumberOfComponents() != 3 ||
        array->GetNumberOfTuples() != currentGrid_->GetNumberOfPoints()) {
        lastError_ = tr("位移字段必须是与节点数量一致的三分量节点矢量。");
        return false;
    }
    for (vtkIdType tuple = 0; tuple < array->GetNumberOfTuples();
         ++tuple) {
        for (int component = 0; component < 3; ++component) {
            if (!std::isfinite(
                    array->GetComponent(tuple, component))) {
                lastError_ = tr("位移字段包含 NaN 或无穷值。");
                return false;
            }
        }
    }
    displacementArrayName_ = arrayName;
    lastError_.clear();
    updateInputPipeline();
    return true;
}

void VtkPostViewWidget::clearDisplacementField() {
    displacementArrayName_.clear();
    deformationEnabled_ = false;
    updateInputPipeline();
}

bool VtkPostViewWidget::setDeformationEnabled(bool enabled) {
    if (enabled && displacementArrayName_.empty()) {
        lastError_ = tr("请先选择有效的节点位移字段。");
        return false;
    }
    deformationEnabled_ = enabled;
    lastError_.clear();
    updateInputPipeline();
    return true;
}

bool VtkPostViewWidget::setDeformationScale(double scale) {
    if (!std::isfinite(scale) || scale < 0.0) {
        lastError_ = tr("变形倍率必须是大于等于零的有限数。");
        return false;
    }
    deformationScale_ = scale;
    lastError_.clear();
    updateInputPipeline();
    return true;
}

void VtkPostViewWidget::setLegendVisible(bool visible) {
    legendRequestedVisible_ = visible;
    scalarBar_->SetVisibility(visible && scalarFieldVisible_);
    unitLabel_->setVisible(visible && scalarFieldVisible_);
    unitLabel_->raise();
    render();
}

void VtkPostViewWidget::setSurfaceWithEdges() {
    applyDisplayMode(VtkMeshDisplayMode::SurfaceWithEdges);
}

void VtkPostViewWidget::setSurfaceOnly() {
    applyDisplayMode(VtkMeshDisplayMode::SurfaceOnly);
}

void VtkPostViewWidget::setWireframe() {
    applyDisplayMode(VtkMeshDisplayMode::Wireframe);
}

bool VtkPostViewWidget::hasMesh() const {
    return currentGrid_ != nullptr;
}

bool VtkPostViewWidget::scalarFieldVisible() const {
    return scalarFieldVisible_;
}

bool VtkPostViewWidget::deformationEnabled() const {
    return deformationEnabled_;
}

bool VtkPostViewWidget::legendVisible() const {
    return legendRequestedVisible_ && scalarFieldVisible_;
}

double VtkPostViewWidget::deformationScale() const {
    return deformationScale_;
}

QString VtkPostViewWidget::lastError() const {
    return lastError_;
}

VtkMeshDisplayMode VtkPostViewWidget::displayMode() const {
    return displayMode_;
}

void VtkPostViewWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (unitLabel_ == nullptr || vtkWidget_ == nullptr) {
        return;
    }
    unitLabel_->adjustSize();
    const int margin = 12;
    const int x = std::max(
        margin,
        vtkWidget_->width() - unitLabel_->width() - margin);
    const int y = std::max(
        margin,
        vtkWidget_->height() - unitLabel_->height() - margin);
    unitLabel_->move(x, y);
}

void VtkPostViewWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    render();
    if (fitOnNextShow_) {
        fitOnNextShow_ = false;
        QTimer::singleShot(0, this, [this] { fitAll(); });
    }
}

void VtkPostViewWidget::applyDisplayMode(VtkMeshDisplayMode mode) {
    displayMode_ = mode;
    switch (mode) {
    case VtkMeshDisplayMode::SurfaceWithEdges:
        actor_->GetProperty()->SetRepresentationToSurface();
        actor_->GetProperty()->EdgeVisibilityOn();
        break;
    case VtkMeshDisplayMode::SurfaceOnly:
        actor_->GetProperty()->SetRepresentationToSurface();
        actor_->GetProperty()->EdgeVisibilityOff();
        break;
    case VtkMeshDisplayMode::Wireframe:
        actor_->GetProperty()->SetRepresentationToWireframe();
        actor_->GetProperty()->EdgeVisibilityOff();
        break;
    }
    render();
}

void VtkPostViewWidget::updateInputPipeline() {
    if (currentGrid_ == nullptr) {
        return;
    }
    if (deformationEnabled_ && deformationScale_ > 0.0 &&
        !displacementArrayName_.empty()) {
        warpVector_->SetInputData(currentGrid_);
        warpVector_->SetInputArrayToProcess(
            0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_POINTS,
            displacementArrayName_.c_str());
        warpVector_->SetScaleFactor(deformationScale_);
        mapper_->SetInputConnection(warpVector_->GetOutputPort());
    } else {
        mapper_->SetInputData(currentGrid_);
    }
    mapper_->Update();
    renderer_->ResetCameraClippingRange();
    render();
}

void VtkPostViewWidget::render() {
    if (renderWindow_ != nullptr && vtkWidget_ != nullptr &&
        vtkWidget_->isVisible()) {
        renderWindow_->Render();
    } else if (vtkWidget_ != nullptr) {
        vtkWidget_->update();
    }
}
