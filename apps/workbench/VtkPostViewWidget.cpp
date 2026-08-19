#include "VtkPostViewWidget.hpp"

#include "MeshDataToVtkConverter.hpp"

#include <QVTKOpenGLNativeWidget.h>

#include <QLabel>
#include <QResizeEvent>
#include <QSaveFile>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>

#include <vtkActor.h>
#include <vtkAlgorithm.h>
#include <vtkAxesActor.h>
#include <vtkCamera.h>
#include <vtkCellData.h>
#include <vtkCellDataToPointData.h>
#include <vtkClipDataSet.h>
#include <vtkCutter.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkDataSet.h>
#include <vtkDataSetMapper.h>
#include <vtkErrorCode.h>
#include <vtkExtractCells.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkLookupTable.h>
#include <vtkOrientationMarkerWidget.h>
#include <vtkPNGWriter.h>
#include <vtkPlane.h>
#include <vtkPointData.h>
#include <vtkPointDataToCellData.h>
#include <vtkProperty.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkScalarBarActor.h>
#include <vtkTextProperty.h>
#include <vtkThreshold.h>
#include <vtkUnsignedCharArray.h>
#include <vtkUnstructuredGrid.h>
#include <vtkWarpVector.h>
#include <vtkWindowToImageFilter.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>

struct VtkPostViewWidget::PartPipeline {
    VtkPostViewPart descriptor;
    vtkSmartPointer<vtkExtractCells> extract;
    vtkSmartPointer<vtkDataSetMapper> mapper;
    vtkSmartPointer<vtkActor> actor;
};

struct VtkPostViewWidget::FilterPipeline {
    PostFilterResultInfo info;
    PostFilterAxis axis{PostFilterAxis::X};
    double position{0.0};
    bool keepPositive{true};
    int component{0};
    double minimum{0.0};
    double maximum{1.0};
    vtkSmartPointer<vtkPlane> plane;
    vtkSmartPointer<vtkClipDataSet> clip;
    vtkSmartPointer<vtkCutter> cutter;
    vtkSmartPointer<vtkThreshold> threshold;
    vtkSmartPointer<vtkPointDataToCellData> pointToCell;
    vtkSmartPointer<vtkCellDataToPointData> cellToPoint;
    vtkSmartPointer<vtkAlgorithm> algorithm;
    vtkSmartPointer<vtkDataSetMapper> mapper;
    vtkSmartPointer<vtkActor> actor;
};

namespace {

void axisNormal(PostFilterAxis axis, double normal[3]) {
    normal[0] = axis == PostFilterAxis::X ? 1.0 : 0.0;
    normal[1] = axis == PostFilterAxis::Y ? 1.0 : 0.0;
    normal[2] = axis == PostFilterAxis::Z ? 1.0 : 0.0;
}

int vtkAssociation(ResultFieldAssociation association) {
    return association == ResultFieldAssociation::Point
        ? vtkDataObject::FIELD_ASSOCIATION_POINTS
        : vtkDataObject::FIELD_ASSOCIATION_CELLS;
}

std::string axisName(PostFilterAxis axis) {
    switch (axis) {
    case PostFilterAxis::X: return "X";
    case PostFilterAxis::Y: return "Y";
    case PostFilterAxis::Z: return "Z";
    }
    return {};
}

} // namespace

VtkPostViewWidget::VtkPostViewWidget(QWidget* parent)
    : QWidget(parent),
      renderWindow_(vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New()),
      renderer_(vtkSmartPointer<vtkRenderer>::New()),
      interactionStyle_(vtkSmartPointer<vtkInteractorStyleTrackballCamera>::New()),
      axesActor_(vtkSmartPointer<vtkAxesActor>::New()),
      orientationMarker_(vtkSmartPointer<vtkOrientationMarkerWidget>::New()),
      lookupTable_(vtkSmartPointer<vtkLookupTable>::New()),
      scalarBar_(vtkSmartPointer<vtkScalarBarActor>::New()),
      warpVector_(vtkSmartPointer<vtkWarpVector>::New()) {
    setObjectName(QStringLiteral("VtkPostViewWidget"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    vtkWidget_ = new QVTKOpenGLNativeWidget(this);
    layout->addWidget(vtkWidget_);
    unitLabel_ = new QLabel(tr("单位：未提供"), vtkWidget_);
    unitLabel_->setStyleSheet(QStringLiteral("color: white; background: transparent;"));
    unitLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
    unitLabel_->hide();

    vtkWidget_->setRenderWindow(renderWindow_);
    renderWindow_->AddRenderer(renderer_);
    renderer_->SetBackground(0.06, 0.08, 0.12);
    renderer_->SetBackground2(0.18, 0.23, 0.31);
    renderer_->GradientBackgroundOn();

    lookupTable_->SetHueRange(0.667, 0.0);
    lookupTable_->SetSaturationRange(1.0, 1.0);
    lookupTable_->SetValueRange(1.0, 1.0);
    lookupTable_->SetNumberOfTableValues(256);
    lookupTable_->SetNanColor(0.45, 0.45, 0.45, 1.0);
    lookupTable_->Build();

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
    return result.success && result.grid != nullptr
        ? displayResultGrid(result.grid)
        : (lastError_ = QString::fromUtf8(result.errorMessage), false);
}

bool VtkPostViewWidget::displayResultGrid(vtkUnstructuredGrid* grid,
                                          bool resetCamera) {
    std::vector<VtkPostViewPart> parts;
    if (grid != nullptr) {
        parts.push_back({0, 0, grid->GetNumberOfCells(), true});
    }
    return displayGridInternal(grid, parts, resetCamera);
}

bool VtkPostViewWidget::displayResultGrid(
    vtkUnstructuredGrid* grid, const std::vector<VtkPostViewPart>& parts,
    bool resetCamera) {
    return displayGridInternal(grid, parts, resetCamera);
}

bool VtkPostViewWidget::displayGridInternal(
    vtkUnstructuredGrid* grid, const std::vector<VtkPostViewPart>& parts,
    bool resetCamera) {
    if (grid == nullptr || grid->GetNumberOfPoints() <= 0 ||
        grid->GetNumberOfCells() <= 0) {
        lastError_ = tr("VTK 结果网格为空或没有有效节点、单元。");
        return false;
    }
    for (const auto& part : parts) {
        if (part.id < 0 || part.firstCell < 0 || part.cellCount <= 0 ||
            part.firstCell + part.cellCount > grid->GetNumberOfCells()) {
            lastError_ = tr("部件单元范围无效。");
            return false;
        }
    }
    try {
        currentGrid_ = grid;
        scalarArrayName_.clear();
        scalarDisplayName_.clear();
        displacementArrayName_.clear();
        scalarFieldVisible_ = false;
        deformationEnabled_ = false;
        deformationScale_ = 1.0;
        scalarBar_->SetVisibility(false);
        unitLabel_->hide();
        rebuildPartPipelines(parts);
        updateInputPipeline();
        fitOnNextShow_ = resetCamera && !isVisible();
        if (resetCamera) {
            setAxonometricView();
        } else {
            renderer_->ResetCameraClippingRange();
            refreshFilters(-1);
            render();
        }
        lastError_.clear();
        return true;
    } catch (const std::exception& exception) {
        lastError_ = tr("VTK 渲染异常：%1").arg(QString::fromUtf8(exception.what()));
    } catch (...) {
        lastError_ = tr("VTK 渲染发生未知异常。");
    }
    return false;
}

void VtkPostViewWidget::rebuildPartPipelines(
    const std::vector<VtkPostViewPart>& descriptors) {
    for (const auto& part : parts_) {
        renderer_->RemoveActor(part->actor);
    }
    parts_.clear();
    for (const auto& descriptor : descriptors) {
        auto part = std::make_unique<PartPipeline>();
        part->descriptor = descriptor;
        part->extract = vtkSmartPointer<vtkExtractCells>::New();
        part->extract->AddCellRange(descriptor.firstCell,
            descriptor.firstCell + descriptor.cellCount - 1);
        part->extract->AssumeSortedAndUniqueIdsOn();
        part->mapper = vtkSmartPointer<vtkDataSetMapper>::New();
        part->mapper->SetLookupTable(lookupTable_);
        part->mapper->SetInputConnection(part->extract->GetOutputPort());
        part->actor = vtkSmartPointer<vtkActor>::New();
        part->actor->SetMapper(part->mapper);
        part->actor->SetVisibility(descriptor.visible);
        part->actor->GetProperty()->SetColor(0.72, 0.78, 0.86);
        part->actor->GetProperty()->SetEdgeColor(0.12, 0.16, 0.22);
        part->actor->GetProperty()->SetLineWidth(1.0);
        renderer_->AddActor(part->actor);
        parts_.push_back(std::move(part));
    }
    applyDisplayModeToParts();
}

void VtkPostViewWidget::clearScene() {
    clearFilters();
    for (const auto& part : parts_) {
        renderer_->RemoveActor(part->actor);
    }
    parts_.clear();
    currentGrid_ = nullptr;
    scalarFieldVisible_ = false;
    deformationEnabled_ = false;
    scalarBar_->SetVisibility(false);
    unitLabel_->hide();
    render();
}

void VtkPostViewWidget::fitAll() {
    if (currentGrid_ == nullptr) return;
    renderer_->ResetCamera();
    renderer_->ResetCameraClippingRange();
    render();
}

void VtkPostViewWidget::setStandardView(double dx, double dy, double dz,
                                        double ux, double uy, double uz) {
    if (currentGrid_ == nullptr) return;
    vtkCamera* camera = renderer_->GetActiveCamera();
    const double* focal = camera->GetFocalPoint();
    double distance = camera->GetDistance();
    if (!std::isfinite(distance) || distance <= 0.0) distance = 1.0;
    const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
    camera->SetPosition(focal[0] + distance * dx / length,
                        focal[1] + distance * dy / length,
                        focal[2] + distance * dz / length);
    camera->SetViewUp(ux, uy, uz);
    camera->OrthogonalizeViewUp();
    renderer_->ResetCamera();
    renderer_->ResetCameraClippingRange();
    render();
}

void VtkPostViewWidget::setAxonometricView() { setStandardView(1, -1, 1, 0, 0, 1); }
void VtkPostViewWidget::setFrontView() { setStandardView(0, -1, 0, 0, 0, 1); }
void VtkPostViewWidget::setBackView() { setStandardView(0, 1, 0, 0, 0, 1); }
void VtkPostViewWidget::setLeftView() { setStandardView(-1, 0, 0, 0, 0, 1); }
void VtkPostViewWidget::setRightView() { setStandardView(1, 0, 0, 0, 0, 1); }
void VtkPostViewWidget::setTopView() { setStandardView(0, 0, 1, 0, 1, 0); }
void VtkPostViewWidget::setBottomView() { setStandardView(0, 0, -1, 0, -1, 0); }

bool VtkPostViewWidget::showScalarField(
    const std::string& arrayName, ResultFieldAssociation association,
    int component, const std::string& displayName) {
    if (currentGrid_ == nullptr) {
        lastError_ = tr("当前没有可显示的结果网格。");
        return false;
    }
    vtkDataArray* array = association == ResultFieldAssociation::Point
        ? currentGrid_->GetPointData()->GetArray(arrayName.c_str())
        : currentGrid_->GetCellData()->GetArray(arrayName.c_str());
    if (array == nullptr || component < 0 ||
        component >= array->GetNumberOfComponents()) {
        lastError_ = tr("找不到指定标量结果数组或分量无效。");
        return false;
    }
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -minimum;
    for (vtkIdType tuple = 0; tuple < array->GetNumberOfTuples(); ++tuple) {
        const double value = array->GetComponent(tuple, component);
        if (std::isfinite(value)) {
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }
    }
    if (!std::isfinite(minimum) || !std::isfinite(maximum)) {
        lastError_ = tr("当前标量结果没有有效有限数值。");
        return false;
    }
    scalarArrayName_ = arrayName;
    scalarDisplayName_ = displayName;
    scalarAssociation_ = association;
    scalarComponent_ = component;
    scalarFieldVisible_ = true;
    lookupTable_->SetVectorModeToComponent();
    lookupTable_->SetVectorComponent(component);
    if (minimum == maximum) {
        const double offset = std::max(std::abs(minimum) * 1.0e-9, 1.0e-12);
        minimum -= offset;
        maximum += offset;
    }
    automaticScalarMinimum_ = minimum;
    automaticScalarMaximum_ = maximum;
    applyScalarRange(minimum, maximum);
    currentGrid_->Modified();
    for (auto& part : parts_) configureMapperScalarState(*part);
    for (auto& filter : filters_) {
        updateFilterPipeline(*filter, filter->info.frameNumber);
    }
    scalarBar_->SetTitle(displayName.c_str());
    scalarBar_->SetVisibility(legendRequestedVisible_);
    unitLabel_->setVisible(legendRequestedVisible_);
    unitLabel_->raise();
    lastError_.clear();
    render();
    return true;
}

void VtkPostViewWidget::configureMapperScalarState(PartPipeline& part) {
    if (!scalarFieldVisible_) {
        part.mapper->ScalarVisibilityOff();
        return;
    }
    if (scalarAssociation_ == ResultFieldAssociation::Point)
        part.mapper->SetScalarModeToUsePointFieldData();
    else
        part.mapper->SetScalarModeToUseCellFieldData();
    part.mapper->SelectColorArray(scalarArrayName_.c_str());
    part.mapper->SetColorModeToMapScalars();
    part.mapper->SetScalarRange(scalarRangeMinimum_, scalarRangeMaximum_);
    part.mapper->ScalarVisibilityOn();
}

void VtkPostViewWidget::clearScalarField() {
    scalarFieldVisible_ = false;
    scalarArrayName_.clear();
    for (auto& part : parts_) part->mapper->ScalarVisibilityOff();
    for (auto& filter : filters_) filter->mapper->ScalarVisibilityOff();
    scalarBar_->SetVisibility(false);
    unitLabel_->hide();
    render();
}

bool VtkPostViewWidget::useAutomaticScalarRange() {
    return scalarFieldVisible_ &&
           applyScalarRange(automaticScalarMinimum_, automaticScalarMaximum_);
}

bool VtkPostViewWidget::setManualScalarRange(double minimum, double maximum) {
    return scalarFieldVisible_ && applyScalarRange(minimum, maximum);
}

double VtkPostViewWidget::scalarRangeMinimum() const { return scalarRangeMinimum_; }
double VtkPostViewWidget::scalarRangeMaximum() const { return scalarRangeMaximum_; }

bool VtkPostViewWidget::applyScalarRange(double minimum, double maximum) {
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum >= maximum) {
        lastError_ = tr("云图最小值必须小于最大值，且均为有限数值。");
        return false;
    }
    lookupTable_->SetRange(minimum, maximum);
    lookupTable_->Build();
    scalarRangeMinimum_ = minimum;
    scalarRangeMaximum_ = maximum;
    for (auto& part : parts_) part->mapper->SetScalarRange(minimum, maximum);
    for (auto& filter : filters_) filter->mapper->SetScalarRange(minimum, maximum);
    lastError_.clear();
    render();
    return true;
}

bool VtkPostViewWidget::setDisplacementField(const std::string& arrayName) {
    if (currentGrid_ == nullptr) return false;
    vtkDataArray* array = currentGrid_->GetPointData()->GetArray(arrayName.c_str());
    if (array == nullptr || array->GetNumberOfComponents() != 3 ||
        array->GetNumberOfTuples() != currentGrid_->GetNumberOfPoints()) {
        lastError_ = tr("位移字段必须是与节点数量一致的三分量节点矢量。");
        return false;
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
    updateInputPipeline();
    return true;
}

bool VtkPostViewWidget::setDeformationScale(double scale) {
    if (!std::isfinite(scale) || scale < 0.0) {
        lastError_ = tr("变形倍率必须是大于等于零的有限数。");
        return false;
    }
    deformationScale_ = scale;
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

void VtkPostViewWidget::setPoints() { applyDisplayMode(VtkMeshDisplayMode::Points); }
void VtkPostViewWidget::setWireframe() { applyDisplayMode(VtkMeshDisplayMode::Wireframe); }
void VtkPostViewWidget::setSurfaceOnly() { applyDisplayMode(VtkMeshDisplayMode::SurfaceOnly); }
void VtkPostViewWidget::setSurfaceWithEdges() { applyDisplayMode(VtkMeshDisplayMode::SurfaceWithEdges); }

bool VtkPostViewWidget::setPointSize(double size) {
    if (!std::isfinite(size) || size < 1.0 || size > 50.0) {
        lastError_ = tr("点大小必须是 1 到 50 之间的有限数值。");
        return false;
    }
    pointSize_ = size;
    applyDisplayModeToParts();
    lastError_.clear();
    render();
    return true;
}

double VtkPostViewWidget::pointSize() const { return pointSize_; }

void VtkPostViewWidget::setBackgroundColor(const QColor& color) {
    if (!color.isValid()) return;
    backgroundColor_ = color;
    renderer_->SetBackground(color.redF(), color.greenF(), color.blueF());
    renderer_->GradientBackgroundOff();

    const bool lightBackground = color.lightnessF() >= 0.55;
    const double text = lightBackground ? 0.08 : 0.95;
    scalarBar_->GetTitleTextProperty()->SetColor(text, text, text);
    scalarBar_->GetLabelTextProperty()->SetColor(text, text, text);
    unitLabel_->setStyleSheet(
        lightBackground
            ? QStringLiteral("color: #141414; background: transparent;")
            : QStringLiteral("color: #f2f2f2; background: transparent;"));
    render();
}

QColor VtkPostViewWidget::backgroundColor() const {
    return backgroundColor_;
}

void VtkPostViewWidget::applyDisplayMode(VtkMeshDisplayMode mode) {
    displayMode_ = mode;
    applyDisplayModeToParts();
    render();
}

void VtkPostViewWidget::applyDisplayModeToParts() {
    const auto apply = [this](vtkActor* actor) {
        switch (displayMode_) {
        case VtkMeshDisplayMode::Points:
            actor->GetProperty()->SetRepresentationToPoints();
            actor->GetProperty()->SetPointSize(pointSize_);
            actor->GetProperty()->EdgeVisibilityOff();
            break;
        case VtkMeshDisplayMode::Wireframe:
            actor->GetProperty()->SetRepresentationToWireframe();
            actor->GetProperty()->EdgeVisibilityOff();
            break;
        case VtkMeshDisplayMode::SurfaceOnly:
            actor->GetProperty()->SetRepresentationToSurface();
            actor->GetProperty()->EdgeVisibilityOff();
            break;
        case VtkMeshDisplayMode::SurfaceWithEdges:
            actor->GetProperty()->SetRepresentationToSurface();
            actor->GetProperty()->EdgeVisibilityOn();
            break;
        }
    };
    for (auto& part : parts_) apply(part->actor);
    for (auto& filter : filters_) apply(filter->actor);
}

bool VtkPostViewWidget::setPartVisible(int partId, bool visible) {
    const auto it = std::find_if(parts_.begin(), parts_.end(),
        [partId](const auto& part) { return part->descriptor.id == partId; });
    if (it == parts_.end()) return false;
    (*it)->descriptor.visible = visible;
    (*it)->actor->SetVisibility(visible);
    render();
    return true;
}

bool VtkPostViewWidget::partVisible(int partId) const {
    const auto it = std::find_if(parts_.begin(), parts_.end(),
        [partId](const auto& part) { return part->descriptor.id == partId; });
    return it != parts_.end() && (*it)->actor->GetVisibility() != 0;
}

void VtkPostViewWidget::updateInputPipeline() {
    if (currentGrid_ == nullptr) return;
    const bool warped = deformationEnabled_ && deformationScale_ > 0.0 &&
                        !displacementArrayName_.empty();
    if (warped) {
        warpVector_->SetInputData(currentGrid_);
        warpVector_->SetInputArrayToProcess(0, 0, 0,
            vtkDataObject::FIELD_ASSOCIATION_POINTS,
            displacementArrayName_.c_str());
        warpVector_->SetScaleFactor(deformationScale_);
    }
    for (auto& part : parts_) {
        if (warped)
            part->extract->SetInputConnection(warpVector_->GetOutputPort());
        else
            part->extract->SetInputData(currentGrid_);
        part->extract->Update();
        configureMapperScalarState(*part);
    }
    for (auto& filter : filters_) updateFilterPipeline(*filter, filter->info.frameNumber);
    renderer_->ResetCameraClippingRange();
    render();
}

int VtkPostViewWidget::createClipFilter(const std::string& name,
    PostFilterAxis axis, double position, bool keepPositive, int frameNumber) {
    auto item = std::make_unique<FilterPipeline>();
    item->info = {nextFilterId_++, name, PostFilterType::Clip, true};
    item->axis = axis; item->position = position; item->keepPositive = keepPositive;
    item->info.parameters = axisName(axis) + "=" + std::to_string(position) +
        (keepPositive ? ", 保留正侧" : ", 保留负侧");
    item->plane = vtkSmartPointer<vtkPlane>::New();
    double normal[3]; axisNormal(axis, normal);
    item->plane->SetNormal(normal); item->plane->SetOrigin(
        axis == PostFilterAxis::X ? position : 0.0,
        axis == PostFilterAxis::Y ? position : 0.0,
        axis == PostFilterAxis::Z ? position : 0.0);
    item->clip = vtkSmartPointer<vtkClipDataSet>::New();
    item->clip->SetClipFunction(item->plane);
    // vtkClipDataSet 默认保留隐式函数值 f<=0 的一侧（平面法向的反侧）；
    // keepPositive 语义为“保留法向正侧”，因此必须开启 InsideOut。
    item->clip->SetInsideOut(keepPositive);
    item->algorithm = item->clip;
    const int id = item->info.id;
    filters_.push_back(std::move(item));
    updateFilterPipeline(*filters_.back(), frameNumber);
    if (!filters_.back()->info.valid) {
        lastError_ = tr("剖切结果为空。");
        renderer_->RemoveActor(filters_.back()->actor);
        filters_.pop_back();
        return -1;
    }
    return id;
}

int VtkPostViewWidget::createSliceFilter(const std::string& name,
    PostFilterAxis axis, double position, int frameNumber) {
    auto item = std::make_unique<FilterPipeline>();
    item->info = {nextFilterId_++, name, PostFilterType::Slice, true};
    item->axis = axis; item->position = position;
    item->info.parameters = axisName(axis) + "=" + std::to_string(position);
    item->plane = vtkSmartPointer<vtkPlane>::New();
    double normal[3]; axisNormal(axis, normal);
    item->plane->SetNormal(normal); item->plane->SetOrigin(
        axis == PostFilterAxis::X ? position : 0.0,
        axis == PostFilterAxis::Y ? position : 0.0,
        axis == PostFilterAxis::Z ? position : 0.0);
    item->cutter = vtkSmartPointer<vtkCutter>::New();
    item->cutter->SetCutFunction(item->plane);
    item->algorithm = item->cutter;
    const int id = item->info.id;
    filters_.push_back(std::move(item));
    updateFilterPipeline(*filters_.back(), frameNumber);
    if (!filters_.back()->info.valid) {
        lastError_ = tr("切片平面与当前结果没有交集。");
        renderer_->RemoveActor(filters_.back()->actor);
        filters_.pop_back();
        return -1;
    }
    return id;
}

int VtkPostViewWidget::createThresholdFilter(const std::string& name,
    const std::string& fieldName, ResultFieldAssociation association,
    int component, double minimum, double maximum, int frameNumber) {
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum > maximum) {
        lastError_ = tr("阈值最小值不能大于最大值。"); return -1;
    }
    auto item = std::make_unique<FilterPipeline>();
    item->info = {nextFilterId_++, name, PostFilterType::Threshold, true};
    item->info.fieldName = fieldName; item->info.association = association;
    item->info.parameters = std::to_string(minimum) + " ~ " +
                            std::to_string(maximum);
    item->component = component; item->minimum = minimum; item->maximum = maximum;
    item->threshold = vtkSmartPointer<vtkThreshold>::New();
    item->threshold->SetInputArrayToProcess(0, 0, 0, vtkAssociation(association), fieldName.c_str());
    item->threshold->SetLowerThreshold(minimum);
    item->threshold->SetUpperThreshold(maximum);
    item->threshold->SetThresholdFunction(vtkThreshold::THRESHOLD_BETWEEN);
    item->threshold->SetComponentModeToUseSelected();
    item->threshold->SetSelectedComponent(component);
    item->algorithm = item->threshold;
    const int id = item->info.id;
    filters_.push_back(std::move(item));
    updateFilterPipeline(*filters_.back(), frameNumber);
    if (!filters_.back()->info.valid) {
        lastError_ = tr("当前阈值范围没有筛选出有效单元。");
        renderer_->RemoveActor(filters_.back()->actor);
        filters_.pop_back();
        return -1;
    }
    return id;
}

int VtkPostViewWidget::createPointToCellFilter(const std::string& name, int frameNumber) {
    auto item = std::make_unique<FilterPipeline>();
    item->info = {nextFilterId_++, name, PostFilterType::PointToCell, true};
    item->info.parameters = "节点数据 → 单元数据";
    item->pointToCell = vtkSmartPointer<vtkPointDataToCellData>::New();
    item->pointToCell->PassPointDataOn(); item->algorithm = item->pointToCell;
    const int id = item->info.id; filters_.push_back(std::move(item));
    updateFilterPipeline(*filters_.back(), frameNumber); return id;
}

int VtkPostViewWidget::createCellToPointFilter(const std::string& name, int frameNumber) {
    auto item = std::make_unique<FilterPipeline>();
    item->info = {nextFilterId_++, name, PostFilterType::CellToPoint, true};
    item->info.parameters = "单元数据 → 节点数据";
    item->cellToPoint = vtkSmartPointer<vtkCellDataToPointData>::New();
    item->cellToPoint->PassCellDataOn(); item->algorithm = item->cellToPoint;
    const int id = item->info.id; filters_.push_back(std::move(item));
    updateFilterPipeline(*filters_.back(), frameNumber); return id;
}

void VtkPostViewWidget::updateFilterPipeline(FilterPipeline& item, int frameNumber) {
    if (currentGrid_ == nullptr) return;
    if (item.threshold) {
        // 阈值依赖的字段（可能是派生数组）在新帧网格中缺失时，
        // vtkThreshold 不会更新输出，会把上一帧的陈旧几何保留在
        // actor 中继续显示。此处显式校验并隐藏过滤器，避免陈旧结果。
        vtkDataArray* inputArray =
            item.info.association == ResultFieldAssociation::Point
            ? currentGrid_->GetPointData()->GetArray(
                  item.info.fieldName.c_str())
            : currentGrid_->GetCellData()->GetArray(
                  item.info.fieldName.c_str());
        if (inputArray == nullptr) {
            item.info.frameNumber = frameNumber;
            item.info.valid = false;
            item.info.pointCount = 0;
            item.info.cellCount = 0;
            item.info.errorMessage =
                "当前帧不包含阈值字段（派生数组可能已随帧切换失效）。";
            if (item.actor != nullptr) {
                item.actor->SetVisibility(false);
            }
            return;
        }
    }
    const bool warped = deformationEnabled_ && deformationScale_ > 0.0 &&
                        !displacementArrayName_.empty();
    auto bind = [&](auto* filter) {
        if (warped) filter->SetInputConnection(warpVector_->GetOutputPort());
        else filter->SetInputData(currentGrid_);
    };
    if (item.clip) bind(item.clip.GetPointer());
    if (item.cutter) bind(item.cutter.GetPointer());
    if (item.threshold) bind(item.threshold.GetPointer());
    if (item.pointToCell) bind(item.pointToCell.GetPointer());
    if (item.cellToPoint) bind(item.cellToPoint.GetPointer());
    item.algorithm->Update();
    vtkDataSet* output = vtkDataSet::SafeDownCast(item.algorithm->GetOutputDataObject(0));
    item.info.frameNumber = frameNumber;
    item.info.valid = output != nullptr && output->GetNumberOfCells() > 0;
    item.info.pointCount = output == nullptr ? 0 : output->GetNumberOfPoints();
    item.info.cellCount = output == nullptr ? 0 : output->GetNumberOfCells();
    item.info.errorMessage = item.info.valid ? "" : "过滤结果为空或当前帧字段不可用。";
    if (item.mapper == nullptr) {
        item.mapper = vtkSmartPointer<vtkDataSetMapper>::New();
        item.mapper->SetLookupTable(lookupTable_);
        item.mapper->SetInputConnection(item.algorithm->GetOutputPort());
        item.actor = vtkSmartPointer<vtkActor>::New();
        item.actor->SetMapper(item.mapper);
        item.actor->GetProperty()->SetColor(0.92, 0.62, 0.18);
        item.actor->GetProperty()->SetEdgeColor(0.1, 0.1, 0.1);
        renderer_->AddActor(item.actor);
    }
    item.actor->SetVisibility(item.info.visible && item.info.valid);
    configureFilterScalarState(item);
    applyDisplayModeToParts();
}

void VtkPostViewWidget::configureFilterScalarState(FilterPipeline& item) {
    if (!scalarFieldVisible_ || !item.info.valid) {
        item.mapper->ScalarVisibilityOff(); return;
    }
    ResultFieldAssociation association = scalarAssociation_;
    if (item.info.type == PostFilterType::PointToCell &&
        association == ResultFieldAssociation::Point)
        association = ResultFieldAssociation::Cell;
    if (item.info.type == PostFilterType::CellToPoint &&
        association == ResultFieldAssociation::Cell)
        association = ResultFieldAssociation::Point;
    if (association == ResultFieldAssociation::Point)
        item.mapper->SetScalarModeToUsePointFieldData();
    else
        item.mapper->SetScalarModeToUseCellFieldData();
    item.mapper->SelectColorArray(scalarArrayName_.c_str());
    item.mapper->SetColorModeToMapScalars();
    item.mapper->SetScalarRange(scalarRangeMinimum_, scalarRangeMaximum_);
    item.mapper->ScalarVisibilityOn();
}

bool VtkPostViewWidget::setFilterVisible(int id, bool visible) {
    for (auto& item : filters_) if (item->info.id == id) {
        item->info.visible = visible;
        item->actor->SetVisibility(visible && item->info.valid);
        render(); return true;
    }
    return false;
}

bool VtkPostViewWidget::renameFilter(int id, const std::string& name) {
    if (name.empty()) return false;
    for (auto& item : filters_) if (item->info.id == id) {
        item->info.name = name; return true;
    }
    return false;
}

bool VtkPostViewWidget::removeFilter(int id) {
    const auto it = std::find_if(filters_.begin(), filters_.end(),
        [id](const auto& item) { return item->info.id == id; });
    if (it == filters_.end()) return false;
    renderer_->RemoveActor((*it)->actor); filters_.erase(it); render(); return true;
}

void VtkPostViewWidget::clearFilters() {
    for (const auto& item : filters_) renderer_->RemoveActor(item->actor);
    filters_.clear(); nextFilterId_ = 1;
}

void VtkPostViewWidget::refreshFilters(int frameNumber) {
    for (auto& item : filters_) updateFilterPipeline(*item, frameNumber);
    render();
}

std::vector<PostFilterResultInfo> VtkPostViewWidget::filterResults() const {
    std::vector<PostFilterResultInfo> result;
    for (const auto& item : filters_) result.push_back(item->info);
    return result;
}

bool VtkPostViewWidget::saveCurrentViewToPng(const QString& filePath) {
    if (currentGrid_ == nullptr || filePath.isEmpty()) {
        lastError_ = tr("当前没有可导出的三维视图。"); return false;
    }
    try {
        render();
        auto capture = vtkSmartPointer<vtkWindowToImageFilter>::New();
        capture->SetInput(renderWindow_);
        capture->SetScale(1);
        capture->SetInputBufferTypeToRGBA();
        capture->ReadFrontBufferOff();
        capture->Update();
        auto writer = vtkSmartPointer<vtkPNGWriter>::New();
        writer->SetInputConnection(capture->GetOutputPort());
        writer->WriteToMemoryOn();
        writer->Write();
        vtkUnsignedCharArray* bytes = writer->GetResult();
        if (writer->GetErrorCode() != vtkErrorCode::NoError || bytes == nullptr ||
            bytes->GetNumberOfValues() <= 0) {
            lastError_ = tr("VTK 无法生成 PNG 图片数据。"); return false;
        }
        QSaveFile output(filePath);
        if (!output.open(QIODevice::WriteOnly)) {
            lastError_ = tr("无法创建 PNG 文件：%1").arg(output.errorString()); return false;
        }
        const qint64 count = static_cast<qint64>(bytes->GetNumberOfValues());
        if (output.write(reinterpret_cast<const char*>(bytes->GetPointer(0)), count) != count ||
            !output.commit()) {
            output.cancelWriting();
            lastError_ = tr("PNG 文件写入失败：%1").arg(output.errorString()); return false;
        }
        lastError_.clear(); return true;
    } catch (const std::exception& exception) {
        lastError_ = tr("PNG 导出异常：%1").arg(QString::fromUtf8(exception.what()));
    } catch (...) { lastError_ = tr("PNG 导出发生未知异常。"); }
    return false;
}

bool VtkPostViewWidget::hasMesh() const { return currentGrid_ != nullptr; }
bool VtkPostViewWidget::scalarFieldVisible() const { return scalarFieldVisible_; }
bool VtkPostViewWidget::deformationEnabled() const { return deformationEnabled_; }
bool VtkPostViewWidget::legendVisible() const { return legendRequestedVisible_ && scalarFieldVisible_; }
double VtkPostViewWidget::deformationScale() const { return deformationScale_; }
QString VtkPostViewWidget::lastError() const { return lastError_; }
VtkMeshDisplayMode VtkPostViewWidget::displayMode() const { return displayMode_; }

void VtkPostViewWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (unitLabel_ == nullptr || vtkWidget_ == nullptr) return;
    unitLabel_->adjustSize(); const int margin = 12;
    unitLabel_->move(std::max(margin, vtkWidget_->width() - unitLabel_->width() - margin),
                     std::max(margin, vtkWidget_->height() - unitLabel_->height() - margin));
}

void VtkPostViewWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event); render();
    if (fitOnNextShow_) { fitOnNextShow_ = false; QTimer::singleShot(0, this, [this] { fitAll(); }); }
}

void VtkPostViewWidget::render() {
    if (renderWindow_ != nullptr) renderWindow_->Render();
}
