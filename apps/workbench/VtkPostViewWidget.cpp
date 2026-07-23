#include "VtkPostViewWidget.hpp"

#include "MeshDataToVtkConverter.hpp"

#include <QVTKOpenGLNativeWidget.h>

#include <QShowEvent>
#include <QVBoxLayout>

#include <vtkActor.h>
#include <vtkAxesActor.h>
#include <vtkCamera.h>
#include <vtkDataSetMapper.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkOrientationMarkerWidget.h>
#include <vtkProperty.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkUnstructuredGrid.h>

#include <exception>

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
          vtkSmartPointer<vtkOrientationMarkerWidget>::New()) {
    setObjectName(QStringLiteral("VtkPostViewWidget"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    vtkWidget_ = new QVTKOpenGLNativeWidget(this);
    layout->addWidget(vtkWidget_);

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

    const auto previousGrid = currentGrid_;
    const bool previousVisibility = actor_->GetVisibility() != 0;
    const VtkMeshDisplayMode previousMode = displayMode_;
    try {
        currentGrid_ = result.grid;
        mapper_->SetInputData(currentGrid_);
        mapper_->ScalarVisibilityOff();
        actor_->SetVisibility(true);
        applyDisplayMode(VtkMeshDisplayMode::SurfaceWithEdges);

        renderer_->ResetCamera();
        vtkCamera* camera = renderer_->GetActiveCamera();
        camera->Azimuth(45.0);
        camera->Elevation(30.0);
        camera->OrthogonalizeViewUp();
        renderer_->ResetCamera();
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
    mapper_->SetInputData(
        static_cast<vtkUnstructuredGrid*>(nullptr));
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

QString VtkPostViewWidget::lastError() const {
    return lastError_;
}

VtkMeshDisplayMode VtkPostViewWidget::displayMode() const {
    return displayMode_;
}

void VtkPostViewWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    render();
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

void VtkPostViewWidget::render() {
    if (renderWindow_ != nullptr && vtkWidget_ != nullptr &&
        vtkWidget_->isVisible()) {
        renderWindow_->Render();
    } else if (vtkWidget_ != nullptr) {
        vtkWidget_->update();
    }
}
