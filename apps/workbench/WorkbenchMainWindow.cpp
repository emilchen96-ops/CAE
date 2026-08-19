#include "WorkbenchMainWindow.hpp"

#include "OccViewWidget.hpp"
#include "PostFilterDialogs.hpp"
#include "GeometryImporter.hpp"
#include "GmshMesher.hpp"
#include "HmAsciiMeshIo.hpp"
#include "HistoryCurveWidget.hpp"
#include "ResultHistoryExtractor.hpp"
#include "DisplacementConstraintDialog.hpp"
#include "MaterialEditorDialog.hpp"
#include "NamedSelectionResolver.hpp"
#include "ResultControlWidget.hpp"
#include "ResultFieldProcessor.hpp"
#include "SectionAssignmentDialog.hpp"
#include "SolidSectionEditorDialog.hpp"
#include "SphExistingJobDialog.hpp"
#include "SphJobWriter.hpp"
#include "SphMultiBodyAnalysisDialog.hpp"
#include "SphSolverProcess.hpp"
#include "VtkResultSequenceLoader.hpp"
#include "VtkResultSequenceScanner.hpp"
#include "VtkPostViewWidget.hpp"

#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <BRep_Tool.hxx>
#include <TopoDS.hxx>

#include <QAction>
#include <QActionGroup>
#include <QAbstractItemView>
#include <QApplication>
#include <QColorDialog>
#include <QDockWidget>
#include <QEventLoop>
#include <QHeaderView>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QStringList>
#include <QTableView>
#include <QTimer>
#include <QToolBar>
#include <QTreeView>
#include <QVBoxLayout>

#include <vtkUnstructuredGrid.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>

namespace {

constexpr int GeometryObjectIdRole = Qt::UserRole + 1;
constexpr int MeshObjectIdRole = Qt::UserRole + 2;
constexpr int MaterialIdRole = Qt::UserRole + 3;
constexpr int SolidSectionIdRole = Qt::UserRole + 4;
constexpr int NamedSelectionIdRole = Qt::UserRole + 5;
constexpr int ConstraintIdRole = Qt::UserRole + 6;
constexpr int PostMeshObjectIdRole = Qt::UserRole + 7;
constexpr int ResultNodeKindRole = Qt::UserRole + 8;
constexpr int ResultFieldIndexRole = Qt::UserRole + 9;
constexpr int ResultOptionIndexRole = Qt::UserRole + 10;
constexpr int ResultFieldNameRole = Qt::UserRole + 11;
constexpr int ResultAssociationRole = Qt::UserRole + 12;
constexpr int ResultOperationRole = Qt::UserRole + 13;
constexpr int ResultComponentRole = Qt::UserRole + 14;
constexpr int ResultFrameNumberRole = Qt::UserRole + 15;
constexpr int ResultPartPathRole = Qt::UserRole + 16;
constexpr int ResultPartNameRole = Qt::UserRole + 17;
constexpr int ResultPartIdRole = Qt::UserRole + 18;
constexpr int FilterResultIdRole = Qt::UserRole + 19;

enum class ResultTreeNodeKind {
    None,
    File,
    Grid,
    Field,
    ScalarOption,
    Sequence,
    SequencePart,
    SequencePartFrame,
    FilterRoot,
    FilterObject
};

QString fromUtf8(const std::string& value) {
    return QString::fromUtf8(value.data(),
                             static_cast<qsizetype>(value.size()));
}

std::string toUtf8(const QString& value) {
    return value.toUtf8().toStdString();
}

std::vector<VtkPostViewPart> postViewParts(
    const VtkResultSequenceLoadResult& loadResult,
    const QHash<int, bool>& visibility) {
    std::vector<VtkPostViewPart> parts;
    parts.reserve(loadResult.parts.size());
    for (const auto& part : loadResult.parts) {
        parts.push_back({part.id, part.firstCell, part.cellCount,
                         visibility.value(part.id, true)});
    }
    return parts;
}

class WaitCursor final {
public:
    WaitCursor() { QApplication::setOverrideCursor(Qt::WaitCursor); }
    ~WaitCursor() { QApplication::restoreOverrideCursor(); }

    WaitCursor(const WaitCursor&) = delete;
    WaitCursor& operator=(const WaitCursor&) = delete;
};

double defaultMeshSize(const TopoDS_Shape& shape) {
    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    if (box.IsVoid()) {
        return 10.0;
    }
    Standard_Real xMin = 0.0;
    Standard_Real yMin = 0.0;
    Standard_Real zMin = 0.0;
    Standard_Real xMax = 0.0;
    Standard_Real yMax = 0.0;
    Standard_Real zMax = 0.0;
    box.Get(xMin, yMin, zMin, xMax, yMax, zMax);
    const double diagonal = std::hypot(
        std::hypot(xMax - xMin, yMax - yMin), zMax - zMin);
    return std::isfinite(diagonal) && diagonal > 0.0
        ? std::max(diagonal / 12.0, 1.0e-6)
        : 10.0;
}

QString geometryFormat(const QString& filePath) {
    const QString extension = QFileInfo(filePath).suffix().toLower();
    if (extension == QStringLiteral("step") ||
        extension == QStringLiteral("stp")) {
        return QStringLiteral("STEP");
    }
    if (extension == QStringLiteral("iges") ||
        extension == QStringLiteral("igs")) {
        return QStringLiteral("IGES");
    }
    if (extension == QStringLiteral("brep")) {
        return QStringLiteral("BREP");
    }
    return extension.toUpper();
}

QString selectionModeName(SelectionMode mode) {
    switch (mode) {
    case SelectionMode::Object:
        return QCoreApplication::translate("WorkbenchMainWindow", "对象");
    case SelectionMode::Vertex:
        return QCoreApplication::translate("WorkbenchMainWindow", "点");
    case SelectionMode::Edge:
        return QCoreApplication::translate("WorkbenchMainWindow", "边");
    case SelectionMode::Face:
        return QCoreApplication::translate("WorkbenchMainWindow", "面");
    case SelectionMode::Solid:
        return QCoreApplication::translate("WorkbenchMainWindow", "实体");
    }
    return {};
}

emilcae::core::NamedSelectionEntityType namedSelectionEntityType(
    SelectionMode mode) {
    using emilcae::core::NamedSelectionEntityType;
    switch (mode) {
    case SelectionMode::Object:
        return NamedSelectionEntityType::Object;
    case SelectionMode::Vertex:
        return NamedSelectionEntityType::Vertex;
    case SelectionMode::Edge:
        return NamedSelectionEntityType::Edge;
    case SelectionMode::Face:
        return NamedSelectionEntityType::Face;
    case SelectionMode::Solid:
        return NamedSelectionEntityType::Solid;
    }
    return NamedSelectionEntityType::Object;
}

QString namedSelectionEntityName(
    emilcae::core::NamedSelectionEntityType entityType) {
    using emilcae::core::NamedSelectionEntityType;
    switch (entityType) {
    case NamedSelectionEntityType::Object:
        return QCoreApplication::translate("WorkbenchMainWindow", "对象");
    case NamedSelectionEntityType::Vertex:
        return QCoreApplication::translate("WorkbenchMainWindow", "点");
    case NamedSelectionEntityType::Edge:
        return QCoreApplication::translate("WorkbenchMainWindow", "边");
    case NamedSelectionEntityType::Face:
        return QCoreApplication::translate("WorkbenchMainWindow", "面");
    case NamedSelectionEntityType::Solid:
        return QCoreApplication::translate("WorkbenchMainWindow", "实体");
    }
    return {};
}

QString constraintTypeName(emilcae::core::ConstraintType type) {
    return type == emilcae::core::ConstraintType::Fixed
        ? QCoreApplication::translate("WorkbenchMainWindow", "固定约束")
        : QCoreApplication::translate("WorkbenchMainWindow", "位移约束");
}

QString constraintValidityName(emilcae::core::ConstraintValidity validity) {
    using emilcae::core::ConstraintValidity;
    switch (validity) {
    case ConstraintValidity::Valid:
        return QCoreApplication::translate("WorkbenchMainWindow", "有效");
    case ConstraintValidity::PartiallyInvalid:
        return QCoreApplication::translate("WorkbenchMainWindow", "部分失效");
    case ConstraintValidity::Invalid:
        return QCoreApplication::translate("WorkbenchMainWindow", "失效");
    }
    return {};
}

QString resultAssociationName(ResultFieldAssociation association) {
    return association == ResultFieldAssociation::Point
        ? QCoreApplication::translate("WorkbenchMainWindow", "节点")
        : QCoreApplication::translate("WorkbenchMainWindow", "单元");
}

} // namespace

WorkbenchMainWindow::WorkbenchMainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle(tr("QTCAE 仿真工作台"));
    resize(1440, 900);

    createCentralWorkspace();
    namedSelectionManager_ =
        std::make_unique<emilcae::core::NamedSelectionManager>(
            [this](int objectId) {
                return occViewWidget_ != nullptr &&
                       occViewWidget_->findGeometryObject(objectId) != nullptr;
            },
            [this](emilcae::core::NamedSelectionId id) {
                return constraintManager_ != nullptr &&
                       constraintManager_->isNamedSelectionReferenced(id);
            });
    namedSelectionResolver_ = std::make_unique<NamedSelectionResolver>(
        [this](int objectId) {
            const GeometryObject* object = occViewWidget_ != nullptr
                ? occViewWidget_->findGeometryObject(objectId)
                : nullptr;
            return object != nullptr
                ? std::optional<NamedSelectionGeometry>(
                      NamedSelectionGeometry{object->shape, object->visible})
                : std::nullopt;
        });
    constraintManager_ = std::make_unique<
        emilcae::core::DisplacementConstraintManager>(
            *namedSelectionManager_);
    createDockWidgets();
    sphSolverProcess_ = new SphSolverProcess(this);
    createActions();
    createMenus();
    createToolBar();
    createStatusBar();

    switchToPreprocessing();
}

WorkbenchMainWindow::~WorkbenchMainWindow() = default;

void WorkbenchMainWindow::createActions() {
    newProjectAction_ = new QAction(tr("新建工程"), this);
    openProjectAction_ = new QAction(tr("打开工程"), this);
    saveProjectAction_ = new QAction(tr("保存工程"), this);
    exitAction_ = new QAction(tr("退出"), this);
    preprocessingAction_ = new QAction(tr("前处理"), this);
    postprocessingAction_ = new QAction(tr("后处理"), this);
    fitAllAction_ = new QAction(tr("适合窗口"), this);
    axonometricViewAction_ = new QAction(tr("轴测视图"), this);
    frontViewAction_ = new QAction(tr("前视图"), this);
    backViewAction_ = new QAction(tr("后视图"), this);
    leftViewAction_ = new QAction(tr("左视图"), this);
    rightViewAction_ = new QAction(tr("右视图"), this);
    topViewAction_ = new QAction(tr("顶视图"), this);
    bottomViewAction_ = new QAction(tr("底视图"), this);
    clearAllGeometryAction_ =
        new QAction(tr("清空所有几何对象"), this);
    generateMeshAction_ = new QAction(tr("生成四面体网格"), this);
    clearMeshAction_ = new QAction(tr("清除网格"), this);
    importHmAsciiAction_ = new QAction(tr("导入 HMASCII 网格"), this);
    exportHmAsciiAction_ = new QAction(tr("导出 HMASCII 网格"), this);
    runSphAnalysisAction_ = new QAction(tr("设置并运行 SPH 分析"), this);
    runExistingSphConfigurationAction_ =
        new QAction(tr("运行已有 SPH 配置"), this);
    stopSphAnalysisAction_ = new QAction(tr("停止当前 SPH 分析"), this);
    importVtkResultAction_ = new QAction(tr("导入 VTK 结果"), this);
    importVtkResultSequenceAction_ =
        new QAction(tr("导入 VTK 结果序列"), this);
    savePostViewImageAction_ =
        new QAction(tr("保存当前视图为图片"), this);
    postBackgroundColorAction_ =
        new QAction(tr("设置后处理背景颜色"), this);
    postPointSizeAction_ =
        new QAction(tr("设置点大小"), this);
    createClipResultAction_ = new QAction(tr("创建轴向剖切"), this);
    createSliceResultAction_ = new QAction(tr("创建平面切片"), this);
    createThresholdResultAction_ = new QAction(tr("创建阈值过滤"), this);
    pointToCellResultAction_ = new QAction(tr("节点数据 → 单元数据"), this);
    cellToPointResultAction_ = new QAction(tr("单元数据 → 节点数据"), this);
    exportResultAnimationAction_ = new QAction(tr("导出结果动画"), this);
    extractNodeHistoryAction_ = new QAction(tr("提取节点时间历程"), this);
    pointsAction_ = new QAction(tr("点"), this);
    surfaceWithEdgesAction_ = new QAction(tr("表面加边线"), this);
    surfaceOnlyAction_ = new QAction(tr("仅表面"), this);
    wireframeAction_ = new QAction(tr("线框"), this);
    newMaterialAction_ = new QAction(tr("新建材料"), this);
    steelMaterialAction_ = new QAction(tr("从结构钢模板创建"), this);
    aluminumMaterialAction_ = new QAction(tr("从铝合金模板创建"), this);
    editMaterialAction_ = new QAction(tr("编辑材料"), this);
    renameMaterialAction_ = new QAction(tr("重命名材料"), this);
    duplicateMaterialAction_ = new QAction(tr("复制材料"), this);
    deleteMaterialAction_ = new QAction(tr("删除材料"), this);
    newSectionAction_ = new QAction(tr("新建实体截面"), this);
    editSectionAction_ = new QAction(tr("编辑实体截面"), this);
    renameSectionAction_ = new QAction(tr("重命名实体截面"), this);
    duplicateSectionAction_ = new QAction(tr("复制实体截面"), this);
    deleteSectionAction_ = new QAction(tr("删除实体截面"), this);
    assignSectionAction_ = new QAction(tr("指派实体截面"), this);
    unassignSectionAction_ = new QAction(tr("取消截面指派"), this);
    createNamedSelectionAction_ =
        new QAction(tr("创建命名选择集"), this);
    clearCurrentSelectionAction_ =
        new QAction(tr("清除当前选择"), this);
    newFixedConstraintAction_ = new QAction(tr("新建固定约束"), this);
    newDisplacementConstraintAction_ =
        new QAction(tr("新建位移约束"), this);
    objectSelectionAction_ = new QAction(tr("对象"), this);
    vertexSelectionAction_ = new QAction(tr("点"), this);
    edgeSelectionAction_ = new QAction(tr("边"), this);
    faceSelectionAction_ = new QAction(tr("面"), this);
    solidSelectionAction_ = new QAction(tr("实体"), this);
    aboutAction_ = new QAction(tr("关于 QTCAE"), this);
    aboutQtAction_ = new QAction(tr("关于 Qt"), this);

    newProjectAction_->setEnabled(false);
    openProjectAction_->setEnabled(true);
    saveProjectAction_->setEnabled(false);
    clearAllGeometryAction_->setEnabled(false);
    stopSphAnalysisAction_->setEnabled(false);

    preprocessingAction_->setCheckable(true);
    postprocessingAction_->setCheckable(true);
    workspaceActionGroup_ = new QActionGroup(this);
    workspaceActionGroup_->setExclusive(true);
    workspaceActionGroup_->addAction(preprocessingAction_);
    workspaceActionGroup_->addAction(postprocessingAction_);

    selectionActionGroup_ = new QActionGroup(this);
    selectionActionGroup_->setExclusive(true);
    for (QAction* action : {objectSelectionAction_, vertexSelectionAction_,
                            edgeSelectionAction_, faceSelectionAction_,
                            solidSelectionAction_}) {
        action->setCheckable(true);
        selectionActionGroup_->addAction(action);
    }
    objectSelectionAction_->setChecked(true);

    postDisplayActionGroup_ = new QActionGroup(this);
    postDisplayActionGroup_->setExclusive(true);
    for (QAction* action : {pointsAction_, wireframeAction_,
                            surfaceOnlyAction_, surfaceWithEdgesAction_}) {
        action->setCheckable(true);
        postDisplayActionGroup_->addAction(action);
    }
    surfaceWithEdgesAction_->setChecked(true);

    connect(exitAction_, &QAction::triggered, this, &QWidget::close);
    connect(openProjectAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::openGeometryFile);
    connect(clearAllGeometryAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::clearAllGeometryObjects);
    connect(generateMeshAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::generateTetrahedralMesh);
    connect(clearMeshAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::clearSelectedMesh);
    connect(importHmAsciiAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::importHmAsciiMesh);
    connect(exportHmAsciiAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::exportHmAsciiMesh);
    connect(runSphAnalysisAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::runSphAnalysis);
    connect(runExistingSphConfigurationAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::runExistingSphConfiguration);
    connect(stopSphAnalysisAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::stopSphAnalysis);
    connect(sphSolverProcess_, &SphSolverProcess::logLine,
            this, [this](const QString& line) {
                messageLog_->appendPlainText(
                    tr("[SPH] %1").arg(line));
            });
    connect(sphSolverProcess_, &SphSolverProcess::progressUpdated,
            this, [this](qint64 step, double time, double timeStep) {
                if (taskModel_ == nullptr || sphTaskRow_ < 0 ||
                    sphTaskRow_ >= taskModel_->rowCount()) {
                    return;
                }
                const double ratio = currentSphSimulationTime_ > 0.0
                    ? std::clamp(time / currentSphSimulationTime_, 0.0, 1.0)
                    : 0.0;
                taskModel_->item(sphTaskRow_, 2)->setText(tr("计算中"));
                if (currentSphSimulationTime_ > 0.0) {
                    taskModel_->item(sphTaskRow_, 3)->setText(
                        tr("%1% · 步 %2 · %3 μs · Δt %4 ns")
                            .arg(QString::number(ratio * 100.0, 'f', 1))
                            .arg(step)
                            .arg(QString::number(time * 1.0e6, 'g', 7))
                            .arg(QString::number(timeStep * 1.0e9, 'g', 5)));
                    statusBar()->showMessage(
                        tr("SPH 正在计算：%1%，步骤 %2")
                            .arg(QString::number(ratio * 100.0, 'f', 1))
                            .arg(step));
                } else {
                    taskModel_->item(sphTaskRow_, 3)->setText(
                        tr("步 %1 · %2 μs · Δt %3 ns")
                            .arg(step)
                            .arg(QString::number(time * 1.0e6, 'g', 7))
                            .arg(QString::number(timeStep * 1.0e9, 'g', 5)));
                    statusBar()->showMessage(
                        tr("SPH 正在计算：步骤 %1").arg(step));
                }
            });
    connect(sphSolverProcess_, &SphSolverProcess::runFinished,
            this, &WorkbenchMainWindow::handleSphRunFinished);
    connect(importVtkResultAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::importVtkResult);
    connect(importVtkResultSequenceAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::importVtkResultSequence);
    connect(savePostViewImageAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::savePostViewImage);
    connect(postBackgroundColorAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::choosePostBackgroundColor);
    connect(postPointSizeAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::choosePostPointSize);
    connect(createClipResultAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::createClipResult);
    connect(createSliceResultAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::createSliceResult);
    connect(createThresholdResultAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::createThresholdResult);
    connect(pointToCellResultAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::createPointToCellResult);
    connect(cellToPointResultAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::createCellToPointResult);
    connect(exportResultAnimationAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::exportResultAnimation);
    connect(extractNodeHistoryAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::extractNodeHistory);
    connect(pointsAction_, &QAction::triggered, this, [this] {
        setPostDisplayMode(VtkMeshDisplayMode::Points);
    });
    connect(surfaceWithEdgesAction_, &QAction::triggered, this, [this] {
        setPostDisplayMode(VtkMeshDisplayMode::SurfaceWithEdges);
    });
    connect(surfaceOnlyAction_, &QAction::triggered, this, [this] {
        setPostDisplayMode(VtkMeshDisplayMode::SurfaceOnly);
    });
    connect(wireframeAction_, &QAction::triggered, this, [this] {
        setPostDisplayMode(VtkMeshDisplayMode::Wireframe);
    });
    connect(newMaterialAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::createBlankMaterial);
    connect(steelMaterialAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::createSteelMaterial);
    connect(aluminumMaterialAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::createAluminumMaterial);
    connect(editMaterialAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::editSelectedMaterial);
    connect(renameMaterialAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::renameSelectedMaterial);
    connect(duplicateMaterialAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::duplicateSelectedMaterial);
    connect(deleteMaterialAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::deleteSelectedMaterial);
    connect(newSectionAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::createSolidSection);
    connect(editSectionAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::editSelectedSection);
    connect(renameSectionAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::renameSelectedSection);
    connect(duplicateSectionAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::duplicateSelectedSection);
    connect(deleteSectionAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::deleteSelectedSection);
    connect(assignSectionAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::assignSectionToSelectedObject);
    connect(unassignSectionAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::unassignSectionFromSelectedObject);
    connect(createNamedSelectionAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::createNamedSelection);
    connect(clearCurrentSelectionAction_, &QAction::triggered,
            occViewWidget_, &OccViewWidget::clearSelection);
    connect(newFixedConstraintAction_, &QAction::triggered,
            this, [this] { createFixedConstraint(); });
    connect(newDisplacementConstraintAction_, &QAction::triggered,
            this, [this] { createDisplacementConstraint(); });
    connect(objectSelectionAction_, &QAction::triggered, this, [this] {
        switchSelectionMode(SelectionMode::Object, tr("对象"));
    });
    connect(vertexSelectionAction_, &QAction::triggered, this, [this] {
        switchSelectionMode(SelectionMode::Vertex, tr("点"));
    });
    connect(edgeSelectionAction_, &QAction::triggered, this, [this] {
        switchSelectionMode(SelectionMode::Edge, tr("边"));
    });
    connect(faceSelectionAction_, &QAction::triggered, this, [this] {
        switchSelectionMode(SelectionMode::Face, tr("面"));
    });
    connect(solidSelectionAction_, &QAction::triggered, this, [this] {
        switchSelectionMode(SelectionMode::Solid, tr("实体"));
    });
    connect(occViewWidget_, &OccViewWidget::selectionChanged,
            this, &WorkbenchMainWindow::handleViewSelectionChanged);
    connect(preprocessingAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::switchToPreprocessing);
    connect(postprocessingAction_, &QAction::triggered,
            this, &WorkbenchMainWindow::switchToPostprocessing);
    connect(fitAllAction_, &QAction::triggered, this, [this] {
        if (workspaceStack_->currentIndex() == 1) {
            vtkPostViewWidget_->fitAll();
        } else {
            occViewWidget_->fitAll();
        }
        statusBar()->showMessage(tr("已执行适合窗口"), 3000);
    });
    connect(axonometricViewAction_, &QAction::triggered, this, [this] {
        if (workspaceStack_->currentIndex() == 1)
            vtkPostViewWidget_->setAxonometricView();
        else
            occViewWidget_->setAxonometricView();
        statusBar()->showMessage(tr("已切换到轴测视图"), 3000);
    });
    connect(frontViewAction_, &QAction::triggered, this, [this] {
        if (workspaceStack_->currentIndex() == 1)
            vtkPostViewWidget_->setFrontView();
        else
            occViewWidget_->setFrontView();
        statusBar()->showMessage(tr("已切换到前视图"), 3000);
    });
    connect(backViewAction_, &QAction::triggered, this, [this] {
        if (workspaceStack_->currentIndex() == 1)
            vtkPostViewWidget_->setBackView();
        else
            occViewWidget_->setBackView();
        statusBar()->showMessage(tr("已切换到后视图"), 3000);
    });
    connect(leftViewAction_, &QAction::triggered, this, [this] {
        if (workspaceStack_->currentIndex() == 1)
            vtkPostViewWidget_->setLeftView();
        else
            occViewWidget_->setLeftView();
        statusBar()->showMessage(tr("已切换到左视图"), 3000);
    });
    connect(rightViewAction_, &QAction::triggered, this, [this] {
        if (workspaceStack_->currentIndex() == 1)
            vtkPostViewWidget_->setRightView();
        else
            occViewWidget_->setRightView();
        statusBar()->showMessage(tr("已切换到右视图"), 3000);
    });
    connect(topViewAction_, &QAction::triggered, this, [this] {
        if (workspaceStack_->currentIndex() == 1)
            vtkPostViewWidget_->setTopView();
        else
            occViewWidget_->setTopView();
        statusBar()->showMessage(tr("已切换到顶视图"), 3000);
    });
    connect(bottomViewAction_, &QAction::triggered, this, [this] {
        if (workspaceStack_->currentIndex() == 1)
            vtkPostViewWidget_->setBottomView();
        else
            occViewWidget_->setBottomView();
        statusBar()->showMessage(tr("已切换到底视图"), 3000);
    });
    connect(aboutAction_, &QAction::triggered, this, [this] {
        QMessageBox::about(
            this,
            tr("关于 QTCAE"),
            tr("QTCAE\n\n计算机辅助工程仿真工作台\n\n当前状态：开发预览版\nSPH 求解流程已接入并处于验证阶段"));
    });
    connect(aboutQtAction_, &QAction::triggered, qApp, &QApplication::aboutQt);
    updateMaterialActionStates();
    updateSectionActionStates();
    updatePostViewActionStates();
}

void WorkbenchMainWindow::createMenus() {
    auto* fileMenu = menuBar()->addMenu(tr("文件"));
    fileMenu->addAction(newProjectAction_);
    fileMenu->addAction(openProjectAction_);
    fileMenu->addAction(saveProjectAction_);
    fileMenu->addSeparator();
    fileMenu->addAction(exitAction_);

    auto* viewMenu = menuBar()->addMenu(tr("视图"));
    viewMenu->addAction(projectDock_->toggleViewAction());
    viewMenu->addAction(propertiesDock_->toggleViewAction());
    viewMenu->addAction(messageLogDock_->toggleViewAction());
    viewMenu->addAction(taskMonitorDock_->toggleViewAction());
    viewMenu->addAction(resultControlDock_->toggleViewAction());
    viewMenu->addAction(historyDock_->toggleViewAction());
    viewMenu->addSeparator();
    auto* standardViewMenu = viewMenu->addMenu(tr("标准视图"));
    standardViewMenu->addAction(axonometricViewAction_);
    standardViewMenu->addAction(frontViewAction_);
    standardViewMenu->addAction(backViewAction_);
    standardViewMenu->addAction(leftViewAction_);
    standardViewMenu->addAction(rightViewAction_);
    standardViewMenu->addAction(topViewAction_);
    standardViewMenu->addAction(bottomViewAction_);
    standardViewMenu->addSeparator();
    standardViewMenu->addAction(fitAllAction_);
    auto* postDisplayMenu = viewMenu->addMenu(tr("后处理显示"));
    postDisplayMenu->addAction(pointsAction_);
    postDisplayMenu->addAction(wireframeAction_);
    postDisplayMenu->addAction(surfaceOnlyAction_);
    postDisplayMenu->addAction(surfaceWithEdgesAction_);

    auto* workspaceMenu = menuBar()->addMenu(tr("工作区"));
    workspaceMenu->addAction(preprocessingAction_);
    workspaceMenu->addAction(postprocessingAction_);

    auto* modelMenu = menuBar()->addMenu(tr("模型"));
    modelMenu->addAction(clearAllGeometryAction_);

    auto* materialMenu = menuBar()->addMenu(tr("材料"));
    materialMenu->addAction(newMaterialAction_);
    materialMenu->addAction(steelMaterialAction_);
    materialMenu->addAction(aluminumMaterialAction_);
    materialMenu->addSeparator();
    materialMenu->addAction(editMaterialAction_);
    materialMenu->addAction(renameMaterialAction_);
    materialMenu->addAction(duplicateMaterialAction_);
    materialMenu->addAction(deleteMaterialAction_);

    auto* sectionMenu = menuBar()->addMenu(tr("截面"));
    sectionMenu->addAction(newSectionAction_);
    sectionMenu->addSeparator();
    sectionMenu->addAction(assignSectionAction_);
    sectionMenu->addAction(unassignSectionAction_);

    auto* selectionMenu = menuBar()->addMenu(tr("选择"));
    selectionMenu->addAction(objectSelectionAction_);
    selectionMenu->addAction(vertexSelectionAction_);
    selectionMenu->addAction(edgeSelectionAction_);
    selectionMenu->addAction(faceSelectionAction_);
    selectionMenu->addAction(solidSelectionAction_);
    selectionMenu->addSeparator();
    selectionMenu->addAction(createNamedSelectionAction_);
    selectionMenu->addAction(clearCurrentSelectionAction_);

    auto* meshMenu = menuBar()->addMenu(tr("网格"));
    meshMenu->addAction(generateMeshAction_);
    meshMenu->addAction(clearMeshAction_);
    auto* displayInPostprocessingAction =
        meshMenu->addAction(tr("在后处理中显示"));
    connect(displayInPostprocessingAction, &QAction::triggered,
            this, [this] { displayMeshInPostprocessing(); });
    meshMenu->addSeparator();
    meshMenu->addAction(importHmAsciiAction_);
    meshMenu->addAction(exportHmAsciiAction_);

    auto* analysisMenu = menuBar()->addMenu(tr("分析"));
    analysisMenu->addAction(runSphAnalysisAction_);
    analysisMenu->addAction(runExistingSphConfigurationAction_);
    analysisMenu->addAction(stopSphAnalysisAction_);

    auto* resultMenu = menuBar()->addMenu(tr("结果"));
    resultMenu->addAction(importVtkResultAction_);
    resultMenu->addAction(importVtkResultSequenceAction_);
    resultMenu->addSeparator();
    resultMenu->addAction(savePostViewImageAction_);
    resultMenu->addAction(postBackgroundColorAction_);
    resultMenu->addAction(postPointSizeAction_);
    resultMenu->addSeparator();
    resultMenu->addAction(createClipResultAction_);
    resultMenu->addAction(createSliceResultAction_);
    resultMenu->addAction(createThresholdResultAction_);
    resultMenu->addSeparator();
    resultMenu->addAction(pointToCellResultAction_);
    resultMenu->addAction(cellToPointResultAction_);
    resultMenu->addSeparator();
    resultMenu->addAction(exportResultAnimationAction_);
    resultMenu->addAction(extractNodeHistoryAction_);

    auto* helpMenu = menuBar()->addMenu(tr("帮助"));
    helpMenu->addAction(aboutAction_);
    helpMenu->addAction(aboutQtAction_);
}

void WorkbenchMainWindow::createToolBar() {
    auto* toolBar = addToolBar(tr("主工具栏"));
    toolBar->setObjectName("MainToolBar");
    auto* newToolAction = toolBar->addAction(tr("新建"));
    auto* openToolAction = toolBar->addAction(tr("打开"));
    auto* saveToolAction = toolBar->addAction(tr("保存"));
    newToolAction->setEnabled(false);
    connect(openToolAction, &QAction::triggered, this, [this] {
        if (workspaceStack_->currentIndex() == 1) {
            importVtkResult();
        } else {
            openGeometryFile();
        }
    });
    saveToolAction->setEnabled(false);
    toolBar->addSeparator();
    toolBar->addAction(preprocessingAction_);
    toolBar->addAction(postprocessingAction_);
    toolBar->addSeparator();
    toolBar->addAction(fitAllAction_);
    axonometricToolAction_ = toolBar->addAction(tr("轴测"));
    frontToolAction_ = toolBar->addAction(tr("前视"));
    topToolAction_ = toolBar->addAction(tr("顶视"));
    rightToolAction_ = toolBar->addAction(tr("右视"));
    connect(axonometricToolAction_, &QAction::triggered,
            axonometricViewAction_, &QAction::trigger);
    connect(frontToolAction_, &QAction::triggered,
            frontViewAction_, &QAction::trigger);
    connect(topToolAction_, &QAction::triggered,
            topViewAction_, &QAction::trigger);
    connect(rightToolAction_, &QAction::triggered,
            rightViewAction_, &QAction::trigger);
    toolBar->addSeparator();
    toolBar->addAction(objectSelectionAction_);
    toolBar->addAction(vertexSelectionAction_);
    toolBar->addAction(edgeSelectionAction_);
    toolBar->addAction(faceSelectionAction_);
    toolBar->addAction(solidSelectionAction_);
    toolBar->addSeparator();
    toolBar->addAction(generateMeshAction_);
    toolBar->addAction(clearMeshAction_);
    toolBar->addAction(runSphAnalysisAction_);
    toolBar->addSeparator();
    toolBar->addAction(pointsAction_);
    toolBar->addAction(wireframeAction_);
    toolBar->addAction(surfaceWithEdgesAction_);
    toolBar->addAction(surfaceOnlyAction_);
}

void WorkbenchMainWindow::createDockWidgets() {
    projectDock_ = new QDockWidget(tr("工程树"), this);
    projectDock_->setObjectName("ProjectDock");
    projectTree_ = new QTreeView(projectDock_);
    projectModel_ = new QStandardItemModel(projectTree_);
    projectModel_->setHorizontalHeaderLabels({tr("工程")});
    auto* projectRoot = new QStandardItem(tr("工程"));
    auto* modelRoot = new QStandardItem(tr("模型"));
    geometryRootItem_ = new QStandardItem(tr("几何"));
    meshRootItem_ = new QStandardItem(tr("网格"));
    namedSelectionRootItem_ = new QStandardItem(tr("命名选择集"));
    modelRoot->appendRow(geometryRootItem_);
    modelRoot->appendRow(meshRootItem_);
    modelRoot->appendRow(namedSelectionRootItem_);
    projectRoot->appendRow(modelRoot);
    materialRootItem_ = new QStandardItem(tr("材料"));
    projectRoot->appendRow(materialRootItem_);
    sectionRootItem_ = new QStandardItem(tr("截面"));
    projectRoot->appendRow(sectionRootItem_);
    analysisRootItem_ = new QStandardItem(tr("分析"));
    boundaryConditionRootItem_ = new QStandardItem(tr("边界条件"));
    analysisRootItem_->appendRow(boundaryConditionRootItem_);
    projectRoot->appendRow(analysisRootItem_);
    resultRootItem_ = new QStandardItem(tr("结果"));
    currentPostMeshRootItem_ = new QStandardItem(tr("当前网格"));
    resultRootItem_->appendRow(currentPostMeshRootItem_);
    projectRoot->appendRow(resultRootItem_);
    projectModel_->appendRow(projectRoot);
    projectTree_->setModel(projectModel_);
    projectTree_->expandAll();
    projectTree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    projectTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(projectModel_, &QStandardItemModel::itemChanged,
            this, &WorkbenchMainWindow::handleProjectItemChanged);
    connect(projectTree_, &QTreeView::clicked,
            this, &WorkbenchMainWindow::handleProjectItemClicked);
    connect(projectTree_, &QTreeView::doubleClicked,
            this, &WorkbenchMainWindow::handleProjectItemDoubleClicked);
    connect(projectTree_, &QWidget::customContextMenuRequested,
            this, &WorkbenchMainWindow::showProjectContextMenu);
    projectDock_->setWidget(projectTree_);
    addDockWidget(Qt::LeftDockWidgetArea, projectDock_);

    propertiesDock_ = new QDockWidget(tr("属性"), this);
    propertiesDock_->setObjectName("PropertiesDock");
    auto* propertiesTable = new QTableView(propertiesDock_);
    propertiesModel_ = new QStandardItemModel(0, 2, propertiesTable);
    propertiesModel_->setHorizontalHeaderLabels({tr("属性"), tr("数值")});
    propertiesTable->setModel(propertiesModel_);
    propertiesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    propertiesTable->horizontalHeader()->setStretchLastSection(true);
    propertiesTable->verticalHeader()->setVisible(false);
    propertiesDock_->setWidget(propertiesTable);
    addDockWidget(Qt::RightDockWidgetArea, propertiesDock_);
    showDefaultProperties();

    messageLogDock_ = new QDockWidget(tr("消息日志"), this);
    messageLogDock_->setObjectName("MessageLogDock");
    messageLog_ = new QPlainTextEdit(messageLogDock_);
    messageLog_->setReadOnly(true);
    messageLog_->setPlainText(tr("QTCAE 工作台已初始化。\n当前未打开工程。"));
    messageLogDock_->setWidget(messageLog_);
    addDockWidget(Qt::BottomDockWidgetArea, messageLogDock_);

    taskMonitorDock_ = new QDockWidget(tr("任务监控"), this);
    taskMonitorDock_->setObjectName("TaskMonitorDock");
    auto* taskTable = new QTableView(taskMonitorDock_);
    taskModel_ = new QStandardItemModel(0, 4, taskTable);
    taskModel_->setHorizontalHeaderLabels(
        {tr("任务"), tr("求解器"), tr("状态"), tr("进度")});
    taskTable->setModel(taskModel_);
    taskTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    taskTable->horizontalHeader()->setStretchLastSection(true);
    taskTable->verticalHeader()->setVisible(false);
    taskMonitorDock_->setWidget(taskTable);
    addDockWidget(Qt::BottomDockWidgetArea, taskMonitorDock_);

    resultControlDock_ = new QDockWidget(tr("结果控制"), this);
    resultControlDock_->setObjectName("ResultControlDock");
    resultControlWidget_ =
        new ResultControlWidget(resultControlDock_);
    resultAnimationTimer_ = new QTimer(this);
    resultAnimationTimer_->setTimerType(Qt::PreciseTimer);
    resultControlDock_->setWidget(resultControlWidget_);
    addDockWidget(Qt::RightDockWidgetArea, resultControlDock_);
    tabifyDockWidget(propertiesDock_, resultControlDock_);
    propertiesDock_->raise();

    historyDock_ = new QDockWidget(tr("时间历程"), this);
    historyDock_->setObjectName("HistoryDock");
    auto* historyContainer = new QWidget(historyDock_);
    auto* historyLayout = new QVBoxLayout(historyContainer);
    historyLayout->setContentsMargins(4, 4, 4, 4);
    historyCurveWidget_ = new HistoryCurveWidget(historyContainer);
    auto* clearHistoryButton = new QPushButton(tr("清除曲线"), historyContainer);
    historyLayout->addWidget(historyCurveWidget_, 1);
    historyLayout->addWidget(clearHistoryButton);
    historyDock_->setWidget(historyContainer);
    addDockWidget(Qt::BottomDockWidgetArea, historyDock_);
    historyDock_->hide();
    connect(clearHistoryButton, &QPushButton::clicked,
            historyCurveWidget_, &HistoryCurveWidget::clearCurve);

    connect(resultControlWidget_,
            &ResultControlWidget::frameChangeRequested,
            this, [this](int frameNumber) {
                loadVtkResultSequenceFrame(frameNumber);
            });
    connect(resultControlWidget_, &ResultControlWidget::playRequested,
            this, &WorkbenchMainWindow::startResultAnimation);
    connect(resultControlWidget_, &ResultControlWidget::pauseRequested,
            this, &WorkbenchMainWindow::pauseResultAnimation);
    connect(resultControlWidget_, &ResultControlWidget::stopRequested,
            this, &WorkbenchMainWindow::stopResultAnimation);
    connect(resultAnimationTimer_, &QTimer::timeout,
            this, &WorkbenchMainWindow::advanceResultAnimation);
    connect(resultControlWidget_,
            &ResultControlWidget::scalarSelectionChanged,
            this, [this] {
                if (resultControlWidget_->scalarVisible()) {
                    applySelectedResultScalar();
                }
            });
    connect(resultControlWidget_,
            &ResultControlWidget::scalarVisibilityChanged,
            this, [this](bool visible) {
                if (visible) {
                    applySelectedResultScalar();
                } else {
                    vtkPostViewWidget_->clearScalarField();
                    currentResultScalarOption_.reset();
                    currentResultStatistics_.reset();
                    showLoadedResultProperties();
                }
            });
    connect(resultControlWidget_,
            &ResultControlWidget::displacementFieldChanged,
            this, &WorkbenchMainWindow::updateSelectedDisplacementField);
    connect(resultControlWidget_,
            &ResultControlWidget::deformationVisibilityChanged,
            this, &WorkbenchMainWindow::setResultDeformationVisible);
    connect(resultControlWidget_,
            &ResultControlWidget::deformationScaleChanged,
            this, &WorkbenchMainWindow::setResultDeformationScale);
    connect(resultControlWidget_,
            &ResultControlWidget::legendVisibilityChanged,
            this, [this](bool visible) {
                vtkPostViewWidget_->setLegendVisible(visible);
                showCurrentResultScalarProperties();
            });
    connect(resultControlWidget_,
            &ResultControlWidget::scalarRangeChangeRequested,
            this, [this] { applySelectedScalarRange(true); });

    tabifyDockWidget(messageLogDock_, taskMonitorDock_);
    messageLogDock_->raise();
}

void WorkbenchMainWindow::createCentralWorkspace() {
    workspaceStack_ = new QStackedWidget(this);

    occViewWidget_ = new OccViewWidget(workspaceStack_);
    vtkPostViewWidget_ = new VtkPostViewWidget(workspaceStack_);
    workspaceStack_->addWidget(occViewWidget_);
    workspaceStack_->addWidget(vtkPostViewWidget_);
    setCentralWidget(workspaceStack_);
}

void WorkbenchMainWindow::createStatusBar() {
    statusBar()->showMessage(tr("就绪"));
}

void WorkbenchMainWindow::switchToPreprocessing() {
    workspaceStack_->setCurrentIndex(0);
    preprocessingAction_->setChecked(true);
    updateWorkspaceProperty(tr("前处理"));
    setViewActionsEnabled(true);
    setSelectionActionsEnabled(true);
    updatePostViewActionStates();
    statusBar()->showMessage(tr("已切换到前处理工作区"), 3000);
}

void WorkbenchMainWindow::switchToPostprocessing() {
    workspaceStack_->setCurrentIndex(1);
    postprocessingAction_->setChecked(true);
    updateWorkspaceProperty(tr("后处理"));
    setViewActionsEnabled(false);
    fitAllAction_->setEnabled(true);
    setSelectionActionsEnabled(false);
    updatePostViewActionStates();
    statusBar()->showMessage(tr("已切换到后处理工作区"), 3000);
}

void WorkbenchMainWindow::openGeometryFile() {
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("打开几何文件"),
        {},
        tr("几何文件 (*.step *.stp *.iges *.igs *.brep);;"
           "STEP 文件 (*.step *.stp);;"
           "IGES 文件 (*.iges *.igs);;"
           "BREP 文件 (*.brep);;"
           "所有文件 (*.*)"));
    if (filePath.isEmpty()) {
        return;
    }

    const GeometryImporter importer;
    const GeometryImporter::ImportResult result = [&] {
        const WaitCursor waitCursor;
        return importer.importFile(filePath);
    }();
    if (!result.success) {
        const QString reason = result.errorMessage.isEmpty()
            ? tr("未知错误")
            : result.errorMessage;
        statusBar()->showMessage(tr("几何文件导入失败"), 5000);
        messageLog_->appendPlainText(
            tr("几何文件导入失败：%1\n原因：%2").arg(filePath, reason));
        QMessageBox::critical(this, tr("导入失败"), reason);
        return;
    }

    const QFileInfo fileInfo(filePath);
    const int objectId = occViewWidget_->addGeometryObject(
        fileInfo.fileName(), fileInfo.absoluteFilePath(), result.shape);
    if (objectId < 0) {
        const QString reason = tr("几何体显示失败，现有模型已保留。");
        statusBar()->showMessage(tr("几何文件导入失败"), 5000);
        messageLog_->appendPlainText(
            tr("几何文件导入失败：%1\n原因：%2").arg(filePath, reason));
        QMessageBox::critical(this, tr("导入失败"), reason);
        return;
    }

    addGeometryTreeItem(objectId, fileInfo.fileName());
    occViewWidget_->fitAll();
    switchToPreprocessing();
    setWindowTitle(tr("QTCAE 仿真工作台 - %1")
                       .arg(fileInfo.fileName()));
    clearAllGeometryAction_->setEnabled(true);
    statusBar()->showMessage(tr("几何文件导入成功"), 5000);
    messageLog_->appendPlainText(tr("已导入几何文件：%1").arg(filePath));
}

void WorkbenchMainWindow::addGeometryTreeItem(int objectId,
                                              const QString& name) {
    auto* item = new QStandardItem(name);
    item->setData(objectId, GeometryObjectIdRole);
    item->setCheckable(true);
    item->setCheckState(Qt::Checked);
    item->setEditable(false);
    geometryRootItem_->appendRow(item);
    geometryItems_.insert(objectId, item);
    projectTree_->expand(geometryRootItem_->index());
    projectTree_->setCurrentIndex(item->index());
    selectedMaterialId_ = -1;
    selectedSectionId_ = -1;
    selectedNamedSelectionId_ = -1;
    selectedConstraintId_ = -1;
    selectedMeshObjectId_ = -1;
    selectedGeometryObjectId_ = objectId;
    updateMaterialActionStates();
    updateSectionActionStates();
    showGeometryObjectProperties(objectId);
}

void WorkbenchMainWindow::handleProjectItemChanged(QStandardItem* item) {
    const auto resultKind = item == nullptr
        ? ResultTreeNodeKind::None
        : static_cast<ResultTreeNodeKind>(
              item->data(ResultNodeKindRole).toInt());
    if (resultKind == ResultTreeNodeKind::SequencePart &&
        item->isCheckable()) {
        setVtkResultPartVisible(
            item, item->checkState() == Qt::Checked);
        return;
    }
    if (resultKind == ResultTreeNodeKind::FilterObject &&
        item->isCheckable()) {
        setFilterResultVisible(item, item->checkState() == Qt::Checked);
        return;
    }
    const int currentMeshId = meshObjectId(item);
    if (currentMeshId >= 0 && item->isCheckable()) {
        const MeshObject* mesh = occViewWidget_->findMesh(currentMeshId);
        const bool requestedVisibility = item->checkState() == Qt::Checked;
        if (mesh != nullptr && mesh->visible != requestedVisibility) {
            setMeshVisible(item, requestedVisibility);
        }
        return;
    }
    const int objectId = geometryObjectId(item);
    if (objectId < 0 || !item->isCheckable()) {
        return;
    }
    const bool requestedVisibility = item->checkState() == Qt::Checked;
    const GeometryObject* object =
        occViewWidget_->findGeometryObject(objectId);
    if (object != nullptr && object->visible != requestedVisibility) {
        setGeometryObjectVisible(item, requestedVisibility);
    }
}

void WorkbenchMainWindow::handleProjectItemClicked(
    const QModelIndex& index) {
    QStandardItem* item = projectModel_->itemFromIndex(index);
    const auto resultKind = item == nullptr
        ? ResultTreeNodeKind::None
        : static_cast<ResultTreeNodeKind>(
              item->data(ResultNodeKindRole).toInt());
    if (resultKind == ResultTreeNodeKind::File) {
        showLoadedResultProperties();
        return;
    }
    if (resultKind == ResultTreeNodeKind::Sequence) {
        showVtkResultSequenceProperties();
        return;
    }
    if (resultKind == ResultTreeNodeKind::SequencePart) {
        showVtkResultSequencePartProperties(
            item->data(ResultPartIdRole).toInt());
        return;
    }
    if (resultKind == ResultTreeNodeKind::SequencePartFrame) {
        showVtkResultSequencePartFrameProperties(
            item->data(ResultFrameNumberRole).toInt(),
            item->data(ResultPartNameRole).toString(),
            item->data(ResultPartPathRole).toString());
        return;
    }
    if (resultKind == ResultTreeNodeKind::FilterObject) {
        showFilterResultProperties(item->data(FilterResultIdRole).toInt());
        return;
    }
    if (resultKind == ResultTreeNodeKind::Grid) {
        showResultGridProperties();
        return;
    }
    if (resultKind == ResultTreeNodeKind::Field ||
        resultKind == ResultTreeNodeKind::ScalarOption) {
        showResultFieldProperties(
            item->data(ResultFieldIndexRole).toInt());
        return;
    }
    const int currentPostMeshId = postMeshObjectId(item);
    if (currentPostMeshId >= 0) {
        selectedGeometryObjectId_ = -1;
        selectedMeshObjectId_ = -1;
        selectedMaterialId_ = -1;
        selectedSectionId_ = -1;
        selectedNamedSelectionId_ = -1;
        selectedConstraintId_ = -1;
        occViewWidget_->clearSelection();
        showPostprocessingMeshProperties(currentPostMeshId);
        if (const MeshObject* mesh =
                occViewWidget_->findMesh(currentPostMeshId)) {
            statusBar()->showMessage(
                tr("后处理：%1").arg(mesh->name), 3000);
        }
        return;
    }
    const int currentConstraintId = constraintId(item);
    if (currentConstraintId >= 0) {
        selectedGeometryObjectId_ = -1;
        selectedMeshObjectId_ = -1;
        selectedMaterialId_ = -1;
        selectedSectionId_ = -1;
        selectedNamedSelectionId_ = -1;
        selectedConstraintId_ = currentConstraintId;
        syncingTreeSelection_ = true;
        occViewWidget_->clearSelection();
        syncingTreeSelection_ = false;
        showConstraintProperties(currentConstraintId);
        if (const auto* constraint =
                constraintManager_->findConstraint(currentConstraintId)) {
            statusBar()->showMessage(
                tr("当前约束：%1").arg(fromUtf8(constraint->name)), 3000);
        }
        return;
    }
    const int currentNamedSelectionId = namedSelectionId(item);
    if (currentNamedSelectionId >= 0) {
        selectedGeometryObjectId_ = -1;
        selectedMeshObjectId_ = -1;
        selectedMaterialId_ = -1;
        selectedSectionId_ = -1;
        selectedNamedSelectionId_ = currentNamedSelectionId;
        selectedConstraintId_ = -1;
        locateSelectedNamedSelection(false);
        updateMaterialActionStates();
        updateSectionActionStates();
        return;
    }
    const int currentSectionId = sectionId(item);
    if (currentSectionId >= 0) {
        selectedGeometryObjectId_ = -1;
        selectedMeshObjectId_ = -1;
        selectedNamedSelectionId_ = -1;
        selectedConstraintId_ = -1;
        selectedMaterialId_ = -1;
        selectedSectionId_ = currentSectionId;
        selectedNamedSelectionId_ = -1;
        occViewWidget_->clearSelection();
        showSectionProperties(currentSectionId);
        updateMaterialActionStates();
        updateSectionActionStates();
        return;
    }
    const int currentMaterialId = materialId(item);
    if (currentMaterialId >= 0) {
        selectedGeometryObjectId_ = -1;
        selectedMeshObjectId_ = -1;
        selectedNamedSelectionId_ = -1;
        selectedConstraintId_ = -1;
        selectedMaterialId_ = currentMaterialId;
        selectedSectionId_ = -1;
        selectedNamedSelectionId_ = -1;
        occViewWidget_->clearSelection();
        showMaterialProperties(currentMaterialId);
        if (const emilcae::core::Material* material =
                materialManager_.findMaterial(currentMaterialId)) {
            statusBar()->showMessage(
                tr("当前材料：%1").arg(fromUtf8(material->name)), 3000);
        }
        updateMaterialActionStates();
        updateSectionActionStates();
        return;
    }
    const int currentMeshId = meshObjectId(item);
    if (currentMeshId >= 0) {
        selectedMaterialId_ = -1;
        selectedSectionId_ = -1;
        selectedNamedSelectionId_ = -1;
        selectedConstraintId_ = -1;
        const MeshObject* mesh = occViewWidget_->findMesh(currentMeshId);
        if (mesh != nullptr) {
            selectedMeshObjectId_ = currentMeshId;
            selectedGeometryObjectId_ = mesh->geometryObjectId;
            updateMaterialActionStates();
            updateSectionActionStates();
            occViewWidget_->clearSelection();
            showMeshProperties(currentMeshId);
            statusBar()->showMessage(tr("当前网格：%1").arg(mesh->name),
                                     3000);
        }
        return;
    }
    const int objectId = geometryObjectId(item);
    if (objectId >= 0) {
        selectedMaterialId_ = -1;
        selectedSectionId_ = -1;
        selectedNamedSelectionId_ = -1;
        selectedConstraintId_ = -1;
        selectedMeshObjectId_ = -1;
        selectedGeometryObjectId_ = objectId;
        updateMaterialActionStates();
        updateSectionActionStates();
        if (const GeometryObject* object =
                occViewWidget_->findGeometryObject(objectId)) {
            if (!syncingTreeSelection_ && object->visible &&
                occViewWidget_->selectGeometryObject(objectId)) {
                return;
            }
            showGeometryObjectProperties(objectId);
            statusBar()->showMessage(tr("当前对象：%1").arg(object->name),
                                     3000);
        }
        return;
    }

    selectedGeometryObjectId_ = -1;
    selectedMeshObjectId_ = -1;
    selectedMaterialId_ = -1;
    selectedSectionId_ = -1;
    selectedNamedSelectionId_ = -1;
    selectedConstraintId_ = -1;
    occViewWidget_->clearNamedSelectionHighlight();
    updateMaterialActionStates();
    updateSectionActionStates();
    if (item != nullptr) {
        showFixedNodeProperties(item->text());
    } else {
        showDefaultProperties();
    }
}

void WorkbenchMainWindow::handleProjectItemDoubleClicked(
    const QModelIndex& index) {
    QStandardItem* item = projectModel_->itemFromIndex(index);
    if (item != nullptr &&
        static_cast<ResultTreeNodeKind>(
            item->data(ResultNodeKindRole).toInt()) ==
            ResultTreeNodeKind::SequencePartFrame) {
        loadVtkResultSequenceFrame(
            item->data(ResultFrameNumberRole).toInt());
        return;
    }
    if (const auto option = resultScalarOption(item)) {
        if (resultControlWidget_->selectScalarOption(*option)) {
            showResultScalar(*option);
        }
        return;
    }
    const int currentConstraintId =
        constraintId(item);
    if (currentConstraintId >= 0) {
        selectedConstraintId_ = currentConstraintId;
        locateSelectedConstraint(true);
        editSelectedConstraint();
        return;
    }
    const int currentNamedSelectionId =
        namedSelectionId(item);
    if (currentNamedSelectionId >= 0) {
        selectedNamedSelectionId_ = currentNamedSelectionId;
        selectedConstraintId_ = -1;
        locateSelectedNamedSelection(true);
        return;
    }
    const int currentMaterialId =
        materialId(item);
    if (currentMaterialId >= 0) {
        selectedMaterialId_ = currentMaterialId;
        updateMaterialActionStates();
        editSelectedMaterial();
        return;
    }
    const int currentSectionId =
        sectionId(item);
    if (currentSectionId >= 0) {
        selectedSectionId_ = currentSectionId;
        selectedConstraintId_ = -1;
        updateSectionActionStates();
        editSelectedSection();
    }
}

void WorkbenchMainWindow::showProjectContextMenu(
    const QPoint& position) {
    const QModelIndex index = projectTree_->indexAt(position);
    QStandardItem* item = projectModel_->itemFromIndex(index);
    if (item != nullptr && static_cast<ResultTreeNodeKind>(
            item->data(ResultNodeKindRole).toInt()) ==
            ResultTreeNodeKind::SequencePart) {
        const int partId = item->data(ResultPartIdRole).toInt();
        QMenu menu(this);
        QAction* showAction = menu.addAction(tr("显示"));
        QAction* hideAction = menu.addAction(tr("隐藏"));
        QAction* fitAction = menu.addAction(tr("适合窗口"));
        menu.addSeparator();
        QAction* deleteAction = menu.addAction(tr("删除"));
        showAction->setEnabled(item->checkState() != Qt::Checked);
        hideAction->setEnabled(item->checkState() == Qt::Checked);
        QAction* chosen = menu.exec(
            projectTree_->viewport()->mapToGlobal(position));
        if (chosen == showAction)
            item->setCheckState(Qt::Checked);
        else if (chosen == hideAction)
            item->setCheckState(Qt::Unchecked);
        else if (chosen == fitAction)
            vtkPostViewWidget_->fitAll();
        else if (chosen == deleteAction)
            deleteVtkResultPart(partId);
        return;
    }
    if (item != nullptr && static_cast<ResultTreeNodeKind>(
            item->data(ResultNodeKindRole).toInt()) ==
            ResultTreeNodeKind::FilterObject) {
        const int filterId = item->data(FilterResultIdRole).toInt();
        QMenu menu(this);
        QAction* showAction = menu.addAction(tr("显示"));
        QAction* hideAction = menu.addAction(tr("隐藏"));
        QAction* renameAction = menu.addAction(tr("重命名"));
        QAction* fitAction = menu.addAction(tr("适合窗口"));
        menu.addSeparator();
        QAction* deleteAction = menu.addAction(tr("删除"));
        QAction* chosen = menu.exec(
            projectTree_->viewport()->mapToGlobal(position));
        if (chosen == showAction)
            item->setCheckState(Qt::Checked);
        else if (chosen == hideAction)
            item->setCheckState(Qt::Unchecked);
        else if (chosen == renameAction)
            renameFilterResult(filterId);
        else if (chosen == fitAction)
            vtkPostViewWidget_->fitAll();
        else if (chosen == deleteAction)
            deleteFilterResult(filterId);
        return;
    }
    if (item == boundaryConditionRootItem_) {
        QMenu menu(this);
        menu.addAction(newFixedConstraintAction_);
        menu.addAction(newDisplacementConstraintAction_);
        menu.exec(projectTree_->viewport()->mapToGlobal(position));
        return;
    }
    const int currentConstraintId = constraintId(item);
    if (currentConstraintId >= 0) {
        selectedConstraintId_ = currentConstraintId;
        selectedNamedSelectionId_ = -1;
        selectedMaterialId_ = -1;
        selectedSectionId_ = -1;
        selectedGeometryObjectId_ = -1;
        selectedMeshObjectId_ = -1;
        QMenu menu(this);
        QAction* editAction = menu.addAction(tr("编辑"));
        QAction* renameAction = menu.addAction(tr("重命名"));
        QAction* duplicateAction = menu.addAction(tr("复制"));
        QAction* locateAction = menu.addAction(tr("定位作用区域"));
        menu.addSeparator();
        QAction* deleteAction = menu.addAction(tr("删除"));
        QAction* chosen =
            menu.exec(projectTree_->viewport()->mapToGlobal(position));
        if (chosen == editAction) {
            editSelectedConstraint();
        } else if (chosen == renameAction) {
            renameSelectedConstraint();
        } else if (chosen == duplicateAction) {
            duplicateSelectedConstraint();
        } else if (chosen == locateAction) {
            locateSelectedConstraint(true);
        } else if (chosen == deleteAction) {
            deleteSelectedConstraint();
        }
        return;
    }
    if (item == namedSelectionRootItem_) {
        QMenu menu(this);
        menu.addAction(createNamedSelectionAction_);
        menu.exec(projectTree_->viewport()->mapToGlobal(position));
        return;
    }
    const int currentNamedSelectionId = namedSelectionId(item);
    if (currentNamedSelectionId >= 0) {
        selectedNamedSelectionId_ = currentNamedSelectionId;
        selectedConstraintId_ = -1;
        selectedMaterialId_ = -1;
        selectedSectionId_ = -1;
        selectedGeometryObjectId_ = -1;
        selectedMeshObjectId_ = -1;
        QMenu menu(this);
        QAction* locateAction = menu.addAction(tr("定位并高亮"));
        QAction* renameAction = menu.addAction(tr("重命名"));
        menu.addSeparator();
        QAction* replaceAction = menu.addAction(tr("用当前选择替换"));
        QAction* addAction = menu.addAction(tr("添加当前选择"));
        QAction* removeItemsAction = menu.addAction(tr("移除当前选择"));
        menu.addSeparator();
        QAction* deleteAction = menu.addAction(tr("删除"));
        menu.addSeparator();
        QAction* fixedConstraintAction =
            menu.addAction(tr("创建固定约束"));
        QAction* displacementConstraintAction =
            menu.addAction(tr("创建位移约束"));
        QAction* chosen =
            menu.exec(projectTree_->viewport()->mapToGlobal(position));
        if (chosen == locateAction) {
            locateSelectedNamedSelection(true);
        } else if (chosen == renameAction) {
            renameSelectedNamedSelection();
        } else if (chosen == replaceAction) {
            replaceSelectedNamedSelectionItems();
        } else if (chosen == addAction) {
            addSelectedNamedSelectionItems();
        } else if (chosen == removeItemsAction) {
            removeSelectedNamedSelectionItems();
        } else if (chosen == deleteAction) {
            deleteSelectedNamedSelection();
        } else if (chosen == fixedConstraintAction) {
            createFixedConstraint(currentNamedSelectionId);
        } else if (chosen == displacementConstraintAction) {
            createDisplacementConstraint(currentNamedSelectionId);
        }
        return;
    }
    if (item == sectionRootItem_) {
        QMenu menu(this);
        menu.addAction(newSectionAction_);
        menu.exec(projectTree_->viewport()->mapToGlobal(position));
        return;
    }
    const int currentSectionId = sectionId(item);
    if (currentSectionId >= 0) {
        selectedSectionId_ = currentSectionId;
        selectedConstraintId_ = -1;
        selectedMaterialId_ = -1;
        selectedNamedSelectionId_ = -1;
        selectedGeometryObjectId_ = -1;
        selectedMeshObjectId_ = -1;
        updateMaterialActionStates();
        updateSectionActionStates();
        QMenu menu(this);
        menu.addAction(editSectionAction_);
        menu.addAction(renameSectionAction_);
        menu.addAction(duplicateSectionAction_);
        menu.addSeparator();
        menu.addAction(deleteSectionAction_);
        menu.exec(projectTree_->viewport()->mapToGlobal(position));
        return;
    }
    if (item == nullptr || item == materialRootItem_) {
        QMenu menu(this);
        menu.addAction(newMaterialAction_);
        menu.addAction(steelMaterialAction_);
        menu.addAction(aluminumMaterialAction_);
        menu.exec(projectTree_->viewport()->mapToGlobal(position));
        return;
    }
    const int currentMaterialId = materialId(item);
    if (currentMaterialId >= 0) {
        selectedMaterialId_ = currentMaterialId;
        selectedConstraintId_ = -1;
        selectedNamedSelectionId_ = -1;
        selectedGeometryObjectId_ = -1;
        selectedMeshObjectId_ = -1;
        updateMaterialActionStates();
        QMenu menu(this);
        menu.addAction(editMaterialAction_);
        menu.addAction(renameMaterialAction_);
        menu.addAction(duplicateMaterialAction_);
        menu.addSeparator();
        menu.addAction(deleteMaterialAction_);
        menu.exec(projectTree_->viewport()->mapToGlobal(position));
        return;
    }
    const int currentMeshId = meshObjectId(item);
    if (currentMeshId >= 0) {
        selectedConstraintId_ = -1;
        const MeshObject* mesh = occViewWidget_->findMesh(currentMeshId);
        if (mesh == nullptr) {
            return;
        }
        QMenu menu(this);
        QAction* showAction = menu.addAction(tr("显示"));
        QAction* hideAction = menu.addAction(tr("隐藏"));
        menu.addSeparator();
        QAction* postprocessAction =
            menu.addAction(tr("在后处理中显示"));
        menu.addSeparator();
        QAction* clearAction = menu.addAction(
            mesh->importedHmAscii ? tr("删除") : tr("清除"));
        menu.addSeparator();
        QAction* assignAction = menu.addAction(tr("指派实体截面"));
        QAction* unassignAction = menu.addAction(tr("取消截面指派"));
        unassignAction->setEnabled(assignmentManager_.assignedSection(
            emilcae::core::SectionAssignmentTargetType::MeshObject,
            currentMeshId).has_value());
        showAction->setEnabled(!mesh->visible);
        hideAction->setEnabled(mesh->visible);
        QAction* selectedAction =
            menu.exec(projectTree_->viewport()->mapToGlobal(position));
        if (selectedAction == showAction) {
            setMeshVisible(item, true);
        } else if (selectedAction == hideAction) {
            setMeshVisible(item, false);
        } else if (selectedAction == postprocessAction) {
            displayMeshInPostprocessing(currentMeshId);
        } else if (selectedAction == clearAction) {
            selectedMeshObjectId_ = currentMeshId;
            selectedGeometryObjectId_ = mesh->geometryObjectId;
            clearSelectedMesh();
        } else if (selectedAction == assignAction) {
            selectedMeshObjectId_ = currentMeshId;
            selectedGeometryObjectId_ = mesh->geometryObjectId;
            assignSectionToSelectedObject();
        } else if (selectedAction == unassignAction) {
            selectedMeshObjectId_ = currentMeshId;
            selectedGeometryObjectId_ = mesh->geometryObjectId;
            unassignSectionFromSelectedObject();
        }
        return;
    }
    if (geometryObjectId(item) < 0) {
        return;
    }
    selectedConstraintId_ = -1;

    QMenu menu(this);
    QAction* showAction = menu.addAction(tr("显示"));
    QAction* hideAction = menu.addAction(tr("隐藏"));
    menu.addSeparator();
    QAction* renameAction = menu.addAction(tr("重命名"));
    QAction* deleteAction = menu.addAction(tr("删除"));
    menu.addSeparator();
    QAction* fitAction = menu.addAction(tr("适合全部"));
    menu.addSeparator();
    QAction* generateAction = menu.addAction(tr("生成四面体网格"));
    menu.addSeparator();
    QAction* assignSectionAction = menu.addAction(tr("指派实体截面"));
    QAction* unassignSectionAction = menu.addAction(tr("取消截面指派"));
    unassignSectionAction->setEnabled(assignmentManager_.assignedSection(
        emilcae::core::SectionAssignmentTargetType::GeometryObject,
        geometryObjectId(item)).has_value());

    const GeometryObject* object =
        occViewWidget_->findGeometryObject(geometryObjectId(item));
    showAction->setEnabled(object != nullptr && !object->visible);
    hideAction->setEnabled(object != nullptr && object->visible);

    QAction* selectedAction =
        menu.exec(projectTree_->viewport()->mapToGlobal(position));
    if (selectedAction == showAction) {
        setGeometryObjectVisible(item, true);
    } else if (selectedAction == hideAction) {
        setGeometryObjectVisible(item, false);
    } else if (selectedAction == renameAction) {
        renameGeometryObject(item);
    } else if (selectedAction == deleteAction) {
        deleteGeometryObject(item);
    } else if (selectedAction == fitAction) {
        occViewWidget_->fitAll();
        statusBar()->showMessage(tr("已执行适合全部"), 3000);
    } else if (selectedAction == generateAction) {
        selectedGeometryObjectId_ = geometryObjectId(item);
        selectedMeshObjectId_ = -1;
        generateTetrahedralMesh();
    } else if (selectedAction == assignSectionAction) {
        selectedGeometryObjectId_ = geometryObjectId(item);
        selectedMeshObjectId_ = -1;
        assignSectionToSelectedObject();
    } else if (selectedAction == unassignSectionAction) {
        selectedGeometryObjectId_ = geometryObjectId(item);
        selectedMeshObjectId_ = -1;
        unassignSectionFromSelectedObject();
    }
}

void WorkbenchMainWindow::setMeshVisible(QStandardItem* item, bool visible) {
    const int currentMeshId = meshObjectId(item);
    const MeshObject* before = occViewWidget_->findMesh(currentMeshId);
    if (before == nullptr) {
        return;
    }
    const bool previousVisibility = before->visible;
    if (!occViewWidget_->setMeshVisible(currentMeshId, visible)) {
        const QSignalBlocker blocker(projectModel_);
        item->setCheckState(previousVisibility ? Qt::Checked
                                               : Qt::Unchecked);
        statusBar()->showMessage(tr("网格显示状态更新失败"), 5000);
        return;
    }
    {
        const QSignalBlocker blocker(projectModel_);
        item->setCheckState(visible ? Qt::Checked : Qt::Unchecked);
    }
    if (selectedMeshObjectId_ == currentMeshId) {
        showMeshProperties(currentMeshId);
    }
    statusBar()->showMessage(
        visible ? tr("已显示网格：%1").arg(item->text())
                : tr("已隐藏网格：%1").arg(item->text()),
        3000);
}

void WorkbenchMainWindow::setGeometryObjectVisible(QStandardItem* item,
                                                   bool visible) {
    const int objectId = geometryObjectId(item);
    const GeometryObject* before =
        occViewWidget_->findGeometryObject(objectId);
    if (objectId < 0 || before == nullptr) {
        return;
    }
    const bool previousVisibility = before->visible;
    if (!occViewWidget_->setGeometryObjectVisible(objectId, visible)) {
        const QSignalBlocker blocker(projectModel_);
        item->setCheckState(previousVisibility ? Qt::Checked
                                               : Qt::Unchecked);
        statusBar()->showMessage(tr("对象显示状态更新失败"), 5000);
        return;
    }

    {
        const QSignalBlocker blocker(projectModel_);
        item->setCheckState(visible ? Qt::Checked : Qt::Unchecked);
    }
    if (selectedGeometryObjectId_ == objectId) {
        showGeometryObjectProperties(objectId);
    }
    statusBar()->showMessage(
        visible ? tr("已显示对象：%1").arg(item->text())
                : tr("已隐藏对象：%1").arg(item->text()),
        3000);
}

void WorkbenchMainWindow::renameGeometryObject(QStandardItem* item) {
    const int objectId = geometryObjectId(item);
    const GeometryObject* object =
        occViewWidget_->findGeometryObject(objectId);
    if (object == nullptr) {
        return;
    }

    bool accepted = false;
    const QString name = QInputDialog::getText(
        this, tr("重命名几何对象"), tr("对象名称："),
        QLineEdit::Normal, object->name, &accepted).trimmed();
    if (!accepted) {
        return;
    }
    if (name.isEmpty()) {
        QMessageBox::warning(this, tr("名称无效"),
                             tr("对象名称不能为空。"));
        return;
    }
    if (!occViewWidget_->renameGeometryObject(objectId, name)) {
        QMessageBox::warning(this, tr("重命名失败"),
                             tr("无法重命名该几何对象。"));
        return;
    }

    item->setText(name);
    if (const MeshObject* mesh =
            occViewWidget_->findMeshForGeometry(objectId)) {
        const QString meshName = tr("%1_网格").arg(name);
        occViewWidget_->renameMesh(mesh->id, meshName);
        if (QStandardItem* currentMeshItem = meshItem(mesh->id)) {
            currentMeshItem->setText(tr("网格"));
        }
    }
    if (selectedGeometryObjectId_ == objectId) {
        showGeometryObjectProperties(objectId);
    }
    updateAssignmentDisplays();
    messageLog_->appendPlainText(tr("几何对象已重命名为：%1").arg(name));
}

void WorkbenchMainWindow::deleteGeometryObject(QStandardItem* item) {
    const int objectId = geometryObjectId(item);
    const GeometryObject* object =
        occViewWidget_->findGeometryObject(objectId);
    if (object == nullptr) {
        return;
    }
    const QString name = object->name;
    const MeshObject* associatedMesh =
        occViewWidget_->findMeshForGeometry(objectId);
    const int associatedMeshId = associatedMesh != nullptr
        ? associatedMesh->id
        : -1;
    const std::size_t namedSelectionReferences =
        namedSelectionManager_->selectionsReferencingGeometry(objectId).size();
    const QString question = namedSelectionReferences > 0
        ? tr("该几何对象正在被 %1 个命名选择集引用。\n"
             "删除后相关成员将变为失效引用，是否继续？")
              .arg(namedSelectionReferences)
        : tr("确定要从当前场景中删除对象“%1”吗？").arg(name);
    if (QMessageBox::question(
            this, tr("删除几何对象"),
            question,
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    if (!occViewWidget_->removeGeometryObject(objectId)) {
        QMessageBox::warning(this, tr("删除失败"),
                             tr("无法从当前场景删除该对象。"));
        return;
    }

    clearPostprocessingMeshIfMatches(associatedMeshId);
    geometryItems_.remove(objectId);
    assignmentManager_.unassign(
        emilcae::core::SectionAssignmentTargetType::GeometryObject,
        objectId);
    if (associatedMeshId >= 0) {
        assignmentManager_.unassign(
            emilcae::core::SectionAssignmentTargetType::MeshObject,
            associatedMeshId);
        meshItems_.remove(associatedMeshId);
    }
    geometryRootItem_->removeRow(item->row());
    if (selectedGeometryObjectId_ == objectId) {
        selectedGeometryObjectId_ = -1;
        selectedMeshObjectId_ = -1;
        showDefaultProperties();
    }
    clearAllGeometryAction_->setEnabled(
        occViewWidget_->geometryObjectCount() > 0);
    updateAssignmentDisplays();
    updateNamedSelectionDisplays();
    updateSectionActionStates();
    messageLog_->appendPlainText(tr("已从场景删除几何对象：%1").arg(name));
    statusBar()->showMessage(tr("几何对象已删除"), 3000);
}

void WorkbenchMainWindow::clearAllGeometryObjects() {
    if (occViewWidget_->geometryObjectCount() == 0) {
        return;
    }
    std::size_t affectedSelections = 0;
    for (const emilcae::core::NamedSelection& selection :
         namedSelectionManager_->namedSelections()) {
        if (std::any_of(selection.items.cbegin(), selection.items.cend(),
                        [this](const emilcae::core::NamedSelectionItem& item) {
                            return geometryItems_.contains(
                                item.geometryObjectId);
                        })) {
            ++affectedSelections;
        }
    }
    QString question = tr("确定要清空当前场景中的所有几何对象吗？\n"
                          "此操作不会删除磁盘上的原始文件。");
    if (affectedSelections > 0) {
        question += tr("\n\n有 %1 个命名选择集引用当前几何。清空后相关成员将变为失效引用。")
                        .arg(affectedSelections);
    }
    if (QMessageBox::question(
            this, tr("清空几何对象"),
            question,
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    for (auto iterator = geometryItems_.cbegin();
         iterator != geometryItems_.cend(); ++iterator) {
        assignmentManager_.unassign(
            emilcae::core::SectionAssignmentTargetType::GeometryObject,
            iterator.key());
    }
    for (auto iterator = meshItems_.cbegin();
         iterator != meshItems_.cend(); ++iterator) {
        const MeshObject* mesh = occViewWidget_->findMesh(iterator.key());
        if (mesh != nullptr && mesh->geometryObjectId >= 0) {
            assignmentManager_.unassign(
                emilcae::core::SectionAssignmentTargetType::MeshObject,
                mesh->id);
        }
    }
    occViewWidget_->clearGeometryObjects();
    if (currentPostMeshId_ >= 0 &&
        occViewWidget_->findMesh(currentPostMeshId_) == nullptr) {
        clearPostprocessingMeshIfMatches(currentPostMeshId_);
    }
    geometryRootItem_->removeRows(0, geometryRootItem_->rowCount());
    geometryItems_.clear();
    for (auto iterator = meshItems_.begin(); iterator != meshItems_.end();) {
        if (occViewWidget_->findMesh(iterator.key()) == nullptr) {
            iterator = meshItems_.erase(iterator);
        } else {
            ++iterator;
        }
    }
    selectedGeometryObjectId_ = -1;
    selectedMeshObjectId_ = -1;
    showDefaultProperties();
    clearAllGeometryAction_->setEnabled(false);
    updateAssignmentDisplays();
    updateNamedSelectionDisplays();
    updateSectionActionStates();
    setWindowTitle(tr("QTCAE 仿真工作台"));
    messageLog_->appendPlainText(tr("已清空所有几何对象。"));
    statusBar()->showMessage(tr("所有几何对象已清空"), 3000);
}

void WorkbenchMainWindow::generateTetrahedralMesh() {
    const GeometryObject* object =
        occViewWidget_->findGeometryObject(selectedGeometryObjectId_);
    if (object == nullptr) {
        QMessageBox::information(this, tr("生成网格"),
                                 tr("请先在工程树中选择一个几何对象。"));
        return;
    }
    if (object->shape.IsNull()) {
        QMessageBox::warning(this, tr("生成网格"),
                             tr("所选几何对象为空，无法生成网格。"));
        return;
    }
    if (!object->visible) {
        QMessageBox::information(
            this, tr("几何对象已隐藏"),
            tr("所选几何对象当前处于隐藏状态，仍将继续生成网格。"));
    }

    bool accepted = false;
    const double targetSize = QInputDialog::getDouble(
        this, tr("生成四面体网格"), tr("全局目标网格尺寸："),
        defaultMeshSize(object->shape), 1.0e-9, 1.0e12, 6,
        &accepted);
    if (!accepted) {
        return;
    }

    statusBar()->showMessage(tr("正在生成四面体网格…"));
    MeshResult result;
    {
        const WaitCursor waitCursor;
        const GmshMesher mesher;
        result = mesher.generateTetrahedralMesh(object->shape, targetSize);
    }
    if (!result.success) {
        const QString reason = result.errorMessage.isEmpty()
            ? tr("未知错误")
            : result.errorMessage;
        statusBar()->showMessage(tr("四面体网格生成失败"), 5000);
        messageLog_->appendPlainText(
            tr("网格生成失败：%1\n原因：%2").arg(object->name, reason));
        QMessageBox::critical(this, tr("网格生成失败"), reason);
        return;
    }

    const QString meshName = tr("%1_网格").arg(object->name);
    const int meshId = occViewWidget_->replaceMesh(
        object->id, meshName, result.mesh, targetSize);
    if (meshId < 0) {
        const QString reason = tr("表面网格显示失败，原有网格已保留。");
        statusBar()->showMessage(tr("四面体网格生成失败"), 5000);
        messageLog_->appendPlainText(
            tr("网格生成失败：%1\n原因：%2").arg(object->name, reason));
        QMessageBox::critical(this, tr("网格显示失败"), reason);
        return;
    }

    addOrUpdateMeshTreeItem(meshId, object->id);
    if (QStandardItem* geometryTreeItem = geometryItem(object->id)) {
        const QSignalBlocker blocker(projectModel_);
        geometryTreeItem->setCheckState(Qt::Unchecked);
    }
    selectedMeshObjectId_ = meshId;
    selectedGeometryObjectId_ = object->id;
    showMeshProperties(meshId);
    occViewWidget_->fitAll();
    const MeshObject* mesh = occViewWidget_->findMesh(meshId);
    if (mesh != nullptr) {
        messageLog_->appendPlainText(
            tr("已生成四面体网格：%1；节点 %2，四面体 %3，表面三角形 %4。")
                .arg(object->name)
                .arg(mesh->data.nodes.size())
                .arg(mesh->data.tetrahedra.size())
                .arg(mesh->data.surfaceTriangles.size()));
    }
    statusBar()->showMessage(tr("四面体网格生成成功"), 5000);
}

void WorkbenchMainWindow::clearSelectedMesh() {
    int currentMeshId = selectedMeshObjectId_;
    if (currentMeshId < 0 && selectedGeometryObjectId_ >= 0) {
        if (const MeshObject* mesh = occViewWidget_->findMeshForGeometry(
                selectedGeometryObjectId_)) {
            currentMeshId = mesh->id;
        }
    }
    const MeshObject* mesh = occViewWidget_->findMesh(currentMeshId);
    if (mesh == nullptr) {
        QMessageBox::information(this, tr("清除网格"),
                                 tr("当前选择没有可清除的网格。"));
        return;
    }
    const int geometryObjectId = mesh->geometryObjectId;
    const QString meshName = mesh->name;
    const bool deletingImportedMesh = mesh->importedHmAscii;
    if (QMessageBox::question(
            this,
            deletingImportedMesh ? tr("删除网格") : tr("清除网格"),
            deletingImportedMesh
                ? tr("确定要删除网格“%1”吗？").arg(meshName)
                : tr("确定要清除网格“%1”吗？").arg(meshName),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    if (!occViewWidget_->removeMesh(currentMeshId)) {
        QMessageBox::warning(this, tr("清除网格"),
                             tr("无法清除当前网格。"));
        return;
    }

    clearPostprocessingMeshIfMatches(currentMeshId);
    assignmentManager_.unassign(
        emilcae::core::SectionAssignmentTargetType::MeshObject,
        currentMeshId);

    QStandardItem* item = meshItem(currentMeshId);
    meshItems_.remove(currentMeshId);
    if (item != nullptr && item->parent() != nullptr) {
        item->parent()->removeRow(item->row());
    }
    selectedMeshObjectId_ = -1;
    selectedGeometryObjectId_ = geometryObjectId;
    if (QStandardItem* geometry = geometryItem(geometryObjectId)) {
        projectTree_->setCurrentIndex(geometry->index());
    }
    showGeometryObjectProperties(geometryObjectId);
    if (QStandardItem* geometry = geometryItem(geometryObjectId)) {
        const QSignalBlocker blocker(projectModel_);
        geometry->setCheckState(Qt::Checked);
    }
    messageLog_->appendPlainText(
        deletingImportedMesh ? tr("已删除网格：%1").arg(meshName)
                             : tr("已清除网格：%1").arg(meshName));
    statusBar()->showMessage(
        deletingImportedMesh ? tr("网格已删除") : tr("网格已清除"), 3000);
    updateAssignmentDisplays();
    updateSectionActionStates();
}

void WorkbenchMainWindow::importHmAsciiMesh() {
    const QString filePath = QFileDialog::getOpenFileName(
        this, tr("导入 HMASCII 网格"), {},
        tr("HyperMesh ASCII 网格文件 (*.hm *.hmascii *.txt);;"
           "所有文件 (*.*)"));
    if (filePath.isEmpty()) {
        return;
    }

    const HmAsciiMeshIo meshIo;
    const HmAsciiImportResult result = meshIo.importFile(filePath);
    if (!result.success) {
        const QString reason = result.errorMessage.isEmpty()
            ? tr("未知错误")
            : result.errorMessage;
        statusBar()->showMessage(tr("HMASCII 网格导入失败"), 5000);
        messageLog_->appendPlainText(
            tr("HMASCII 网格导入失败：%1\n原因：%2")
                .arg(filePath, reason));
        QMessageBox::critical(this, tr("HMASCII 导入失败"), reason);
        return;
    }

    const QString meshName = result.componentName.trimmed().isEmpty()
        ? tr("Imported HMASCII Mesh")
        : result.componentName.trimmed();
    const int meshId = occViewWidget_->addStandaloneMesh(
        meshName, QFileInfo(filePath).absoluteFilePath(), result.mesh);
    if (meshId < 0) {
        const QString reason = tr("HMASCII 表面网格显示失败。");
        statusBar()->showMessage(tr("HMASCII 网格导入失败"), 5000);
        messageLog_->appendPlainText(
            tr("HMASCII 网格导入失败：%1\n原因：%2")
                .arg(filePath, reason));
        QMessageBox::critical(this, tr("HMASCII 导入失败"), reason);
        return;
    }

    addStandaloneMeshTreeItem(meshId, meshName);
    selectedGeometryObjectId_ = -1;
    selectedMeshObjectId_ = meshId;
    switchToPreprocessing();
    showMeshProperties(meshId);
    occViewWidget_->fitAll();
    messageLog_->appendPlainText(
        tr("已导入 HMASCII 网格：%1；节点 %2，四面体 %3，表面三角形 %4。")
            .arg(filePath)
            .arg(result.mesh.nodes.size())
            .arg(result.mesh.tetrahedra.size())
            .arg(result.mesh.surfaceTriangles.size()));
    for (const QString& warning : result.warnings) {
        messageLog_->appendPlainText(tr("HMASCII 警告：%1").arg(warning));
    }
    statusBar()->showMessage(tr("HMASCII 网格导入成功"), 5000);
}

void WorkbenchMainWindow::exportHmAsciiMesh() {
    const MeshObject* mesh = occViewWidget_->findMesh(selectedMeshObjectId_);
    if (mesh == nullptr) {
        QMessageBox::information(
            this, tr("导出 HMASCII 网格"),
            tr("请先在工程树中选择需要导出的网格。"));
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(
        this, tr("导出 HMASCII 网格"), mesh->name + QStringLiteral(".hm"),
        tr("HyperMesh ASCII 网格文件 (*.hm *.hmascii)"));
    if (filePath.isEmpty()) {
        return;
    }
    if (QFileInfo(filePath).suffix().isEmpty()) {
        filePath += QStringLiteral(".hm");
    }

    const HmAsciiMeshIo meshIo;
    const HmAsciiIoResult result =
        meshIo.exportFile(filePath, mesh->data, mesh->name);
    if (!result.success) {
        const QString reason = result.errorMessage.isEmpty()
            ? tr("未知错误")
            : result.errorMessage;
        statusBar()->showMessage(tr("HMASCII 网格导出失败"), 5000);
        messageLog_->appendPlainText(
            tr("HMASCII 网格导出失败：%1\n原因：%2")
                .arg(filePath, reason));
        QMessageBox::critical(this, tr("HMASCII 导出失败"), reason);
        return;
    }

    messageLog_->appendPlainText(
        tr("已导出 HMASCII 网格：%1").arg(filePath));
    for (const QString& warning : result.warnings) {
        messageLog_->appendPlainText(tr("HMASCII 警告：%1").arg(warning));
    }
    statusBar()->showMessage(tr("HMASCII 网格导出成功"), 5000);
}

SphDynamicMaterialDefinition
WorkbenchMainWindow::initialSphMaterialForMesh(const MeshObject& mesh) const {
    SphDynamicMaterialDefinition material;
    const std::optional<int> geometryId = mesh.geometryObjectId >= 0
        ? std::optional<int>(mesh.geometryObjectId)
        : std::nullopt;
    const auto assignment = assignmentManager_.resolvedForMesh(
        mesh.id, geometryId);
    if (!assignment) {
        return material;
    }
    const auto* section = sectionManager_.findSection(assignment->sectionId);
    if (section == nullptr) {
        return material;
    }
    const auto* source = materialManager_.findMaterial(section->materialId);
    if (source == nullptr) {
        return material;
    }
    material.name = fromUtf8(source->name);
    if (std::isfinite(source->density) && source->density > 0.0) {
        material.density = source->density;
    }
    if (std::isfinite(source->elasticity.youngsModulus) &&
        source->elasticity.youngsModulus > 0.0) {
        material.youngsModulus = source->elasticity.youngsModulus;
    }
    if (std::isfinite(source->elasticity.poissonRatio) &&
        source->elasticity.poissonRatio > -1.0 &&
        source->elasticity.poissonRatio < 0.5) {
        material.poissonRatio = source->elasticity.poissonRatio;
    }
    return material;
}

void WorkbenchMainWindow::runSphAnalysis() {
    if (sphSolverProcess_->isRunning()) {
        QMessageBox::information(
            this, tr("SPH 分析"), tr("已有 SPH 分析正在运行。"));
        taskMonitorDock_->show();
        taskMonitorDock_->raise();
        return;
    }
    std::vector<SphBodyJobInput> availableBodies;
    for (const MeshObject& mesh : occViewWidget_->meshes()) {
        if (mesh.data.nodes.empty() || mesh.data.tetrahedra.empty()) {
            continue;
        }
        SphBodyJobInput body;
        body.meshObjectId = mesh.id;
        body.bodyName = mesh.name;
        body.meshName = mesh.name;
        body.sourceMeshFilePath = mesh.sourceFilePath;
        body.sourceIsHmAscii = mesh.importedHmAscii;
        body.mesh = mesh.data;
        body.material = initialSphMaterialForMesh(mesh);
        body.preserveSourceNodeSets = mesh.importedHmAscii;
        availableBodies.push_back(std::move(body));
    }
    if (availableBodies.empty()) {
        QMessageBox::information(
            this, tr("SPH 分析"),
            tr("请先导入或生成至少一个一阶四面体网格。"));
        return;
    }

    SphMultiBodyAnalysisDialog dialog(
        std::move(availableBodies), selectedMeshObjectId_, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const SphMultiBodyJobInput input = dialog.jobInput();

    const SphJobWriteResult writeResult = SphJobWriter().write(input);
    if (!writeResult.success) {
        const QString reason = writeResult.errorMessage.isEmpty()
            ? tr("未知错误") : writeResult.errorMessage;
        statusBar()->showMessage(tr("SPH 作业生成失败"), 5000);
        messageLog_->appendPlainText(
            tr("SPH 作业生成失败：%1").arg(reason));
        QMessageBox::critical(this, tr("SPH 作业生成失败"), reason);
        return;
    }

    currentSphJobName_ = input.settings.jobName;
    currentSphSimulationTime_ = input.settings.maximumSteps > 0
        ? 0.0
        : input.settings.simulationTime;
    messageLogDock_->show();
    messageLog_->appendPlainText(
        tr("已生成 SPH 作业：%1\n部件数量：%2\n配置文件：%3\n结果目录：%4\n本次步数限制：%5")
            .arg(currentSphJobName_)
            .arg(static_cast<qulonglong>(input.bodies.size()))
            .arg(writeResult.configurationFilePath)
            .arg(writeResult.outputDirectory)
            .arg(input.settings.maximumSteps == 0
                     ? tr("按终止时间完整运行")
                     : QString::number(input.settings.maximumSteps)));
    for (const QString& warning : writeResult.warnings) {
        messageLog_->appendPlainText(tr("SPH 作业警告：%1").arg(warning));
    }

    QStringList solverArguments;
    if (input.settings.maximumSteps > 0) {
        solverArguments << QStringLiteral("--steps")
                        << QString::number(input.settings.maximumSteps);
    }
    if (!sphSolverProcess_->start(
            input.settings.solverExecutable,
            writeResult.configurationFilePath,
            writeResult.outputDirectory, solverArguments)) {
        const QString reason = sphSolverProcess_->lastError().isEmpty()
            ? tr("未知错误")
            : sphSolverProcess_->lastError();
        statusBar()->showMessage(tr("SPH 求解器启动失败"), 5000);
        messageLog_->appendPlainText(
            tr("SPH 求解器启动失败：%1").arg(reason));
        QMessageBox::critical(this, tr("SPH 求解器启动失败"), reason);
        return;
    }

    sphTaskRow_ = taskModel_->rowCount();
    QList<QStandardItem*> row;
    row << new QStandardItem(currentSphJobName_)
        << new QStandardItem(tr("SPH Solver"))
        << new QStandardItem(tr("计算中"))
        << new QStandardItem(tr("0.0%"));
    taskModel_->appendRow(row);
    taskMonitorDock_->show();
    taskMonitorDock_->raise();
    runSphAnalysisAction_->setEnabled(false);
    runExistingSphConfigurationAction_->setEnabled(false);
    stopSphAnalysisAction_->setEnabled(true);
    statusBar()->showMessage(tr("SPH 求解器已启动"), 3000);
}

void WorkbenchMainWindow::runExistingSphConfiguration() {
    if (sphSolverProcess_->isRunning()) {
        QMessageBox::information(
            this, tr("SPH 分析"), tr("已有 SPH 分析正在运行。"));
        taskMonitorDock_->show();
        taskMonitorDock_->raise();
        return;
    }

    SphExistingJobDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;
    const SphExistingJobSettings settings = dialog.settings();

    const QFileInfo configuration(settings.configurationFile);
    currentSphJobName_ = configuration.dir().dirName();
    if (currentSphJobName_.isEmpty())
        currentSphJobName_ = tr("已有 SPH 配置");
    currentSphSimulationTime_ = settings.endTime;
    messageLogDock_->show();
    messageLog_->appendPlainText(
        tr("正在运行已有 SPH 配置：%1\n结果目录：%2\n最大步数：%3")
            .arg(settings.configurationFile, settings.outputDirectory)
            .arg(settings.maximumSteps == 0
                     ? tr("按配置完整运行")
                     : QString::number(settings.maximumSteps)));

    QStringList arguments;
    if (settings.maximumSteps > 0) {
        arguments << QStringLiteral("--steps")
                  << QString::number(settings.maximumSteps);
    }
    if (settings.endTime > 0.0) {
        arguments << QStringLiteral("--end-time")
                  << QString::number(settings.endTime, 'g', 16);
    }
    arguments << QStringLiteral("--output-interval")
              << QString::number(settings.outputInterval);

    if (!sphSolverProcess_->start(
            settings.solverExecutable, settings.configurationFile,
            settings.outputDirectory, arguments)) {
        const QString reason = sphSolverProcess_->lastError().isEmpty()
            ? tr("未知错误")
            : sphSolverProcess_->lastError();
        statusBar()->showMessage(tr("SPH 求解器启动失败"), 5000);
        messageLog_->appendPlainText(
            tr("SPH 求解器启动失败：%1").arg(reason));
        QMessageBox::critical(this, tr("SPH 求解器启动失败"), reason);
        return;
    }

    sphTaskRow_ = taskModel_->rowCount();
    QList<QStandardItem*> row;
    row << new QStandardItem(currentSphJobName_)
        << new QStandardItem(tr("SPH Solver"))
        << new QStandardItem(tr("计算中"))
        << new QStandardItem(tr("0.0%"));
    taskModel_->appendRow(row);
    taskMonitorDock_->show();
    taskMonitorDock_->raise();
    runSphAnalysisAction_->setEnabled(false);
    runExistingSphConfigurationAction_->setEnabled(false);
    stopSphAnalysisAction_->setEnabled(true);
    statusBar()->showMessage(tr("SPH 求解器已启动"), 3000);
}

void WorkbenchMainWindow::stopSphAnalysis() {
    if (!sphSolverProcess_->isRunning()) {
        return;
    }
    stopSphAnalysisAction_->setEnabled(false);
    statusBar()->showMessage(tr("正在停止 SPH 分析..."));
    messageLog_->appendPlainText(tr("正在请求停止当前 SPH 分析。"));
    sphSolverProcess_->stop();
}

void WorkbenchMainWindow::handleSphRunFinished(
    bool success, const QString& message, const QString& outputDirectory) {
    runSphAnalysisAction_->setEnabled(true);
    runExistingSphConfigurationAction_->setEnabled(true);
    stopSphAnalysisAction_->setEnabled(false);
    if (taskModel_ != nullptr && sphTaskRow_ >= 0 &&
        sphTaskRow_ < taskModel_->rowCount()) {
        taskModel_->item(sphTaskRow_, 2)->setText(
            success ? tr("已完成") : tr("失败/已停止"));
        if (success) {
            taskModel_->item(sphTaskRow_, 3)->setText(tr("100.0%"));
        }
    }
    messageLog_->appendPlainText(message);
    if (!success) {
        statusBar()->showMessage(tr("SPH 分析未完成"), 5000);
        QMessageBox::warning(this, tr("SPH 分析"), message);
        return;
    }

    statusBar()->showMessage(tr("SPH 分析完成，正在加载结果..."));
    if (!loadVtkResultSequenceDirectory(outputDirectory, false, true)) {
        const QString reason = tr(
            "SPH 计算已完成，但结果目录无法作为 VTK 序列加载：%1")
                                   .arg(outputDirectory);
        statusBar()->showMessage(tr("SPH 结果自动加载失败"), 5000);
        messageLog_->appendPlainText(reason);
        QMessageBox::warning(this, tr("SPH 结果自动加载失败"), reason);
        return;
    }
    messageLog_->appendPlainText(
        tr("SPH 结果已自动载入后处理：%1").arg(outputDirectory));
    statusBar()->showMessage(tr("SPH 分析完成，结果已加载"), 5000);
}

void WorkbenchMainWindow::importVtkResult() {
    const QString filePath = QFileDialog::getOpenFileName(
        this, tr("导入 VTK 结果"), {},
        tr("VTK 非结构网格结果 (*.vtu *.vtk);;"
           "VTK XML 非结构网格结果 (*.vtu);;"
           "VTK Legacy 非结构网格结果 (*.vtk);;"
           "所有文件 (*.*)"));
    if (filePath.isEmpty()) {
        return;
    }

    VtkResultSequence candidate;
    if (loadedResultIsStandaloneCollection_ && loadedResultSequence_ &&
        loadedResultSequence_->frames.size() == 1) {
        candidate = *loadedResultSequence_;
    } else {
        candidate.frames.push_back({0, {}});
    }
    const QFileInfo fileInfo(filePath);
    const int newPartId = nextStandaloneResultPartId_++;
    candidate.frames.front().parts.push_back({
        newPartId,
        toUtf8(fileInfo.fileName()),
        std::filesystem::path(filePath.toStdWString())});

    VtkResultSequenceLoadResult result;
    {
        const WaitCursor waitCursor;
        const VtkResultSequenceLoader loader;
        result = loader.load(candidate.frames.front());
    }
    if (!result.success || result.grid == nullptr) {
        const QString reason = result.errorMessage.empty()
            ? tr("未知错误")
            : fromUtf8(result.errorMessage);
        statusBar()->showMessage(tr("VTK 结果导入失败"), 5000);
        messageLog_->appendPlainText(
            tr("VTK 结果导入失败：%1\n原因：%2")
                .arg(filePath, reason));
        QMessageBox::critical(this, tr("VTK 结果导入失败"), reason);
        return;
    }
    QHash<int, bool> candidateVisibility =
        loadedResultIsStandaloneCollection_ ? resultPartVisibility_
                                            : QHash<int, bool>{};
    candidateVisibility.insert(newPartId, true);
    if (!vtkPostViewWidget_->displayResultGrid(
            result.grid, postViewParts(result, candidateVisibility), true)) {
        const QString reason = vtkPostViewWidget_->lastError().isEmpty()
            ? tr("未知错误")
            : vtkPostViewWidget_->lastError();
        statusBar()->showMessage(tr("VTK 结果导入失败"), 5000);
        messageLog_->appendPlainText(
            tr("VTK 结果无法显示：%1\n原因：%2")
                .arg(filePath, reason));
        QMessageBox::critical(this, tr("VTK 结果无法显示"), reason);
        return;
    }
    vtkPostViewWidget_->clearFilters();

    clearLoadedVtkResult(false);
    loadedResultIsStandaloneCollection_ = true;
    resultPartVisibility_ = candidateVisibility;
    loadedResultSequence_ = std::move(candidate);
    loadedResultFrameNumber_ = 0;
    loadedResultFilePath_ = tr("已导入 VTK 结果");
    loadedResultGrid_ = result.grid;
    loadedResultFields_ = result.fields;
    currentPostMeshId_ = -1;
    currentPostMeshItem_ = nullptr;
    buildVtkResultSequenceTree();
    resultControlWidget_->setResultFields(loadedResultFields_);
    resultControlWidget_->setSequenceFrames({0}, 0);

    switchToPostprocessing();
    resultControlDock_->raise();
    updateSelectedDisplacementField();
    if (resultControlWidget_->scalarVisible()) {
        applySelectedResultScalar();
    } else {
        showVtkResultSequenceProperties();
    }

    const qlonglong partCount = static_cast<qlonglong>(
        loadedResultSequence_->frames.front().parts.size());
    setWindowTitle(tr("QTCAE 仿真工作台 - 后处理 - %1 个部件")
                       .arg(partCount));
    statusBar()->showMessage(tr("VTK 结果已作为新部件加入"), 5000);
    messageLog_->appendPlainText(
        tr("已加入 VTK 结果部件：%1；当前共 %2 个部件；节点 %3，单元 %4，公共结果字段 %5。")
            .arg(filePath)
            .arg(partCount)
            .arg(loadedResultGrid_->GetNumberOfPoints())
            .arg(loadedResultGrid_->GetNumberOfCells())
            .arg(loadedResultFields_.size()));
    for (const std::string& warning : result.warnings) {
        messageLog_->appendPlainText(
            tr("VTK 警告：%1").arg(fromUtf8(warning)));
    }
}

void WorkbenchMainWindow::importVtkResultSequence() {
    const QString directory = QFileDialog::getExistingDirectory(
        this, tr("导入 VTK 结果序列"));
    if (directory.isEmpty()) {
        return;
    }
    loadVtkResultSequenceDirectory(directory);
}

bool WorkbenchMainWindow::loadVtkResultSequenceDirectory(
    const QString& directory, bool showErrors, bool loadLastFrame) {
    const VtkResultSequenceScanner scanner;
    VtkResultSequenceScanResult scanResult;
    VtkResultSequenceLoadResult loadResult;
    {
        const WaitCursor waitCursor;
        scanResult = scanner.scan(
            std::filesystem::path(directory.toStdWString()));
        if (scanResult.success && !scanResult.sequence.frames.empty()) {
            const VtkResultSequenceLoader loader;
            const auto& frame = loadLastFrame
                ? scanResult.sequence.frames.back()
                : scanResult.sequence.frames.front();
            loadResult = loader.load(frame);
        }
    }
    if (!scanResult.success || scanResult.sequence.frames.empty()) {
        const QString reason = scanResult.errorMessage.empty()
            ? tr("目录中没有可用的结果帧。")
            : fromUtf8(scanResult.errorMessage);
        statusBar()->showMessage(tr("VTK 结果序列导入失败"), 5000);
        messageLog_->appendPlainText(
            tr("VTK 结果序列扫描失败：%1\n原因：%2")
                .arg(directory, reason));
        if (showErrors) {
            QMessageBox::critical(
                this, tr("VTK 结果序列导入失败"), reason);
        }
        return false;
    }
    if (!loadResult.success || loadResult.grid == nullptr) {
        const QString reason = loadResult.errorMessage.empty()
            ? tr("目标结果帧加载失败。")
            : fromUtf8(loadResult.errorMessage);
        statusBar()->showMessage(tr("VTK 结果序列导入失败"), 5000);
        messageLog_->appendPlainText(
            tr("VTK 结果序列目标帧加载失败：%1\n原因：%2")
                .arg(directory, reason));
        if (showErrors) {
            QMessageBox::critical(
                this, tr("VTK 结果序列导入失败"), reason);
        }
        return false;
    }
    QHash<int, bool> initialPartVisibility;
    for (const auto& frame : scanResult.sequence.frames) {
        for (const auto& part : frame.parts) {
            initialPartVisibility.insert(part.id, true);
        }
    }
    vtkPostViewWidget_->clearFilters();
    if (!vtkPostViewWidget_->displayResultGrid(
            loadResult.grid,
            postViewParts(loadResult, initialPartVisibility), true)) {
        const QString reason = vtkPostViewWidget_->lastError().isEmpty()
            ? tr("未知错误")
            : vtkPostViewWidget_->lastError();
        statusBar()->showMessage(tr("VTK 结果序列导入失败"), 5000);
        if (showErrors) {
            QMessageBox::critical(
                this, tr("VTK 结果序列无法显示"), reason);
        }
        return false;
    }

    clearLoadedVtkResult(false);
    loadedResultIsStandaloneCollection_ = false;
    resultPartVisibility_ = initialPartVisibility;
    loadedResultSequence_ = std::move(scanResult.sequence);
    loadedResultFrameNumber_ = loadLastFrame
        ? loadedResultSequence_->frames.back().frameNumber
        : loadedResultSequence_->frames.front().frameNumber;
    loadedResultFilePath_ = directory;
    loadedResultGrid_ = loadResult.grid;
    loadedResultFields_ = loadResult.fields;
    currentPostMeshId_ = -1;
    currentPostMeshItem_ = nullptr;
    buildVtkResultSequenceTree();
    resultControlWidget_->setResultFields(loadedResultFields_);
    std::vector<int> frameNumbers;
    frameNumbers.reserve(loadedResultSequence_->frames.size());
    for (const auto& frame : loadedResultSequence_->frames) {
        frameNumbers.push_back(frame.frameNumber);
    }
    resultControlWidget_->setSequenceFrames(
        frameNumbers, loadedResultFrameNumber_);

    switchToPostprocessing();
    resultControlDock_->raise();
    updateSelectedDisplacementField();
    if (resultControlWidget_->scalarVisible()) {
        applySelectedResultScalar();
    } else {
        showVtkResultSequenceFrameProperties(
            loadedResultFrameNumber_);
    }
    setWindowTitle(
        tr("QTCAE 仿真工作台 - 后处理 - %1 - 帧 %2")
            .arg(QFileInfo(directory).fileName())
            .arg(loadedResultFrameNumber_));
    statusBar()->showMessage(
        tr("VTK 结果序列导入成功，当前帧 %1")
            .arg(loadedResultFrameNumber_), 5000);
    messageLog_->appendPlainText(
        tr("已导入 VTK 结果序列：%1；共 %2 帧；当前按需加载帧 %3。")
            .arg(directory)
            .arg(loadedResultSequence_->frames.size())
            .arg(loadedResultFrameNumber_));
    for (const std::string& warning :
         loadedResultSequence_->warnings) {
        messageLog_->appendPlainText(
            tr("VTK 序列警告：%1").arg(fromUtf8(warning)));
    }
    for (const std::string& warning : loadResult.warnings) {
        messageLog_->appendPlainText(
            tr("VTK 帧警告：%1").arg(fromUtf8(warning)));
    }
    return true;
}

bool WorkbenchMainWindow::loadVtkResultSequenceFrame(
    int frameNumber, bool firstLoad) {
    Q_UNUSED(firstLoad)
    if (!loadedResultSequence_ ||
        frameNumber == loadedResultFrameNumber_) {
        return loadedResultSequence_.has_value();
    }
    const auto frameIterator = std::find_if(
        loadedResultSequence_->frames.begin(),
        loadedResultSequence_->frames.end(),
        [frameNumber](const VtkResultSequenceFrame& frame) {
            return frame.frameNumber == frameNumber;
        });
    if (frameIterator == loadedResultSequence_->frames.end()) {
        return false;
    }

    const auto previousScalar =
        resultControlWidget_->selectedScalarOption();
    const std::string previousDisplacement =
        resultControlWidget_->selectedDisplacementField();
    const bool previousScalarVisible =
        resultControlWidget_->scalarVisible();
    const bool previousDeformationVisible =
        resultControlWidget_->deformationVisible();
    const bool previousLegendVisible =
        resultControlWidget_->legendVisible();
    const double previousScale =
        resultControlWidget_->deformationScale();
    const ResultScalarRangeMode previousRangeMode =
        resultControlWidget_->scalarRangeMode();
    const double previousRangeMinimum =
        resultControlWidget_->manualScalarMinimum();
    const double previousRangeMaximum =
        resultControlWidget_->manualScalarMaximum();
    const VtkMeshDisplayMode previousDisplayMode =
        vtkPostViewWidget_->displayMode();

    VtkResultSequenceLoadResult loadResult;
    {
        const WaitCursor waitCursor;
        const VtkResultSequenceLoader loader;
        loadResult = loader.load(*frameIterator);
    }
    if (!loadResult.success || loadResult.grid == nullptr) {
        const QString reason = loadResult.errorMessage.empty()
            ? tr("未知错误")
            : fromUtf8(loadResult.errorMessage);
        resultControlWidget_->selectFrame(loadedResultFrameNumber_);
        statusBar()->showMessage(tr("结果帧加载失败"), 5000);
        messageLog_->appendPlainText(
            tr("VTK 结果帧 %1 加载失败：%2")
                .arg(frameNumber).arg(reason));
        QMessageBox::critical(this, tr("结果帧加载失败"), reason);
        return false;
    }
    if (!vtkPostViewWidget_->displayResultGrid(
            loadResult.grid,
            postViewParts(loadResult, resultPartVisibility_), false)) {
        const QString reason = vtkPostViewWidget_->lastError().isEmpty()
            ? tr("未知错误")
            : vtkPostViewWidget_->lastError();
        resultControlWidget_->selectFrame(loadedResultFrameNumber_);
        QMessageBox::critical(this, tr("结果帧无法显示"), reason);
        return false;
    }

    loadedResultGrid_ = loadResult.grid;
    loadedResultFields_ = loadResult.fields;
    loadedResultFrameNumber_ = frameNumber;
    vtkPostViewWidget_->refreshFilters(frameNumber);
    currentResultScalarOption_.reset();
    currentResultStatistics_.reset();
    resultControlWidget_->setResultFields(loadedResultFields_);
    std::vector<int> frameNumbers;
    for (const auto& frame : loadedResultSequence_->frames) {
        frameNumbers.push_back(frame.frameNumber);
    }
    resultControlWidget_->setSequenceFrames(frameNumbers, frameNumber);
    resultControlWidget_->setManualScalarRange(
        previousRangeMinimum, previousRangeMaximum);
    resultControlWidget_->setScalarRangeMode(previousRangeMode);

    bool scalarRestored = false;
    if (previousScalar) {
        scalarRestored =
            resultControlWidget_->selectScalarOption(*previousScalar);
        if (!scalarRestored) {
            messageLog_->appendPlainText(
                tr("帧 %1 不包含先前云图字段，已使用当前帧默认字段。")
                    .arg(frameNumber));
        }
    }
    resultControlWidget_->setScalarVisible(previousScalarVisible);
    if (resultControlWidget_->scalarVisible()) {
        applySelectedResultScalar();
    } else {
        vtkPostViewWidget_->clearScalarField();
    }

    const bool displacementRestored =
        resultControlWidget_->selectDisplacementField(
            previousDisplacement);
    if (!previousDisplacement.empty() && !displacementRestored) {
        messageLog_->appendPlainText(
            tr("帧 %1 不包含先前位移字段，已关闭变形显示。")
                .arg(frameNumber));
    }
    updateSelectedDisplacementField();
    resultControlWidget_->setDeformationScaleValue(previousScale);
    vtkPostViewWidget_->setDeformationScale(previousScale);
    resultControlWidget_->setDeformationChecked(
        previousDeformationVisible && displacementRestored);
    if (previousDeformationVisible && displacementRestored) {
        setResultDeformationVisible(true);
    }
    resultControlWidget_->setLegendChecked(previousLegendVisible);
    vtkPostViewWidget_->setLegendVisible(previousLegendVisible);
    setPostDisplayMode(previousDisplayMode);

    if (QStandardItem* frameItem =
            resultSequenceFrameItems_.value(frameNumber, nullptr)) {
        projectTree_->setCurrentIndex(frameItem->index());
    }
    setWindowTitle(
        tr("QTCAE 仿真工作台 - 后处理 - %1 - 帧 %2")
            .arg(QFileInfo(loadedResultFilePath_).fileName())
            .arg(frameNumber));
    statusBar()->showMessage(
        tr("已切换到结果帧 %1").arg(frameNumber), 3000);
    messageLog_->appendPlainText(
        tr("已按需加载 VTK 结果帧 %1：节点 %2，单元 %3。")
            .arg(frameNumber)
            .arg(loadedResultGrid_->GetNumberOfPoints())
            .arg(loadedResultGrid_->GetNumberOfCells()));
    for (const std::string& warning : loadResult.warnings) {
        messageLog_->appendPlainText(
            tr("VTK 帧警告：%1").arg(fromUtf8(warning)));
    }
    rebuildFilterResultTree();
    return true;
}

void WorkbenchMainWindow::clearLoadedVtkResult(bool clearView) {
    if (resultAnimationTimer_ != nullptr) resultAnimationTimer_->stop();
    loadedResultFilePath_.clear();
    loadedResultGrid_ = nullptr;
    loadedResultFields_.clear();
    loadedResultSequence_.reset();
    loadedResultFrameNumber_ = -1;
    loadedResultIsStandaloneCollection_ = false;
    resultSequenceFrameItems_.clear();
    resultPartVisibility_.clear();
    currentResultScalarOption_.reset();
    currentResultStatistics_.reset();
    loadedResultItem_ = nullptr;
    loadedResultGridItem_ = nullptr;
    pointResultsRootItem_ = nullptr;
    cellResultsRootItem_ = nullptr;
    filterResultsRootItem_ = nullptr;
    resultControlWidget_->clearResult();
    resultControlWidget_->clearSequence();
    if (clearView) {
        vtkPostViewWidget_->clearScene();
    }
    if (resultRootItem_ != nullptr) {
        resultRootItem_->removeRows(0, resultRootItem_->rowCount());
        currentPostMeshRootItem_ = new QStandardItem(tr("当前网格"));
        currentPostMeshRootItem_->setEditable(false);
        resultRootItem_->appendRow(currentPostMeshRootItem_);
    }
}

void WorkbenchMainWindow::buildVtkResultTree() {
    resultRootItem_->removeRows(0, resultRootItem_->rowCount());
    currentPostMeshRootItem_ = nullptr;
    currentPostMeshItem_ = nullptr;

    loadedResultItem_ =
        new QStandardItem(QFileInfo(loadedResultFilePath_).fileName());
    loadedResultItem_->setEditable(false);
    loadedResultItem_->setData(
        static_cast<int>(ResultTreeNodeKind::File),
        ResultNodeKindRole);

    loadedResultGridItem_ = new QStandardItem(tr("网格"));
    loadedResultGridItem_->setEditable(false);
    loadedResultGridItem_->setData(
        static_cast<int>(ResultTreeNodeKind::Grid),
        ResultNodeKindRole);
    loadedResultItem_->appendRow(loadedResultGridItem_);

    pointResultsRootItem_ = new QStandardItem(tr("节点结果"));
    pointResultsRootItem_->setEditable(false);
    cellResultsRootItem_ = new QStandardItem(tr("单元结果"));
    cellResultsRootItem_->setEditable(false);
    loadedResultItem_->appendRow(pointResultsRootItem_);
    loadedResultItem_->appendRow(cellResultsRootItem_);

    for (int index = 0;
         index < static_cast<int>(loadedResultFields_.size());
         ++index) {
        const ResultFieldInfo& field =
            loadedResultFields_[static_cast<std::size_t>(index)];
        addVtkFieldTreeItem(
            field, index,
            field.association == ResultFieldAssociation::Point
                ? pointResultsRootItem_
                : cellResultsRootItem_);
    }
    filterResultsRootItem_ = new QStandardItem(tr("过滤结果"));
    filterResultsRootItem_->setEditable(false);
    filterResultsRootItem_->setData(
        static_cast<int>(ResultTreeNodeKind::FilterRoot),
        ResultNodeKindRole);
    loadedResultItem_->appendRow(filterResultsRootItem_);
    resultRootItem_->appendRow(loadedResultItem_);
    projectTree_->expand(resultRootItem_->index());
    projectTree_->expand(loadedResultItem_->index());
    projectTree_->expand(pointResultsRootItem_->index());
    projectTree_->expand(cellResultsRootItem_->index());
    projectTree_->setCurrentIndex(loadedResultItem_->index());
}

void WorkbenchMainWindow::buildVtkResultSequenceTree() {
    if (!loadedResultSequence_) {
        return;
    }
    resultRootItem_->removeRows(0, resultRootItem_->rowCount());
    currentPostMeshRootItem_ = nullptr;
    currentPostMeshItem_ = nullptr;
    resultSequenceFrameItems_.clear();

    const QString directoryName = loadedResultIsStandaloneCollection_
        ? tr("已导入 VTK 结果")
        : (QFileInfo(loadedResultFilePath_).fileName().isEmpty()
               ? loadedResultFilePath_
               : QFileInfo(loadedResultFilePath_).fileName());
    loadedResultItem_ = new QStandardItem(directoryName);
    loadedResultItem_->setEditable(false);
    loadedResultItem_->setData(
        static_cast<int>(ResultTreeNodeKind::Sequence),
        ResultNodeKindRole);
    resultRootItem_->appendRow(loadedResultItem_);

    QHash<int, QStandardItem*> partItems;
    for (const auto& frame : loadedResultSequence_->frames) {
        for (const auto& part : frame.parts) {
            const QString partName = fromUtf8(part.name);
            QStandardItem* partItem = partItems.value(part.id, nullptr);
            if (partItem == nullptr) {
                partItem = new QStandardItem(partName);
                partItem->setEditable(false);
                partItem->setData(
                    static_cast<int>(ResultTreeNodeKind::SequencePart),
                    ResultNodeKindRole);
                partItem->setData(partName, ResultPartNameRole);
                partItem->setData(part.id, ResultPartIdRole);
                partItem->setData(
                    QString::fromStdWString(part.filePath.wstring()),
                    ResultPartPathRole);
                partItem->setCheckable(true);
                partItem->setCheckState(
                    resultPartVisibility_.value(part.id, true)
                        ? Qt::Checked
                        : Qt::Unchecked);
                loadedResultItem_->appendRow(partItem);
                partItems.insert(part.id, partItem);
            }

        }
    }
    filterResultsRootItem_ = new QStandardItem(tr("过滤结果"));
    filterResultsRootItem_->setEditable(false);
    filterResultsRootItem_->setData(
        static_cast<int>(ResultTreeNodeKind::FilterRoot),
        ResultNodeKindRole);
    loadedResultItem_->appendRow(filterResultsRootItem_);
    projectTree_->expand(resultRootItem_->index());
    projectTree_->expand(loadedResultItem_->index());
    projectTree_->setCurrentIndex(loadedResultItem_->index());
}

void WorkbenchMainWindow::showVtkResultSequenceProperties() {
    if (!loadedResultSequence_) {
        showDefaultProperties();
        return;
    }
    const auto currentFrame = std::find_if(
        loadedResultSequence_->frames.begin(),
        loadedResultSequence_->frames.end(),
        [this](const VtkResultSequenceFrame& frame) {
            return frame.frameNumber == loadedResultFrameNumber_;
        });
    const std::size_t currentPartCount =
        currentFrame == loadedResultSequence_->frames.end()
        ? 0
        : currentFrame->parts.size();
    setPropertyRows({
        {tr("名称"), loadedResultIsStandaloneCollection_
                         ? tr("已导入 VTK 结果")
                         : QFileInfo(loadedResultFilePath_).fileName()},
        {tr("类型"), loadedResultIsStandaloneCollection_
                         ? tr("VTK 多部件结果")
                         : tr("VTK 结果序列")},
        {loadedResultIsStandaloneCollection_ ? tr("来源")
                                             : tr("结果目录"),
         loadedResultIsStandaloneCollection_ ? tr("多个独立文件")
                                             : loadedResultFilePath_},
        {tr("帧数"),
         QString::number(loadedResultSequence_->frames.size())},
        {tr("当前帧"), QString::number(loadedResultFrameNumber_)},
        {tr("当前对象数"), QString::number(currentPartCount)},
        {tr("节点数"),
         loadedResultGrid_ == nullptr
             ? QStringLiteral("0")
             : QString::number(
                   loadedResultGrid_->GetNumberOfPoints())},
        {tr("单元数"),
         loadedResultGrid_ == nullptr
             ? QStringLiteral("0")
             : QString::number(
                   loadedResultGrid_->GetNumberOfCells())},
        {tr("加载方式"), loadedResultIsStandaloneCollection_
                             ? tr("合并加载全部部件")
                             : tr("按需加载当前帧")}
    });
}

void WorkbenchMainWindow::showVtkResultSequenceFrameProperties(
    int frameNumber) {
    if (!loadedResultSequence_) {
        showDefaultProperties();
        return;
    }
    const auto iterator = std::find_if(
        loadedResultSequence_->frames.begin(),
        loadedResultSequence_->frames.end(),
        [frameNumber](const VtkResultSequenceFrame& frame) {
            return frame.frameNumber == frameNumber;
        });
    if (iterator == loadedResultSequence_->frames.end()) {
        showDefaultProperties();
        return;
    }
    QVector<QPair<QString, QString>> rows{
        {tr("名称"), tr("帧 %1").arg(frameNumber)},
        {tr("类型"), tr("VTK 结果序列帧")},
        {tr("对象文件数"), QString::number(iterator->parts.size())},
        {tr("当前已加载"),
         frameNumber == loadedResultFrameNumber_ ? tr("是") : tr("否")}
    };
    if (frameNumber == loadedResultFrameNumber_ &&
        loadedResultGrid_ != nullptr) {
        rows.push_back(
            {tr("节点数"),
             QString::number(loadedResultGrid_->GetNumberOfPoints())});
        rows.push_back(
            {tr("单元数"),
             QString::number(loadedResultGrid_->GetNumberOfCells())});
        rows.push_back(
            {tr("公共结果字段"),
             QString::number(loadedResultFields_.size())});
    }
    setPropertyRows(rows);
}

void WorkbenchMainWindow::showVtkResultSequencePartProperties(
    int partId) {
    if (!loadedResultSequence_) {
        showDefaultProperties();
        return;
    }
    int frameCount = 0;
    bool availableInCurrentFrame = false;
    QString partName;
    QString partPath;
    for (const auto& frame : loadedResultSequence_->frames) {
        const auto partIterator = std::find_if(
            frame.parts.begin(), frame.parts.end(),
            [partId](const VtkResultSequencePart& part) {
                return part.id == partId;
            });
        if (partIterator == frame.parts.end()) {
            continue;
        }
        partName = fromUtf8(partIterator->name);
        partPath = QString::fromStdWString(
            partIterator->filePath.wstring());
        ++frameCount;
        availableInCurrentFrame = availableInCurrentFrame ||
            frame.frameNumber == loadedResultFrameNumber_;
    }
    QVector<QPair<QString, QString>> rows{
        {tr("名称"), partName},
        {tr("类型"), loadedResultIsStandaloneCollection_
                         ? tr("VTK 结果部件")
                         : tr("VTK 结果序列部件")},
        {tr("部件 ID"), QString::number(partId)},
        {tr("路径"), partPath},
        {tr("可见"), resultPartVisibility_.value(partId, true)
                          ? tr("是") : tr("否")},
    };
    if (!loadedResultIsStandaloneCollection_) {
        rows.push_back({tr("包含帧数"), QString::number(frameCount)});
        rows.push_back({tr("当前帧可用"),
                        availableInCurrentFrame ? tr("是") : tr("否")});
        rows.push_back({tr("加载方式"), tr("随所选帧按需加载")});
    }
    setPropertyRows(rows);
}

void WorkbenchMainWindow::showVtkResultSequencePartFrameProperties(
    int frameNumber, const QString& partName,
    const QString& filePath) {
    setPropertyRows({
        {tr("名称"), QFileInfo(filePath).fileName()},
        {tr("类型"), tr("VTK 结果序列部件帧")},
        {tr("帧编号"), QString::number(frameNumber)},
        {tr("对象"), partName},
        {tr("路径"), filePath},
        {tr("加载状态"),
         frameNumber == loadedResultFrameNumber_
             ? tr("随当前帧加载")
             : tr("未加载")}
    });
}

void WorkbenchMainWindow::addVtkFieldTreeItem(
    const ResultFieldInfo& field, int fieldIndex,
    QStandardItem* parent) {
    if (parent == nullptr) {
        return;
    }
    const ResultFieldProcessor processor;
    const auto options = processor.scalarOptions(field);
    auto* fieldItem = new QStandardItem(fromUtf8(field.name));
    fieldItem->setEditable(false);
    fieldItem->setData(
        static_cast<int>(ResultTreeNodeKind::Field),
        ResultNodeKindRole);
    fieldItem->setData(fieldIndex, ResultFieldIndexRole);
    fieldItem->setData(fromUtf8(field.name), ResultFieldNameRole);
    fieldItem->setData(static_cast<int>(field.association),
                       ResultAssociationRole);

    const auto setOptionData =
        [fieldIndex](QStandardItem* item,
                     const ResultScalarOption& option,
                     int optionIndex) {
            item->setData(fieldIndex, ResultFieldIndexRole);
            item->setData(optionIndex, ResultOptionIndexRole);
            item->setData(fromUtf8(option.sourceArrayName),
                          ResultFieldNameRole);
            item->setData(static_cast<int>(option.association),
                          ResultAssociationRole);
            item->setData(static_cast<int>(option.operation),
                          ResultOperationRole);
            item->setData(option.component, ResultComponentRole);
        };

    if (!options.empty()) {
        int defaultOption = 0;
        if (field.componentCount == 3 || field.stressTensor) {
            defaultOption = static_cast<int>(options.size()) - 1;
        }
        setOptionData(
            fieldItem,
            options[static_cast<std::size_t>(defaultOption)],
            defaultOption);
    }
    if (options.size() > 1) {
        for (int index = 0; index < static_cast<int>(options.size());
             ++index) {
            const ResultScalarOption& option =
                options[static_cast<std::size_t>(index)];
            auto* optionItem =
                new QStandardItem(fromUtf8(option.displayName));
            optionItem->setEditable(false);
            optionItem->setData(
                static_cast<int>(ResultTreeNodeKind::ScalarOption),
                ResultNodeKindRole);
            setOptionData(optionItem, option, index);
            fieldItem->appendRow(optionItem);
        }
    }
    parent->appendRow(fieldItem);
}

void WorkbenchMainWindow::applySelectedResultScalar() {
    const auto option = resultControlWidget_->selectedScalarOption();
    if (!option) {
        vtkPostViewWidget_->clearScalarField();
        currentResultScalarOption_.reset();
        currentResultStatistics_.reset();
        showLoadedResultProperties();
        return;
    }
    showResultScalar(*option);
}

bool WorkbenchMainWindow::applySelectedScalarRange(bool announce) {
    if (loadedResultGrid_ == nullptr ||
        !currentResultScalarOption_ ||
        !vtkPostViewWidget_->scalarFieldVisible()) {
        return false;
    }

    bool success = false;
    QString modeName;
    if (resultControlWidget_->scalarRangeMode() ==
        ResultScalarRangeMode::Automatic) {
        modeName = tr("自动");
        success = vtkPostViewWidget_->useAutomaticScalarRange();
    } else {
        modeName = tr("手动");
        const double minimum =
            resultControlWidget_->manualScalarMinimum();
        const double maximum =
            resultControlWidget_->manualScalarMaximum();
        const ResultFieldProcessor processor;
        const ResultValidationResult validation =
            processor.validateScalarRange(minimum, maximum);
        if (validation.success) {
            success = vtkPostViewWidget_->setManualScalarRange(
                minimum, maximum);
        } else {
            const QString reason = fromUtf8(validation.errorMessage);
            resultControlWidget_->setManualScalarRange(
                vtkPostViewWidget_->scalarRangeMinimum(),
                vtkPostViewWidget_->scalarRangeMaximum());
            statusBar()->showMessage(tr("云图范围设置失败"), 5000);
            messageLog_->appendPlainText(
                tr("云图范围设置失败：%1").arg(reason));
            if (announce) {
                QMessageBox::warning(
                    this, tr("云图范围无效"), reason);
            }
            return false;
        }
    }

    if (!success) {
        const QString reason = vtkPostViewWidget_->lastError();
        statusBar()->showMessage(tr("云图范围设置失败"), 5000);
        messageLog_->appendPlainText(
            tr("云图范围设置失败：%1").arg(reason));
        if (announce) {
            QMessageBox::warning(
                this, tr("云图范围设置失败"), reason);
        }
        return false;
    }

    showCurrentResultScalarProperties();
    if (announce) {
        const QString rangeText = tr("[%1, %2]")
            .arg(vtkPostViewWidget_->scalarRangeMinimum(), 0, 'g', 12)
            .arg(vtkPostViewWidget_->scalarRangeMaximum(), 0, 'g', 12);
        statusBar()->showMessage(
            tr("云图范围：%1 %2").arg(modeName, rangeText), 5000);
        messageLog_->appendPlainText(
            tr("已应用%1云图范围：%2").arg(modeName, rangeText));
    }
    return true;
}

bool WorkbenchMainWindow::showResultScalar(
    const ResultScalarOption& option) {
    if (loadedResultGrid_ == nullptr) {
        return false;
    }
    const ResultFieldProcessor processor;
    const ResultScalarBuildResult build =
        processor.buildScalar(loadedResultGrid_, option);
    if (!build.success) {
        const QString reason = build.errorMessage.empty()
            ? tr("未知错误")
            : fromUtf8(build.errorMessage);
        statusBar()->showMessage(tr("结果云图显示失败"), 5000);
        messageLog_->appendPlainText(
            tr("结果云图显示失败：%1").arg(reason));
        return false;
    }
    const std::string displayName =
        option.displayName == option.sourceArrayName
        ? option.sourceArrayName
        : option.sourceArrayName + "." + option.displayName;
    if (!vtkPostViewWidget_->showScalarField(
            build.arrayName, option.association, 0, displayName)) {
        const QString reason = vtkPostViewWidget_->lastError();
        statusBar()->showMessage(tr("结果云图显示失败"), 5000);
        messageLog_->appendPlainText(
            tr("结果云图显示失败：%1").arg(reason));
        return false;
    }
    currentResultScalarOption_ = option;
    currentResultStatistics_ = build.statistics;
    resultControlWidget_->setAutomaticScalarRange(
        vtkPostViewWidget_->scalarRangeMinimum(),
        vtkPostViewWidget_->scalarRangeMaximum());
    if (!applySelectedScalarRange(false)) {
        return false;
    }
    for (const std::string& warning : build.warnings) {
        messageLog_->appendPlainText(
            tr("结果字段警告：%1").arg(fromUtf8(warning)));
    }
    showCurrentResultScalarProperties();
    statusBar()->showMessage(
        tr("当前结果：%1").arg(fromUtf8(displayName)), 3000);
    return true;
}

void WorkbenchMainWindow::updateSelectedDisplacementField() {
    if (loadedResultGrid_ == nullptr) {
        return;
    }
    const std::string name =
        resultControlWidget_->selectedDisplacementField();
    if (name.empty()) {
        vtkPostViewWidget_->clearDisplacementField();
        resultControlWidget_->setDeformationChecked(false);
        showCurrentResultScalarProperties();
        return;
    }
    const ResultFieldProcessor processor;
    const ResultValidationResult validation =
        processor.validateDisplacementField(loadedResultGrid_, name);
    if (!validation.success ||
        !vtkPostViewWidget_->setDisplacementField(name)) {
        const QString reason = !validation.success
            ? fromUtf8(validation.errorMessage)
            : vtkPostViewWidget_->lastError();
        resultControlWidget_->setDeformationChecked(false);
        messageLog_->appendPlainText(
            tr("位移字段不可用：%1").arg(reason));
        statusBar()->showMessage(tr("位移字段不可用"), 5000);
        return;
    }
    if (resultControlWidget_->deformationVisible()) {
        setResultDeformationVisible(true);
    }
    showCurrentResultScalarProperties();
}

void WorkbenchMainWindow::setResultDeformationVisible(bool visible) {
    if (loadedResultGrid_ == nullptr) {
        return;
    }
    if (!vtkPostViewWidget_->setDeformationEnabled(visible)) {
        resultControlWidget_->setDeformationChecked(false);
        messageLog_->appendPlainText(
            tr("位移变形显示失败：%1")
                .arg(vtkPostViewWidget_->lastError()));
        statusBar()->showMessage(tr("位移变形显示失败"), 5000);
        return;
    }
    showCurrentResultScalarProperties();
    statusBar()->showMessage(
        visible ? tr("已显示位移变形")
                : tr("已关闭位移变形"),
        3000);
}

void WorkbenchMainWindow::setResultDeformationScale(double scale) {
    if (loadedResultGrid_ == nullptr) {
        return;
    }
    const ResultFieldProcessor processor;
    const ResultValidationResult validation =
        processor.validateDeformationScale(scale);
    if (!validation.success ||
        !vtkPostViewWidget_->setDeformationScale(scale)) {
        const QString reason = !validation.success
            ? fromUtf8(validation.errorMessage)
            : vtkPostViewWidget_->lastError();
        messageLog_->appendPlainText(
            tr("变形倍率更新失败：%1").arg(reason));
        statusBar()->showMessage(tr("变形倍率更新失败"), 5000);
        return;
    }
    showCurrentResultScalarProperties();
    statusBar()->showMessage(
        tr("变形倍率：%1").arg(scale, 0, 'g', 10), 3000);
}

void WorkbenchMainWindow::showLoadedResultProperties() {
    if (loadedResultSequence_) {
        showVtkResultSequenceFrameProperties(
            loadedResultFrameNumber_);
        return;
    }
    if (loadedResultGrid_ == nullptr) {
        showDefaultProperties();
        return;
    }
    const qlonglong pointFields = static_cast<qlonglong>(
        std::count_if(
        loadedResultFields_.begin(), loadedResultFields_.end(),
        [](const ResultFieldInfo& field) {
            return field.association == ResultFieldAssociation::Point;
        }));
    const qlonglong cellFields =
        static_cast<qlonglong>(loadedResultFields_.size()) -
        pointFields;
    setPropertyRows({
        {tr("名称"), QFileInfo(loadedResultFilePath_).fileName()},
        {tr("类型"),
         QFileInfo(loadedResultFilePath_).suffix().compare(
             QStringLiteral("vtk"), Qt::CaseInsensitive) == 0
             ? tr("VTK Legacy 非结构网格结果")
             : tr("VTK XML 非结构网格结果")},
        {tr("路径"), loadedResultFilePath_},
        {tr("节点数"),
         QString::number(loadedResultGrid_->GetNumberOfPoints())},
        {tr("单元数"),
         QString::number(loadedResultGrid_->GetNumberOfCells())},
        {tr("节点结果字段"), QString::number(pointFields)},
        {tr("单元结果字段"), QString::number(cellFields)}
    });
}

void WorkbenchMainWindow::showResultGridProperties() {
    if (loadedResultGrid_ == nullptr) {
        showDefaultProperties();
        return;
    }
    setPropertyRows({
        {tr("名称"), tr("网格")},
        {tr("类型"), tr("VTK 非结构网格")},
        {tr("节点数"),
         QString::number(loadedResultGrid_->GetNumberOfPoints())},
        {tr("单元数"),
         QString::number(loadedResultGrid_->GetNumberOfCells())},
        {tr("显示模式"), postDisplayModeName()}
    });
}

void WorkbenchMainWindow::showResultFieldProperties(int fieldIndex) {
    if (fieldIndex < 0 ||
        fieldIndex >= static_cast<int>(loadedResultFields_.size())) {
        showLoadedResultProperties();
        return;
    }
    const ResultFieldInfo& field =
        loadedResultFields_[static_cast<std::size_t>(fieldIndex)];
    setPropertyRows({
        {tr("名称"), fromUtf8(field.name)},
        {tr("关联位置"), resultAssociationName(field.association)},
        {tr("分量数量"), QString::number(field.componentCount)},
        {tr("元组数量"), QString::number(field.tupleCount)},
        {tr("应力张量"), field.stressTensor ? tr("是") : tr("否")}
    });
}

void WorkbenchMainWindow::showCurrentResultScalarProperties() {
    if (loadedResultGrid_ == nullptr) {
        showDefaultProperties();
        return;
    }
    if (!currentResultScalarOption_ || !currentResultStatistics_) {
        showLoadedResultProperties();
        return;
    }
    const ResultScalarOption& option = *currentResultScalarOption_;
    const ResultScalarStatistics& statistics = *currentResultStatistics_;
    const QString displayName =
        option.displayName == option.sourceArrayName
        ? fromUtf8(option.sourceArrayName)
        : tr("%1.%2").arg(fromUtf8(option.sourceArrayName),
                          fromUtf8(option.displayName));
    QVector<QPair<QString, QString>> rows{
        {tr("结果名称"), displayName},
        {tr("关联位置"), resultAssociationName(option.association)},
        {tr("最小值"),
         QString::number(statistics.minimum, 'g', 12)},
        {tr("最大值"),
         QString::number(statistics.maximum, 'g', 12)},
        {tr("云图范围模式"),
         resultControlWidget_->scalarRangeMode() ==
                 ResultScalarRangeMode::Automatic
             ? tr("自动")
             : tr("手动")},
        {tr("云图显示范围"),
         tr("[%1, %2]")
             .arg(vtkPostViewWidget_->scalarRangeMinimum(), 0, 'g', 12)
             .arg(vtkPostViewWidget_->scalarRangeMaximum(), 0, 'g', 12)},
        {tr("单位"), tr("未提供")},
        {tr("最小值 ID"),
         QString::number(statistics.minimumId)},
        {tr("最大值 ID"),
         QString::number(statistics.maximumId)},
        {tr("显示模式"), postDisplayModeName()},
        {tr("位移变形"),
         vtkPostViewWidget_->deformationEnabled()
             ? tr("开启")
             : tr("关闭")},
        {tr("变形倍率"),
         QString::number(vtkPostViewWidget_->deformationScale(),
                         'g', 12)},
        {tr("图例"),
         vtkPostViewWidget_->legendVisible()
             ? tr("显示")
             : tr("隐藏")}
    };
    if (statistics.hasPositions) {
        rows.append({
            tr("最小值坐标"),
            tr("(%1, %2, %3)")
                .arg(statistics.minimumPosition[0], 0, 'g', 10)
                .arg(statistics.minimumPosition[1], 0, 'g', 10)
                .arg(statistics.minimumPosition[2], 0, 'g', 10)});
        rows.append({
            tr("最大值坐标"),
            tr("(%1, %2, %3)")
                .arg(statistics.maximumPosition[0], 0, 'g', 10)
                .arg(statistics.maximumPosition[1], 0, 'g', 10)
                .arg(statistics.maximumPosition[2], 0, 'g', 10)});
    }
    if (statistics.ignoredValueCount > 0) {
        rows.append({
            tr("忽略无效值"),
            QString::number(statistics.ignoredValueCount)});
    }
    setPropertyRows(rows);
}

std::optional<ResultScalarOption>
WorkbenchMainWindow::resultScalarOption(
    const QStandardItem* item) const {
    if (item == nullptr) {
        return std::nullopt;
    }
    bool fieldIndexValid = false;
    bool optionIndexValid = false;
    const int fieldIndex =
        item->data(ResultFieldIndexRole).toInt(&fieldIndexValid);
    const int optionIndex =
        item->data(ResultOptionIndexRole).toInt(&optionIndexValid);
    if (!fieldIndexValid || !optionIndexValid) {
        return std::nullopt;
    }
    if (fieldIndex < 0 ||
        fieldIndex >= static_cast<int>(loadedResultFields_.size())) {
        return std::nullopt;
    }
    const ResultFieldProcessor processor;
    const auto options = processor.scalarOptions(
        loadedResultFields_[static_cast<std::size_t>(fieldIndex)]);
    if (optionIndex < 0 ||
        optionIndex >= static_cast<int>(options.size())) {
        return std::nullopt;
    }
    return options[static_cast<std::size_t>(optionIndex)];
}

void WorkbenchMainWindow::displayMeshInPostprocessing(int meshId) {
    const int requestedMeshId = meshId >= 0 ? meshId
                                            : selectedMeshObjectId_;
    const MeshObject* mesh = occViewWidget_->findMesh(requestedMeshId);
    if (mesh == nullptr) {
        QMessageBox::information(
            this, tr("后处理显示"),
            tr("请先在工程树中选择需要显示的网格。"));
        return;
    }

    if (!vtkPostViewWidget_->displayMesh(mesh->data)) {
        const QString reason = vtkPostViewWidget_->lastError().isEmpty()
            ? tr("未知错误")
            : vtkPostViewWidget_->lastError();
        statusBar()->showMessage(tr("网格无法显示"), 5000);
        messageLog_->appendPlainText(
            tr("网格无法在后处理视窗中显示：%1\n原因：%2")
                .arg(mesh->name, reason));
        QMessageBox::critical(
            this, tr("网格无法显示"),
            tr("网格无法显示：%1").arg(reason));
        return;
    }

    clearLoadedVtkResult(false);
    currentPostMeshId_ = requestedMeshId;
    if (currentPostMeshItem_ != nullptr &&
        currentPostMeshItem_->parent() != nullptr) {
        currentPostMeshItem_->parent()->removeRow(
            currentPostMeshItem_->row());
    }
    currentPostMeshItem_ = new QStandardItem(mesh->name);
    currentPostMeshItem_->setEditable(false);
    currentPostMeshItem_->setData(requestedMeshId,
                                  PostMeshObjectIdRole);
    currentPostMeshRootItem_->appendRow(currentPostMeshItem_);
    projectTree_->expand(resultRootItem_->index());
    projectTree_->expand(currentPostMeshRootItem_->index());

    surfaceWithEdgesAction_->setChecked(true);
    switchToPostprocessing();
    projectTree_->setCurrentIndex(currentPostMeshItem_->index());
    showPostprocessingMeshProperties(requestedMeshId);
    setWindowTitle(tr("QTCAE 仿真工作台 - 后处理 - %1")
                       .arg(mesh->name));
    statusBar()->showMessage(tr("后处理：%1").arg(mesh->name), 5000);
    messageLog_->appendPlainText(
        tr("已在后处理视窗中显示网格：%1；节点 %2，四面体 %3。")
            .arg(mesh->name)
            .arg(mesh->data.nodes.size())
            .arg(mesh->data.tetrahedra.size()));
}

void WorkbenchMainWindow::clearPostprocessingMeshIfMatches(int meshId) {
    if (meshId < 0 || meshId != currentPostMeshId_) {
        return;
    }
    vtkPostViewWidget_->clearScene();
    if (currentPostMeshItem_ != nullptr &&
        currentPostMeshItem_->parent() != nullptr) {
        currentPostMeshItem_->parent()->removeRow(
            currentPostMeshItem_->row());
    }
    currentPostMeshItem_ = nullptr;
    currentPostMeshId_ = -1;
    updatePostViewActionStates();
    if (workspaceStack_->currentIndex() == 1) {
        showDefaultProperties();
        setWindowTitle(tr("QTCAE 仿真工作台"));
        statusBar()->showMessage(tr("当前后处理网格已清除"), 3000);
    }
}

void WorkbenchMainWindow::showPostprocessingMeshProperties(int meshId) {
    const MeshObject* mesh = occViewWidget_->findMesh(meshId);
    if (mesh == nullptr || meshId != currentPostMeshId_) {
        showDefaultProperties();
        return;
    }
    setPropertyRows({
        {tr("名称"), mesh->name},
        {tr("类型"), tr("四面体网格")},
        {tr("节点数"), QString::number(mesh->data.nodes.size())},
        {tr("四面体数"), QString::number(mesh->data.tetrahedra.size())},
        {tr("显示模式"), postDisplayModeName()},
        {tr("结果数据"), tr("未加载")}
    });
}

void WorkbenchMainWindow::setVtkResultPartVisible(
    QStandardItem* item, bool visible) {
    if (item == nullptr) return;
    const int partId = item->data(ResultPartIdRole).toInt();
    if (partId < 0) return;
    resultPartVisibility_.insert(partId, visible);
    vtkPostViewWidget_->setPartVisible(partId, visible);
    showVtkResultSequencePartProperties(partId);
    statusBar()->showMessage(
        visible ? tr("已显示部件：%1").arg(item->text())
                : tr("已隐藏部件：%1").arg(item->text()), 3000);
}

void WorkbenchMainWindow::deleteVtkResultPart(int partId) {
    if (!loadedResultSequence_) return;
    QString partName;
    int referencedFrameCount = 0;
    for (const auto& frame : loadedResultSequence_->frames) {
        const auto part = std::find_if(
            frame.parts.begin(), frame.parts.end(),
            [partId](const VtkResultSequencePart& candidate) {
                return candidate.id == partId;
            });
        if (part == frame.parts.end()) continue;
        if (partName.isEmpty()) partName = fromUtf8(part->name);
        ++referencedFrameCount;
    }
    if (partName.isEmpty()) return;

    const QString scope = loadedResultIsStandaloneCollection_
        ? tr("当前结果")
        : tr("结果序列的 %1 个帧").arg(referencedFrameCount);
    if (QMessageBox::question(
            this, tr("删除 VTK 结果部件"),
            tr("确定要从%1中删除部件“%2”吗？\n"
               "此操作不会删除磁盘上的原始文件。")
                .arg(scope, partName)) != QMessageBox::Yes) {
        return;
    }

    VtkResultSequence candidate = *loadedResultSequence_;
    for (auto& frame : candidate.frames) {
        std::erase_if(frame.parts,
                      [partId](const VtkResultSequencePart& item) {
                          return item.id == partId;
                      });
    }
    std::erase_if(candidate.frames,
                  [](const VtkResultSequenceFrame& frame) {
                      return frame.parts.empty();
                  });
    if (candidate.frames.empty()) {
        clearLoadedVtkResult(true);
        updatePostViewActionStates();
        showDefaultProperties();
        setWindowTitle(tr("QTCAE 仿真工作台"));
        statusBar()->showMessage(tr("已删除最后一个 VTK 结果部件"), 5000);
        messageLog_->appendPlainText(
            tr("已从当前结果中删除部件：%1。原始文件未删除。").arg(partName));
        return;
    }

    const auto previousScalar = resultControlWidget_->selectedScalarOption();
    const std::string previousDisplacement =
        resultControlWidget_->selectedDisplacementField();
    const bool previousScalarVisible = resultControlWidget_->scalarVisible();
    const bool previousDeformationVisible =
        resultControlWidget_->deformationVisible();
    const bool previousLegendVisible = resultControlWidget_->legendVisible();
    const double previousScale = resultControlWidget_->deformationScale();
    const ResultScalarRangeMode previousRangeMode =
        resultControlWidget_->scalarRangeMode();
    const double previousRangeMinimum =
        resultControlWidget_->manualScalarMinimum();
    const double previousRangeMaximum =
        resultControlWidget_->manualScalarMaximum();
    const VtkMeshDisplayMode previousDisplayMode =
        vtkPostViewWidget_->displayMode();

    auto targetFrame = std::find_if(
        candidate.frames.begin(), candidate.frames.end(),
        [this](const VtkResultSequenceFrame& frame) {
            return frame.frameNumber == loadedResultFrameNumber_;
        });
    if (targetFrame == candidate.frames.end())
        targetFrame = candidate.frames.begin();
    const int targetFrameNumber = targetFrame->frameNumber;

    VtkResultSequenceLoadResult loaded;
    {
        const WaitCursor waitCursor;
        const VtkResultSequenceLoader loader;
        loaded = loader.load(*targetFrame);
    }
    if (!loaded.success || loaded.grid == nullptr) {
        const QString reason = loaded.errorMessage.empty()
            ? tr("剩余部件无法重新加载。")
            : fromUtf8(loaded.errorMessage);
        QMessageBox::critical(this, tr("删除部件失败"), reason);
        messageLog_->appendPlainText(
            tr("删除 VTK 结果部件“%1”失败：%2").arg(partName, reason));
        return;
    }

    QHash<int, bool> remainingVisibility = resultPartVisibility_;
    remainingVisibility.remove(partId);
    if (!vtkPostViewWidget_->displayResultGrid(
            loaded.grid, postViewParts(loaded, remainingVisibility), false)) {
        const QString reason = vtkPostViewWidget_->lastError().isEmpty()
            ? tr("剩余部件无法显示。")
            : vtkPostViewWidget_->lastError();
        QMessageBox::critical(this, tr("删除部件失败"), reason);
        return;
    }

    loadedResultSequence_ = std::move(candidate);
    resultPartVisibility_ = remainingVisibility;
    loadedResultGrid_ = loaded.grid;
    loadedResultFields_ = loaded.fields;
    loadedResultFrameNumber_ = targetFrameNumber;
    currentResultScalarOption_.reset();
    currentResultStatistics_.reset();
    resultControlWidget_->setResultFields(loadedResultFields_);
    std::vector<int> frameNumbers;
    frameNumbers.reserve(loadedResultSequence_->frames.size());
    for (const auto& frame : loadedResultSequence_->frames)
        frameNumbers.push_back(frame.frameNumber);
    resultControlWidget_->setSequenceFrames(
        frameNumbers, loadedResultFrameNumber_);
    resultControlWidget_->setManualScalarRange(
        previousRangeMinimum, previousRangeMaximum);
    resultControlWidget_->setScalarRangeMode(previousRangeMode);
    buildVtkResultSequenceTree();

    if (previousScalar)
        resultControlWidget_->selectScalarOption(*previousScalar);
    resultControlWidget_->setScalarVisible(previousScalarVisible);
    if (previousScalarVisible)
        applySelectedResultScalar();
    else
        vtkPostViewWidget_->clearScalarField();

    const bool displacementRestored =
        resultControlWidget_->selectDisplacementField(previousDisplacement);
    updateSelectedDisplacementField();
    resultControlWidget_->setDeformationScaleValue(previousScale);
    vtkPostViewWidget_->setDeformationScale(previousScale);
    resultControlWidget_->setDeformationChecked(
        previousDeformationVisible && displacementRestored);
    if (previousDeformationVisible && displacementRestored)
        setResultDeformationVisible(true);
    resultControlWidget_->setLegendChecked(previousLegendVisible);
    vtkPostViewWidget_->setLegendVisible(previousLegendVisible);
    setPostDisplayMode(previousDisplayMode);
    vtkPostViewWidget_->refreshFilters(loadedResultFrameNumber_);
    rebuildFilterResultTree();

    const qlonglong remainingCount = static_cast<qlonglong>(
        loadedResultSequence_->frames.front().parts.size());
    if (loadedResultIsStandaloneCollection_) {
        setWindowTitle(tr("QTCAE 仿真工作台 - 后处理 - %1 个部件")
                           .arg(remainingCount));
    } else {
        setWindowTitle(
            tr("QTCAE 仿真工作台 - 后处理 - %1 - 帧 %2")
                .arg(QFileInfo(loadedResultFilePath_).fileName())
                .arg(loadedResultFrameNumber_));
    }
    statusBar()->showMessage(tr("VTK 结果部件已删除"), 5000);
    messageLog_->appendPlainText(
        tr("已从当前结果中删除部件：%1；剩余 %2 个部件。原始文件未删除。")
            .arg(partName)
            .arg(remainingCount));
    for (const std::string& warning : loaded.warnings)
        messageLog_->appendPlainText(tr("VTK 警告：%1").arg(fromUtf8(warning)));
}

void WorkbenchMainWindow::savePostViewImage() {
    if (workspaceStack_->currentIndex() != 1 ||
        !vtkPostViewWidget_->hasMesh()) {
        QMessageBox::information(this, tr("保存视图"),
                                 tr("当前没有可导出的后处理视图。"));
        return;
    }
    QString filePath = QFileDialog::getSaveFileName(
        this, tr("保存当前视图为图片"), {}, tr("PNG 图片 (*.png)"));
    if (filePath.isEmpty()) return;
    if (!filePath.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive))
        filePath += QStringLiteral(".png");
    if (!vtkPostViewWidget_->saveCurrentViewToPng(filePath)) {
        QMessageBox::critical(this, tr("PNG 导出失败"),
                              vtkPostViewWidget_->lastError());
        statusBar()->showMessage(tr("PNG 图片导出失败"), 5000);
        return;
    }
    statusBar()->showMessage(tr("PNG 图片导出成功"), 5000);
    messageLog_->appendPlainText(tr("已保存当前后处理视图：%1").arg(filePath));
}

void WorkbenchMainWindow::choosePostBackgroundColor() {
    const QColor selected = QColorDialog::getColor(
        vtkPostViewWidget_->backgroundColor(), this,
        tr("选择后处理背景颜色"));
    if (!selected.isValid()) return;
    vtkPostViewWidget_->setBackgroundColor(selected);
    statusBar()->showMessage(tr("后处理背景颜色已更新"), 3000);
}

void WorkbenchMainWindow::choosePostPointSize() {
    bool accepted = false;
    const double size = QInputDialog::getDouble(
        this, tr("设置点大小"), tr("点大小（像素）"),
        vtkPostViewWidget_->pointSize(), 1.0, 50.0, 1, &accepted);
    if (!accepted) return;
    if (!vtkPostViewWidget_->setPointSize(size)) {
        QMessageBox::critical(this, tr("点大小设置失败"),
                              vtkPostViewWidget_->lastError());
        return;
    }
    statusBar()->showMessage(
        tr("后处理点大小已设置为 %1 像素").arg(size), 3000);
}

void WorkbenchMainWindow::createClipResult() {
    if (loadedResultGrid_ == nullptr) return;
    PlaneFilterDialog dialog(true, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const int number = static_cast<int>(vtkPostViewWidget_->filterResults().size()) + 1;
    const int id = vtkPostViewWidget_->createClipFilter(
        toUtf8(tr("轴向剖切-%1").arg(number)), dialog.axis(),
        dialog.position(), dialog.keepPositive(), loadedResultFrameNumber_);
    if (id < 0) {
        QMessageBox::information(this, tr("剖切结果为空"),
                                 vtkPostViewWidget_->lastError().isEmpty()
                                     ? tr("剖切平面没有得到有效输出。")
                                     : vtkPostViewWidget_->lastError());
        return;
    }
    rebuildFilterResultTree();
}

void WorkbenchMainWindow::createSliceResult() {
    if (loadedResultGrid_ == nullptr) return;
    PlaneFilterDialog dialog(false, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const int number = static_cast<int>(vtkPostViewWidget_->filterResults().size()) + 1;
    const int id = vtkPostViewWidget_->createSliceFilter(
        toUtf8(tr("平面切片-%1").arg(number)), dialog.axis(),
        dialog.position(), loadedResultFrameNumber_);
    if (id < 0) {
        QMessageBox::information(this, tr("切片结果为空"),
                                 tr("切片平面与当前结果没有交集。"));
        return;
    }
    rebuildFilterResultTree();
}

void WorkbenchMainWindow::createThresholdResult() {
    if (loadedResultGrid_ == nullptr) return;
    std::vector<ResultScalarOption> options;
    QStringList names;
    QVector<QPair<double, double>> ranges;
    const ResultFieldProcessor processor;
    for (const auto& field : loadedResultFields_) {
        for (const auto& option : processor.scalarOptions(field)) {
            const auto built = processor.buildScalar(loadedResultGrid_, option);
            if (!built.success) continue;
            options.push_back(option);
            names.push_back(fromUtf8(option.displayName));
            ranges.push_back({built.statistics.minimum,
                              built.statistics.maximum});
        }
    }
    if (options.empty()) {
        QMessageBox::information(this, tr("无法创建阈值"),
                                 tr("当前结果没有有效标量字段。"));
        return;
    }
    ThresholdFilterDialog dialog(names, ranges, 0, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const int index = dialog.fieldIndex();
    const auto& option = options[static_cast<std::size_t>(index)];
    const auto built = processor.buildScalar(loadedResultGrid_, option);
    if (!built.success) return;
    const int number = static_cast<int>(vtkPostViewWidget_->filterResults().size()) + 1;
    const int id = vtkPostViewWidget_->createThresholdFilter(
        toUtf8(tr("%1 阈值-%2").arg(fromUtf8(option.displayName)).arg(number)),
        built.arrayName, option.association, 0,
        dialog.minimum(), dialog.maximum(), loadedResultFrameNumber_);
    if (id < 0) {
        QMessageBox::information(this, tr("阈值结果为空"),
                                 tr("当前范围没有筛选出有效单元。"));
        return;
    }
    showResultScalar(option);
    rebuildFilterResultTree();
}

void WorkbenchMainWindow::createPointToCellResult() {
    if (loadedResultGrid_ == nullptr) return;
    const int number = static_cast<int>(vtkPostViewWidget_->filterResults().size()) + 1;
    vtkPostViewWidget_->createPointToCellFilter(
        toUtf8(tr("节点转单元-%1").arg(number)), loadedResultFrameNumber_);
    rebuildFilterResultTree();
}

void WorkbenchMainWindow::createCellToPointResult() {
    if (loadedResultGrid_ == nullptr) return;
    const int number = static_cast<int>(vtkPostViewWidget_->filterResults().size()) + 1;
    vtkPostViewWidget_->createCellToPointFilter(
        toUtf8(tr("单元转节点-%1").arg(number)), loadedResultFrameNumber_);
    rebuildFilterResultTree();
}

void WorkbenchMainWindow::rebuildFilterResultTree() {
    if (filterResultsRootItem_ == nullptr) return;
    filterResultsRootItem_->removeRows(0, filterResultsRootItem_->rowCount());
    for (const auto& filter : vtkPostViewWidget_->filterResults()) {
        auto* item = new QStandardItem(fromUtf8(filter.name));
        item->setEditable(false);
        item->setCheckable(true);
        item->setCheckState(filter.visible ? Qt::Checked : Qt::Unchecked);
        item->setData(static_cast<int>(ResultTreeNodeKind::FilterObject),
                      ResultNodeKindRole);
        item->setData(filter.id, FilterResultIdRole);
        if (!filter.valid) item->setText(item->text() + tr("（需要更新）"));
        filterResultsRootItem_->appendRow(item);
    }
    projectTree_->expand(filterResultsRootItem_->index());
}

void WorkbenchMainWindow::showFilterResultProperties(int filterId) {
    const auto filters = vtkPostViewWidget_->filterResults();
    const auto it = std::find_if(filters.begin(), filters.end(),
        [filterId](const auto& filter) { return filter.id == filterId; });
    if (it == filters.end()) { showDefaultProperties(); return; }
    QString type;
    switch (it->type) {
    case PostFilterType::Clip: type = tr("剖切"); break;
    case PostFilterType::Slice: type = tr("平面切片"); break;
    case PostFilterType::Threshold: type = tr("阈值"); break;
    case PostFilterType::PointToCell: type = tr("节点转单元"); break;
    case PostFilterType::CellToPoint: type = tr("单元转节点"); break;
    }
    setPropertyRows({
        {tr("名称"), fromUtf8(it->name)}, {tr("类型"), type},
        {tr("源结果"), loadedResultFilePath_},
        {tr("过滤字段"), it->fieldName.empty() ? tr("全部字段") : fromUtf8(it->fieldName)},
        {tr("参数"), fromUtf8(it->parameters)},
        {tr("当前可见性"), it->visible ? tr("显示") : tr("隐藏")},
        {tr("数据来源"), tr("派生")},
        {tr("当前帧"), QString::number(it->frameNumber)},
        {tr("状态"), it->valid ? tr("有效") : tr("需要更新")},
        {tr("输出节点数"), QString::number(it->pointCount)},
        {tr("输出单元数"), QString::number(it->cellCount)}
    });
}

void WorkbenchMainWindow::setFilterResultVisible(QStandardItem* item,
                                                  bool visible) {
    if (item == nullptr) return;
    const int id = item->data(FilterResultIdRole).toInt();
    vtkPostViewWidget_->setFilterVisible(id, visible);
    showFilterResultProperties(id);
}

void WorkbenchMainWindow::renameFilterResult(int filterId) {
    const auto filters = vtkPostViewWidget_->filterResults();
    const auto it = std::find_if(filters.begin(), filters.end(),
        [filterId](const auto& filter) { return filter.id == filterId; });
    if (it == filters.end()) return;
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("重命名过滤结果"), tr("名称"), QLineEdit::Normal,
        fromUtf8(it->name), &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    vtkPostViewWidget_->renameFilter(filterId, toUtf8(name));
    rebuildFilterResultTree();
}

void WorkbenchMainWindow::deleteFilterResult(int filterId) {
    if (QMessageBox::question(this, tr("删除过滤结果"),
            tr("确定删除该派生过滤结果吗？原始结果不会被删除。")) !=
        QMessageBox::Yes) return;
    vtkPostViewWidget_->removeFilter(filterId);
    rebuildFilterResultTree();
    showLoadedResultProperties();
}

void WorkbenchMainWindow::startResultAnimation() {
    if (!loadedResultSequence_ || loadedResultSequence_->frames.size() <= 1) {
        QMessageBox::information(this, tr("无法播放"),
                                 tr("当前结果序列不足两帧。"));
        return;
    }
    const double speed = std::max(resultControlWidget_->playbackSpeed(), 0.01);
    resultAnimationTimer_->start(
        std::max(10, static_cast<int>(200.0 / speed)));
    resultControlWidget_->setPlaybackActive(true);
    statusBar()->showMessage(tr("结果动画正在播放"));
}

void WorkbenchMainWindow::pauseResultAnimation() {
    resultAnimationTimer_->stop();
    resultControlWidget_->setPlaybackActive(false);
    statusBar()->showMessage(tr("结果动画已暂停"), 3000);
}

void WorkbenchMainWindow::stopResultAnimation() {
    resultAnimationTimer_->stop();
    resultControlWidget_->setPlaybackActive(false);
    const int first = resultControlWidget_->firstFrame();
    if (first >= 0 && first != loadedResultFrameNumber_)
        loadVtkResultSequenceFrame(first);
    statusBar()->showMessage(tr("结果动画已停止"), 3000);
}

void WorkbenchMainWindow::advanceResultAnimation() {
    const int next = resultControlWidget_->nextFrame(
        resultControlWidget_->loopPlayback());
    if (next < 0) {
        pauseResultAnimation();
        return;
    }
    if (!loadVtkResultSequenceFrame(next)) {
        pauseResultAnimation();
        return;
    }
    const double speed = std::max(resultControlWidget_->playbackSpeed(), 0.01);
    resultAnimationTimer_->setInterval(
        std::max(10, static_cast<int>(200.0 / speed)));
}

void WorkbenchMainWindow::exportResultAnimation() {
    QMessageBox::information(
        this, tr("AVI 导出不可用"),
        tr("当前随 QTCAE 配置的 VTK 9.4 构建未包含 IOMovie/AVI Writer。\n"
           "本版本不会伪造 AVI 输出，也不会自动引入 FFmpeg。"));
}

void WorkbenchMainWindow::extractNodeHistory() {
    if (!loadedResultSequence_) {
        QMessageBox::information(this, tr("无法提取时间历程"),
                                 tr("请先导入多帧 VTK 结果序列。"));
        return;
    }
    const ResultFieldProcessor processor;
    std::vector<ResultScalarOption> options;
    QStringList names;
    for (const auto& field : loadedResultFields_) {
        if (field.association != ResultFieldAssociation::Point) continue;
        for (const auto& option : processor.scalarOptions(field)) {
            options.push_back(option);
            names.push_back(fromUtf8(option.displayName));
        }
    }
    if (options.empty()) {
        QMessageBox::information(this, tr("无法提取时间历程"),
                                 tr("当前序列没有兼容的节点结果字段。"));
        return;
    }
    bool ok = false;
    QHash<QString, int> partIds;
    QStringList partNames;
    for (const auto& frame : loadedResultSequence_->frames) {
        for (const auto& part : frame.parts) {
            const QString name = fromUtf8(part.name);
            if (!partIds.contains(name)) {
                partIds.insert(name, part.id);
                partNames.push_back(name);
            }
        }
    }
    const QString selectedPart = QInputDialog::getItem(
        this, tr("节点时间历程"), tr("部件"), partNames, 0, false, &ok);
    if (!ok) return;
    const QString selected = QInputDialog::getItem(
        this, tr("节点时间历程"), tr("结果字段"), names, 0, false, &ok);
    if (!ok) return;
    const int optionIndex = names.indexOf(selected);
    const int nodeId = QInputDialog::getInt(
        this, tr("节点时间历程"), tr("节点 ID"), 1, 1,
        std::numeric_limits<int>::max(), 1, &ok);
    if (!ok) return;
    HistoryResult history;
    {
        const WaitCursor waitCursor;
        const ResultHistoryExtractor extractor;
        history = extractor.extractNodeHistory(
            *loadedResultSequence_,
            options[static_cast<std::size_t>(optionIndex)], nodeId,
            partIds.value(selectedPart, -1));
    }
    if (!history.success) {
        QMessageBox::critical(this, tr("时间历程提取失败"),
                              fromUtf8(history.errorMessage));
        return;
    }
    historyCurveWidget_->setCurve(history.curve);
    historyDock_->show();
    historyDock_->raise();
    for (const auto& warning : history.curve.warnings)
        messageLog_->appendPlainText(tr("时间历程警告：%1").arg(fromUtf8(warning)));
    statusBar()->showMessage(tr("节点时间历程提取成功"), 5000);
}

void WorkbenchMainWindow::setPostDisplayMode(
    VtkMeshDisplayMode mode) {
    if (workspaceStack_->currentIndex() != 1 ||
        !vtkPostViewWidget_->hasMesh()) {
        return;
    }
    switch (mode) {
    case VtkMeshDisplayMode::Points:
        vtkPostViewWidget_->setPoints();
        break;
    case VtkMeshDisplayMode::SurfaceWithEdges:
        vtkPostViewWidget_->setSurfaceWithEdges();
        break;
    case VtkMeshDisplayMode::SurfaceOnly:
        vtkPostViewWidget_->setSurfaceOnly();
        break;
    case VtkMeshDisplayMode::Wireframe:
        vtkPostViewWidget_->setWireframe();
        break;
    }
    if (loadedResultGrid_ != nullptr) {
        showCurrentResultScalarProperties();
    } else {
        showPostprocessingMeshProperties(currentPostMeshId_);
    }
    statusBar()->showMessage(
        tr("后处理显示模式：%1").arg(postDisplayModeName()), 3000);
}

QString WorkbenchMainWindow::postDisplayModeName() const {
    switch (vtkPostViewWidget_->displayMode()) {
    case VtkMeshDisplayMode::Points:
        return tr("点");
    case VtkMeshDisplayMode::SurfaceWithEdges:
        return tr("表面加边线");
    case VtkMeshDisplayMode::SurfaceOnly:
        return tr("仅表面");
    case VtkMeshDisplayMode::Wireframe:
        return tr("线框");
    }
    return {};
}

void WorkbenchMainWindow::updatePostViewActionStates() {
    const bool inPostprocessing = workspaceStack_ != nullptr &&
                                  workspaceStack_->currentIndex() == 1;
    const bool enabled = inPostprocessing &&
                         vtkPostViewWidget_ != nullptr &&
                         vtkPostViewWidget_->hasMesh();
    surfaceWithEdgesAction_->setEnabled(enabled);
    surfaceOnlyAction_->setEnabled(enabled);
    wireframeAction_->setEnabled(enabled);
    pointsAction_->setEnabled(enabled);
    savePostViewImageAction_->setEnabled(enabled);
    postBackgroundColorAction_->setEnabled(inPostprocessing);
    postPointSizeAction_->setEnabled(enabled);
    createClipResultAction_->setEnabled(enabled && loadedResultGrid_ != nullptr);
    createSliceResultAction_->setEnabled(enabled && loadedResultGrid_ != nullptr);
    createThresholdResultAction_->setEnabled(enabled && loadedResultGrid_ != nullptr);
    pointToCellResultAction_->setEnabled(enabled && loadedResultGrid_ != nullptr);
    cellToPointResultAction_->setEnabled(enabled && loadedResultGrid_ != nullptr);
    exportResultAnimationAction_->setEnabled(
        enabled && loadedResultSequence_ &&
        loadedResultSequence_->frames.size() > 1);
    extractNodeHistoryAction_->setEnabled(
        enabled && loadedResultSequence_ &&
        !loadedResultSequence_->frames.empty());
    if (inPostprocessing) {
        setViewActionsEnabled(enabled);
        generateMeshAction_->setEnabled(false);
        clearMeshAction_->setEnabled(false);
        importHmAsciiAction_->setEnabled(false);
        exportHmAsciiAction_->setEnabled(false);
    }
}

void WorkbenchMainWindow::createBlankMaterial() {
    createMaterial({});
}

void WorkbenchMainWindow::createSteelMaterial() {
    createMaterial(emilcae::core::makeStructuralSteelMaterial(
        toUtf8(tr("结构钢"))));
}

void WorkbenchMainWindow::createAluminumMaterial() {
    createMaterial(emilcae::core::makeAluminumAlloyMaterial(
        toUtf8(tr("铝合金"))));
}

void WorkbenchMainWindow::createMaterial(
    const emilcae::core::Material& initialMaterial) {
    MaterialEditorDialog dialog(
        [this](const QString& name, int excludedMaterialId) {
            return isMaterialNameAvailable(name, excludedMaterialId);
        },
        -1, this);
    dialog.setMaterial(initialMaterial);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const emilcae::core::Material material = dialog.material();
    const emilcae::core::MaterialOperationResult result =
        materialManager_.createMaterial(material);
    if (!result.success) {
        QMessageBox::warning(this, tr("创建材料失败"),
                             materialErrorMessage(result.error));
        return;
    }

    const emilcae::core::Material* stored =
        materialManager_.findMaterial(result.materialId);
    if (stored == nullptr) {
        QMessageBox::warning(this, tr("创建材料失败"),
                             tr("材料创建后无法读取。"));
        return;
    }
    const QString name = fromUtf8(stored->name);
    addMaterialTreeItem(stored->id, name);
    selectedMaterialId_ = stored->id;
    selectedNamedSelectionId_ = -1;
    selectedConstraintId_ = -1;
    selectedGeometryObjectId_ = -1;
    selectedMeshObjectId_ = -1;
    showMaterialProperties(stored->id);
    updateMaterialActionStates();
    updateSectionActionStates();
    messageLog_->appendPlainText(tr("已创建材料：%1").arg(name));
    statusBar()->showMessage(tr("材料创建成功"), 3000);
}

void WorkbenchMainWindow::editSelectedMaterial() {
    const emilcae::core::Material* existing =
        materialManager_.findMaterial(selectedMaterialId_);
    if (existing == nullptr) {
        updateMaterialActionStates();
        return;
    }
    const int id = existing->id;
    MaterialEditorDialog dialog(
        [this](const QString& name, int excludedMaterialId) {
            return isMaterialNameAvailable(name, excludedMaterialId);
        },
        id, this);
    dialog.setMaterial(*existing);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const emilcae::core::MaterialOperationResult result =
        materialManager_.updateMaterial(id, dialog.material());
    if (!result.success) {
        QMessageBox::warning(this, tr("更新材料失败"),
                             materialErrorMessage(result.error));
        return;
    }
    const emilcae::core::Material* updated =
        materialManager_.findMaterial(id);
    if (updated == nullptr) {
        return;
    }
    const QString name = fromUtf8(updated->name);
    if (QStandardItem* item = materialItem(id)) {
        item->setText(name);
    }
    showMaterialProperties(id);
    updateAssignmentDisplays();
    messageLog_->appendPlainText(tr("已更新材料：%1").arg(name));
    statusBar()->showMessage(tr("材料已更新"), 3000);
}

void WorkbenchMainWindow::renameSelectedMaterial() {
    const emilcae::core::Material* existing =
        materialManager_.findMaterial(selectedMaterialId_);
    if (existing == nullptr) {
        updateMaterialActionStates();
        return;
    }
    bool accepted = false;
    const QString name = QInputDialog::getText(
        this, tr("重命名材料"), tr("材料名称："), QLineEdit::Normal,
        fromUtf8(existing->name), &accepted).trimmed();
    if (!accepted) {
        return;
    }
    if (name.isEmpty()) {
        QMessageBox::warning(this, tr("重命名材料"),
                             tr("材料名称不能为空。"));
        return;
    }
    if (!isMaterialNameAvailable(name, existing->id)) {
        QMessageBox::warning(this, tr("重命名材料"),
                             tr("材料名称“%1”已经存在。").arg(name));
        return;
    }

    emilcae::core::Material updated = *existing;
    updated.name = toUtf8(name);
    const emilcae::core::MaterialOperationResult result =
        materialManager_.updateMaterial(existing->id, updated);
    if (!result.success) {
        QMessageBox::warning(this, tr("重命名材料"),
                             materialErrorMessage(result.error));
        return;
    }
    if (QStandardItem* item = materialItem(existing->id)) {
        item->setText(name);
    }
    showMaterialProperties(existing->id);
    updateAssignmentDisplays();
    messageLog_->appendPlainText(tr("已更新材料：%1").arg(name));
    statusBar()->showMessage(tr("材料已重命名"), 3000);
}

void WorkbenchMainWindow::duplicateSelectedMaterial() {
    const emilcae::core::Material* existing =
        materialManager_.findMaterial(selectedMaterialId_);
    if (existing == nullptr) {
        updateMaterialActionStates();
        return;
    }
    emilcae::core::Material copy = *existing;
    copy.id = -1;
    copy.name = toUtf8(uniqueMaterialCopyName(fromUtf8(existing->name)));
    const emilcae::core::MaterialOperationResult result =
        materialManager_.createMaterial(copy);
    if (!result.success) {
        QMessageBox::warning(this, tr("复制材料失败"),
                             materialErrorMessage(result.error));
        return;
    }
    const emilcae::core::Material* stored =
        materialManager_.findMaterial(result.materialId);
    if (stored == nullptr) {
        return;
    }
    const QString name = fromUtf8(stored->name);
    addMaterialTreeItem(stored->id, name);
    selectedMaterialId_ = stored->id;
    selectedNamedSelectionId_ = -1;
    selectedConstraintId_ = -1;
    showMaterialProperties(stored->id);
    updateMaterialActionStates();
    messageLog_->appendPlainText(tr("已创建材料：%1").arg(name));
    statusBar()->showMessage(tr("材料复制成功"), 3000);
}

void WorkbenchMainWindow::deleteSelectedMaterial() {
    const emilcae::core::Material* existing =
        materialManager_.findMaterial(selectedMaterialId_);
    if (existing == nullptr) {
        updateMaterialActionStates();
        return;
    }
    const int id = existing->id;
    const QString name = fromUtf8(existing->name);
    const std::vector<emilcae::core::SolidSection> references =
        sectionManager_.sectionsUsingMaterial(id);
    if (!references.empty()) {
        QString message = tr("材料“%1”正在被以下实体截面使用：\n")
                              .arg(name);
        for (const emilcae::core::SolidSection& section : references) {
            message += tr("- %1\n").arg(fromUtf8(section.name));
        }
        message += tr("\n请先修改或删除相关截面。");
        QMessageBox::warning(this, tr("无法删除材料"), message);
        return;
    }
    if (QMessageBox::question(
            this, tr("删除材料"),
            tr("确定要删除材料“%1”吗？").arg(name),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    const emilcae::core::MaterialOperationResult result =
        materialManager_.removeMaterial(id);
    if (!result.success) {
        QMessageBox::warning(this, tr("删除材料失败"),
                             materialErrorMessage(result.error));
        return;
    }
    QStandardItem* item = materialItem(id);
    materialItems_.remove(id);
    if (item != nullptr && item->parent() == materialRootItem_) {
        materialRootItem_->removeRow(item->row());
    }
    selectedMaterialId_ = -1;
    showDefaultProperties();
    updateMaterialActionStates();
    updateSectionActionStates();
    messageLog_->appendPlainText(tr("已删除材料：%1").arg(name));
    statusBar()->showMessage(tr("材料已删除"), 3000);
}

void WorkbenchMainWindow::addMaterialTreeItem(int materialId,
                                               const QString& name) {
    auto* item = new QStandardItem(name);
    item->setData(materialId, MaterialIdRole);
    item->setEditable(false);
    materialRootItem_->appendRow(item);
    materialItems_.insert(materialId, item);
    projectTree_->expand(materialRootItem_->index());
    projectTree_->setCurrentIndex(item->index());
}

bool WorkbenchMainWindow::isMaterialNameAvailable(
    const QString& name, int excludedMaterialId) const {
    const QString normalized = name.trimmed();
    for (const emilcae::core::Material& material :
         materialManager_.materials()) {
        if (material.id != excludedMaterialId &&
            fromUtf8(material.name) == normalized) {
            return false;
        }
    }
    return true;
}

QString WorkbenchMainWindow::uniqueMaterialCopyName(
    const QString& sourceName) const {
    const QString base = tr("%1 - 副本").arg(sourceName);
    if (isMaterialNameAvailable(base, -1)) {
        return base;
    }
    for (int suffix = 2;; ++suffix) {
        const QString candidate = tr("%1 - 副本 %2")
                                      .arg(sourceName)
                                      .arg(suffix);
        if (isMaterialNameAvailable(candidate, -1)) {
            return candidate;
        }
    }
}

QString WorkbenchMainWindow::materialErrorMessage(
    emilcae::core::MaterialError error) const {
    using emilcae::core::MaterialError;
    switch (error) {
    case MaterialError::None:
        return tr("无错误。");
    case MaterialError::InvalidId:
        return tr("材料 ID 无效或已经存在。");
    case MaterialError::NotFound:
        return tr("指定材料不存在。");
    case MaterialError::EmptyName:
        return tr("材料名称不能为空。");
    case MaterialError::DuplicateName:
        return tr("同名材料已经存在。");
    case MaterialError::InvalidDensity:
        return tr("密度必须是大于零的有限数值。");
    case MaterialError::InvalidYoungsModulus:
        return tr("弹性模量必须是大于零的有限数值。");
    case MaterialError::InvalidPoissonRatio:
        return tr("泊松比必须满足 -1.0 < ν < 0.5。");
    }
    return tr("未知材料错误。");
}

void WorkbenchMainWindow::createSolidSection() {
    if (materialManager_.materials().empty()) {
        QMessageBox::information(
            this, tr("新建实体截面"),
            tr("当前工程中没有可用材料，请先创建材料。"));
        return;
    }
    SolidSectionEditorDialog dialog(
        materialManager_.materials(),
        [this](const QString& name, int excludedId) {
            return isSectionNameAvailable(name, excludedId);
        },
        -1, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const auto result = sectionManager_.createSection(
        toUtf8(dialog.sectionName()), dialog.materialId());
    if (!result.success) {
        QMessageBox::warning(this, tr("创建截面失败"),
                             tr("截面名称或材料引用无效。"));
        return;
    }
    const auto* section = sectionManager_.findSection(result.sectionId);
    if (section == nullptr) {
        return;
    }
    addSectionTreeItem(section->id, fromUtf8(section->name));
    selectedSectionId_ = section->id;
    selectedNamedSelectionId_ = -1;
    selectedConstraintId_ = -1;
    showSectionProperties(section->id);
    updateSectionActionStates();
    messageLog_->appendPlainText(
        tr("已创建实体截面：%1").arg(fromUtf8(section->name)));
    statusBar()->showMessage(tr("实体截面创建成功"), 3000);
}

void WorkbenchMainWindow::editSelectedSection() {
    const auto* section = sectionManager_.findSection(selectedSectionId_);
    if (section == nullptr) {
        return;
    }
    const int id = section->id;
    SolidSectionEditorDialog dialog(
        materialManager_.materials(),
        [this](const QString& name, int excludedId) {
            return isSectionNameAvailable(name, excludedId);
        },
        id, this);
    dialog.setValues(fromUtf8(section->name), section->materialId);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const auto result = sectionManager_.updateSection(
        id, toUtf8(dialog.sectionName()), dialog.materialId());
    if (!result.success) {
        QMessageBox::warning(this, tr("更新截面失败"),
                             tr("截面名称或材料引用无效。"));
        return;
    }
    const auto* updated = sectionManager_.findSection(id);
    if (updated == nullptr) {
        return;
    }
    if (QStandardItem* item = sectionItem(id)) {
        item->setText(fromUtf8(updated->name));
    }
    showSectionProperties(id);
    updateAssignmentDisplays();
    messageLog_->appendPlainText(
        tr("已更新实体截面：%1").arg(fromUtf8(updated->name)));
}

void WorkbenchMainWindow::renameSelectedSection() {
    const auto* section = sectionManager_.findSection(selectedSectionId_);
    if (section == nullptr) {
        return;
    }
    const int id = section->id;
    bool accepted = false;
    const QString name = QInputDialog::getText(
        this, tr("重命名实体截面"), tr("截面名称："),
        QLineEdit::Normal, fromUtf8(section->name), &accepted).trimmed();
    if (!accepted) {
        return;
    }
    if (name.isEmpty() || !isSectionNameAvailable(name, id)) {
        QMessageBox::warning(this, tr("重命名实体截面"),
                             tr("截面名称为空或已经存在。"));
        return;
    }
    const auto result = sectionManager_.updateSection(
        id, toUtf8(name), section->materialId);
    if (!result.success) {
        return;
    }
    if (QStandardItem* item = sectionItem(id)) {
        item->setText(name);
    }
    showSectionProperties(id);
    updateAssignmentDisplays();
    messageLog_->appendPlainText(tr("已更新实体截面：%1").arg(name));
}

void WorkbenchMainWindow::duplicateSelectedSection() {
    const auto* section = sectionManager_.findSection(selectedSectionId_);
    if (section == nullptr) {
        return;
    }
    const QString name = uniqueSectionCopyName(fromUtf8(section->name));
    const auto result = sectionManager_.createSection(
        toUtf8(name), section->materialId);
    if (!result.success) {
        QMessageBox::warning(this, tr("复制截面失败"),
                             tr("无法复制当前实体截面。"));
        return;
    }
    addSectionTreeItem(result.sectionId, name);
    selectedSectionId_ = result.sectionId;
    selectedNamedSelectionId_ = -1;
    selectedConstraintId_ = -1;
    showSectionProperties(result.sectionId);
    updateSectionActionStates();
    messageLog_->appendPlainText(tr("已创建实体截面：%1").arg(name));
}

void WorkbenchMainWindow::deleteSelectedSection() {
    const auto* section = sectionManager_.findSection(selectedSectionId_);
    if (section == nullptr) {
        return;
    }
    const int id = section->id;
    const QString name = fromUtf8(section->name);
    const std::size_t references = assignmentManager_.referenceCount(id);
    if (references > 0 &&
        QMessageBox::question(
            this, tr("删除实体截面"),
            tr("实体截面“%1”当前已指派给 %2 个对象。\n"
               "删除后这些对象将变为未指派状态。\n\n是否继续？")
                .arg(name)
                .arg(references),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    if (references == 0 &&
        QMessageBox::question(
            this, tr("删除实体截面"),
            tr("确定要删除实体截面“%1”吗？").arg(name),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    assignmentManager_.removeAssignmentsForSection(id);
    if (!sectionManager_.removeSection(id).success) {
        return;
    }
    QStandardItem* item = sectionItem(id);
    sectionItems_.remove(id);
    if (item != nullptr) {
        sectionRootItem_->removeRow(item->row());
    }
    selectedSectionId_ = -1;
    showDefaultProperties();
    updateAssignmentDisplays();
    updateSectionActionStates();
    messageLog_->appendPlainText(tr("已删除实体截面：%1").arg(name));
}

void WorkbenchMainWindow::assignSectionToSelectedObject() {
    emilcae::core::SectionAssignmentTargetType targetType;
    int targetId = -1;
    QString targetName;
    QString targetTypeName;
    int sourceGeometryId = -1;
    if (const MeshObject* mesh =
            occViewWidget_->findMesh(selectedMeshObjectId_)) {
        targetType = emilcae::core::SectionAssignmentTargetType::MeshObject;
        targetId = mesh->id;
        targetName = mesh->name;
        targetTypeName = tr("网格对象");
        sourceGeometryId = mesh->geometryObjectId;
    } else if (const GeometryObject* geometry =
                   occViewWidget_->findGeometryObject(
                       selectedGeometryObjectId_)) {
        targetType =
            emilcae::core::SectionAssignmentTargetType::GeometryObject;
        targetId = geometry->id;
        targetName = geometry->name;
        targetTypeName = tr("几何对象");
    } else {
        QMessageBox::information(this, tr("指派实体截面"),
                                 tr("请先选择几何对象或网格对象。"));
        return;
    }
    if (sectionManager_.sections().empty()) {
        QMessageBox::information(
            this, tr("指派实体截面"),
            tr("当前工程中没有实体截面，请先创建实体截面。"));
        return;
    }
    const auto direct = assignmentManager_.assignedSection(targetType,
                                                            targetId);
    QString current = tr("未指派");
    if (direct) {
        if (const auto* section = sectionManager_.findSection(*direct)) {
            current = fromUtf8(section->name);
        }
    } else if (targetType ==
                   emilcae::core::SectionAssignmentTargetType::MeshObject &&
               sourceGeometryId >= 0) {
        const auto inherited = assignmentManager_.resolvedForMesh(
            targetId, sourceGeometryId);
        if (inherited) {
            if (const auto* section = sectionManager_.findSection(
                    inherited->sectionId)) {
                current = tr("%1（继承自源几何）")
                              .arg(fromUtf8(section->name));
            }
        }
    }
    SectionAssignmentDialog dialog(
        targetName, targetTypeName, current, sectionManager_.sections(),
        materialManager_.materials(), direct.value_or(-1), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const int selectedId = dialog.sectionId();
    if (!assignmentManager_.assign(targetType, targetId, selectedId).success) {
        QMessageBox::warning(this, tr("指派失败"),
                             tr("目标对象或实体截面无效。"));
        return;
    }
    const auto* section = sectionManager_.findSection(selectedId);
    updateAssignmentDisplays();
    if (targetType ==
        emilcae::core::SectionAssignmentTargetType::GeometryObject) {
        showGeometryObjectProperties(targetId);
    } else {
        showMeshProperties(targetId);
    }
    messageLog_->appendPlainText(
        tr("已将实体截面“%1”指派给对象“%2”。")
            .arg(section != nullptr ? fromUtf8(section->name) : tr("未知"),
                 targetName));
    statusBar()->showMessage(tr("实体截面指派成功"), 3000);
    updateSectionActionStates();
}

void WorkbenchMainWindow::unassignSectionFromSelectedObject() {
    emilcae::core::SectionAssignmentTargetType targetType;
    int targetId = -1;
    QString targetName;
    if (const MeshObject* mesh =
            occViewWidget_->findMesh(selectedMeshObjectId_)) {
        targetType = emilcae::core::SectionAssignmentTargetType::MeshObject;
        targetId = mesh->id;
        targetName = mesh->name;
        if (!assignmentManager_.assignedSection(targetType, targetId)) {
            if (mesh->geometryObjectId >= 0 &&
                assignmentManager_.resolvedForMesh(
                    mesh->id, mesh->geometryObjectId)) {
                QMessageBox::information(
                    this, tr("取消截面指派"),
                    tr("当前网格继承源几何的截面，请在源几何对象上取消指派。"));
            }
            return;
        }
    } else if (const GeometryObject* geometry =
                   occViewWidget_->findGeometryObject(
                       selectedGeometryObjectId_)) {
        targetType =
            emilcae::core::SectionAssignmentTargetType::GeometryObject;
        targetId = geometry->id;
        targetName = geometry->name;
    } else {
        return;
    }
    if (!assignmentManager_.assignedSection(targetType, targetId)) {
        return;
    }
    if (QMessageBox::question(
            this, tr("取消截面指派"),
            tr("确定要取消对象“%1”的实体截面指派吗？")
                .arg(targetName),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    assignmentManager_.unassign(targetType, targetId);
    updateAssignmentDisplays();
    if (targetType ==
        emilcae::core::SectionAssignmentTargetType::GeometryObject) {
        showGeometryObjectProperties(targetId);
    } else {
        showMeshProperties(targetId);
    }
    messageLog_->appendPlainText(
        tr("已取消对象“%1”的实体截面指派。").arg(targetName));
    statusBar()->showMessage(tr("实体截面指派已取消"), 3000);
    updateSectionActionStates();
}

void WorkbenchMainWindow::addSectionTreeItem(int id,
                                             const QString& name) {
    auto* item = new QStandardItem(name);
    item->setData(id, SolidSectionIdRole);
    item->setEditable(false);
    sectionRootItem_->appendRow(item);
    sectionItems_.insert(id, item);
    projectTree_->expand(sectionRootItem_->index());
    projectTree_->setCurrentIndex(item->index());
}

bool WorkbenchMainWindow::isSectionNameAvailable(
    const QString& name, int excludedSectionId) const {
    const QString normalized = name.trimmed();
    for (const auto& section : sectionManager_.sections()) {
        if (section.id != excludedSectionId &&
            fromUtf8(section.name) == normalized) {
            return false;
        }
    }
    return true;
}

QString WorkbenchMainWindow::uniqueSectionCopyName(
    const QString& sourceName) const {
    const QString base = tr("%1 - 副本").arg(sourceName);
    if (isSectionNameAvailable(base, -1)) {
        return base;
    }
    for (int suffix = 2;; ++suffix) {
        const QString candidate = tr("%1 - 副本 %2")
                                      .arg(sourceName)
                                      .arg(suffix);
        if (isSectionNameAvailable(candidate, -1)) {
            return candidate;
        }
    }
}

void WorkbenchMainWindow::createFixedConstraint(int namedSelectionId) {
    createConstraint(emilcae::core::ConstraintType::Fixed,
                     namedSelectionId);
}

void WorkbenchMainWindow::createDisplacementConstraint(
    int namedSelectionId) {
    createConstraint(emilcae::core::ConstraintType::Displacement,
                     namedSelectionId);
}

void WorkbenchMainWindow::createConstraint(
    emilcae::core::ConstraintType type, int namedSelectionId) {
    const auto selections = namedSelectionManager_->namedSelections();
    if (selections.empty()) {
        QMessageBox::information(
            this, tr("新建约束"),
            tr("请先创建至少一个几何命名选择集。"));
        return;
    }
    if (namedSelectionId >= 0 &&
        namedSelectionValiditySummary(namedSelectionId).validItemCount == 0) {
        QMessageBox::warning(
            this, tr("作用区域无效"),
            tr("当前命名选择集没有有效成员，不能创建约束。"));
        return;
    }
    DisplacementConstraintDialog dialog(
        type, selections,
        [this](const QString& name, int excludedId) {
            return isConstraintNameAvailable(name, excludedId);
        },
        -1, this);
    dialog.setName(uniqueConstraintName(type));
    if (namedSelectionId >= 0) {
        dialog.setNamedSelectionId(namedSelectionId);
    }
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    emilcae::core::DisplacementConstraint constraint = dialog.constraint();
    if (!confirmUsableConstraintRegion(constraint.namedSelectionId, true)) {
        return;
    }
    const auto result = constraintManager_->createConstraint(constraint);
    if (!result.success) {
        QMessageBox::warning(this, tr("创建约束失败"),
                             constraintErrorMessage(result.error));
        return;
    }
    const auto* stored = constraintManager_->findConstraint(
        result.constraintId);
    if (stored == nullptr) {
        return;
    }
    addConstraintTreeItem(stored->id, fromUtf8(stored->name));
    selectedConstraintId_ = stored->id;
    selectedNamedSelectionId_ = -1;
    selectedMaterialId_ = -1;
    selectedSectionId_ = -1;
    selectedGeometryObjectId_ = -1;
    selectedMeshObjectId_ = -1;
    showConstraintProperties(stored->id);
    messageLog_->appendPlainText(
        tr("已创建%1：%2")
            .arg(constraintTypeName(stored->type), fromUtf8(stored->name)));
    statusBar()->showMessage(tr("约束创建成功"), 3000);
}

void WorkbenchMainWindow::editSelectedConstraint() {
    const auto* current = constraintManager_->findConstraint(
        selectedConstraintId_);
    if (current == nullptr) {
        return;
    }
    const int id = current->id;
    const auto selections = namedSelectionManager_->namedSelections();
    DisplacementConstraintDialog dialog(
        current->type, selections,
        [this](const QString& name, int excludedId) {
            return isConstraintNameAvailable(name, excludedId);
        },
        id, this);
    dialog.setConstraint(*current);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    emilcae::core::DisplacementConstraint edited = dialog.constraint();
    if (!confirmUsableConstraintRegion(edited.namedSelectionId, true)) {
        return;
    }
    edited.id = id;
    const auto result = constraintManager_->updateConstraint(id, edited);
    if (!result.success) {
        QMessageBox::warning(this, tr("编辑约束失败"),
                             constraintErrorMessage(result.error));
        return;
    }
    updateConstraintDisplays();
    showConstraintProperties(id);
    messageLog_->appendPlainText(
        tr("已更新约束：%1").arg(fromUtf8(edited.name)));
}

void WorkbenchMainWindow::renameSelectedConstraint() {
    const auto* current = constraintManager_->findConstraint(
        selectedConstraintId_);
    if (current == nullptr) {
        return;
    }
    const int id = current->id;
    bool accepted = false;
    const QString name = QInputDialog::getText(
        this, tr("重命名约束"), tr("名称："), QLineEdit::Normal,
        fromUtf8(current->name), &accepted).trimmed();
    if (!accepted) {
        return;
    }
    emilcae::core::DisplacementConstraint renamed = *current;
    renamed.name = toUtf8(name);
    const auto result = constraintManager_->updateConstraint(id, renamed);
    if (!result.success) {
        QMessageBox::warning(this, tr("重命名约束失败"),
                             constraintErrorMessage(result.error));
        return;
    }
    updateConstraintDisplays();
    showConstraintProperties(id);
    messageLog_->appendPlainText(tr("已重命名约束：%1").arg(name));
}

void WorkbenchMainWindow::duplicateSelectedConstraint() {
    const auto* current = constraintManager_->findConstraint(
        selectedConstraintId_);
    if (current == nullptr) {
        return;
    }
    emilcae::core::DisplacementConstraint copy = *current;
    copy.id = -1;
    copy.name = toUtf8(uniqueConstraintCopyName(fromUtf8(current->name)));
    const auto result = constraintManager_->createConstraint(copy);
    if (!result.success) {
        QMessageBox::warning(this, tr("复制约束失败"),
                             constraintErrorMessage(result.error));
        return;
    }
    const auto* stored = constraintManager_->findConstraint(
        result.constraintId);
    if (stored == nullptr) {
        return;
    }
    addConstraintTreeItem(stored->id, fromUtf8(stored->name));
    selectedConstraintId_ = stored->id;
    showConstraintProperties(stored->id);
    messageLog_->appendPlainText(
        tr("已复制约束：%1").arg(fromUtf8(stored->name)));
}

void WorkbenchMainWindow::locateSelectedConstraint(bool notify) {
    const auto* constraint = constraintManager_->findConstraint(
        selectedConstraintId_);
    const auto* selection = constraint != nullptr
        ? namedSelectionManager_->find(constraint->namedSelectionId)
        : nullptr;
    if (constraint == nullptr || selection == nullptr) {
        if (notify) {
            QMessageBox::warning(this, tr("定位约束"),
                                 tr("约束的作用区域已经失效。"));
        }
        return;
    }
    const NamedSelectionResolveResult resolved =
        namedSelectionResolver_->resolve(*selection);
    std::vector<TopoDS_Shape> visibleShapes;
    std::size_t hiddenCount = 0;
    for (const ResolvedNamedSelectionItem& item : resolved.validItems) {
        if (item.sourceVisible) {
            visibleShapes.push_back(item.shape);
        } else {
            ++hiddenCount;
        }
    }
    syncingTreeSelection_ = true;
    occViewWidget_->clearSelection();
    if (!visibleShapes.empty()) {
        occViewWidget_->highlightNamedSelection(visibleShapes);
    }
    if (QStandardItem* item = constraintItem(constraint->id)) {
        projectTree_->setCurrentIndex(item->index());
    }
    syncingTreeSelection_ = false;
    showConstraintProperties(constraint->id);
    if (notify && (!resolved.invalidItems.empty() || hiddenCount > 0)) {
        QMessageBox::information(
            this, tr("定位约束"),
            tr("作用区域中有 %1 个失效成员、%2 个隐藏成员。")
                .arg(resolved.invalidItems.size())
                .arg(hiddenCount));
    }
    statusBar()->showMessage(
        tr("已定位约束作用区域：%1").arg(fromUtf8(constraint->name)),
        3000);
}

void WorkbenchMainWindow::deleteSelectedConstraint() {
    const auto* constraint = constraintManager_->findConstraint(
        selectedConstraintId_);
    if (constraint == nullptr) {
        return;
    }
    const int id = constraint->id;
    const QString name = fromUtf8(constraint->name);
    if (QMessageBox::question(
            this, tr("删除约束"),
            tr("确定要删除约束“%1”吗？").arg(name),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    if (!constraintManager_->removeConstraint(id).success) {
        return;
    }
    QStandardItem* item = constraintItem(id);
    constraintItems_.remove(id);
    constraintValidityStates_.remove(id);
    if (item != nullptr) {
        boundaryConditionRootItem_->removeRow(item->row());
    }
    selectedConstraintId_ = -1;
    occViewWidget_->clearNamedSelectionHighlight();
    showDefaultProperties();
    messageLog_->appendPlainText(tr("已删除约束：%1").arg(name));
}

void WorkbenchMainWindow::addConstraintTreeItem(
    int id, const QString& name) {
    auto* item = new QStandardItem(name);
    item->setData(id, ConstraintIdRole);
    item->setEditable(false);
    boundaryConditionRootItem_->appendRow(item);
    constraintItems_.insert(id, item);
    projectTree_->expand(analysisRootItem_->index());
    projectTree_->expand(boundaryConditionRootItem_->index());
    projectTree_->setCurrentIndex(item->index());
    updateConstraintDisplays();
}

emilcae::core::NamedSelectionValiditySummary
WorkbenchMainWindow::namedSelectionValiditySummary(int id) const {
    const auto* selection = namedSelectionManager_->find(id);
    if (selection == nullptr) {
        return {};
    }
    const NamedSelectionResolveResult resolved =
        namedSelectionResolver_->resolve(*selection);
    return {resolved.validItems.size(), resolved.invalidItems.size()};
}

emilcae::core::ConstraintValidity
WorkbenchMainWindow::constraintValidity(int id) const {
    return constraintManager_->validity(
        id, [this](int namedSelectionId) {
            return namedSelectionValiditySummary(namedSelectionId);
        });
}

bool WorkbenchMainWindow::confirmUsableConstraintRegion(
    int namedSelectionId, bool allowPartial) {
    const auto summary = namedSelectionValiditySummary(namedSelectionId);
    if (summary.validItemCount == 0) {
        QMessageBox::warning(
            this, tr("作用区域无效"),
            tr("当前命名选择集没有有效成员，不能创建约束。"));
        return false;
    }
    if (summary.invalidItemCount > 0) {
        if (!allowPartial) {
            return false;
        }
        return QMessageBox::question(
                   this, tr("作用区域部分失效"),
                   tr("所选命名选择集中有 %1 个失效成员。约束将仅保留当前可解析的作用区域，是否继续？")
                       .arg(summary.invalidItemCount),
                   QMessageBox::Yes | QMessageBox::No,
                   QMessageBox::No) == QMessageBox::Yes;
    }
    return true;
}

bool WorkbenchMainWindow::isConstraintNameAvailable(
    const QString& name, int excludedId) const {
    const std::string candidate = toUtf8(name.trimmed());
    const auto constraints = constraintManager_->constraints();
    return std::none_of(
        constraints.cbegin(), constraints.cend(),
        [&candidate, excludedId](
            const emilcae::core::DisplacementConstraint& constraint) {
            return constraint.id != excludedId &&
                   constraint.name == candidate;
        });
}

QString WorkbenchMainWindow::uniqueConstraintName(
    emilcae::core::ConstraintType type) const {
    const QString base = type == emilcae::core::ConstraintType::Fixed
        ? tr("固定约束") : tr("位移约束");
    for (int suffix = 1;; ++suffix) {
        const QString candidate = tr("%1-%2").arg(base).arg(suffix);
        if (isConstraintNameAvailable(candidate, -1)) {
            return candidate;
        }
    }
}

QString WorkbenchMainWindow::uniqueConstraintCopyName(
    const QString& sourceName) const {
    const QString base = tr("%1 - 副本").arg(sourceName);
    if (isConstraintNameAvailable(base, -1)) {
        return base;
    }
    for (int suffix = 2;; ++suffix) {
        const QString candidate = tr("%1 - 副本 %2")
                                      .arg(sourceName).arg(suffix);
        if (isConstraintNameAvailable(candidate, -1)) {
            return candidate;
        }
    }
}

QString WorkbenchMainWindow::constraintErrorMessage(
    emilcae::core::ConstraintError error) const {
    using emilcae::core::ConstraintError;
    switch (error) {
    case ConstraintError::None:
        return {};
    case ConstraintError::InvalidId:
        return tr("约束 ID 无效。");
    case ConstraintError::NotFound:
        return tr("没有找到指定约束。");
    case ConstraintError::EmptyName:
        return tr("约束名称不能为空。");
    case ConstraintError::DuplicateName:
        return tr("约束名称不能重复。");
    case ConstraintError::InvalidNamedSelection:
        return tr("必须引用一个存在且非空的命名选择集。");
    case ConstraintError::NoConstrainedDof:
        return tr("至少需要约束一个平移方向。");
    case ConstraintError::NonFiniteValue:
        return tr("位移值必须是有限数值。");
    case ConstraintError::InvalidFixedConstraint:
        return tr("固定约束必须固定 X、Y、Z 三个方向且位移值为零。");
    }
    return tr("约束操作失败。");
}

void WorkbenchMainWindow::createNamedSelection() {
    std::vector<emilcae::core::NamedSelectionItem> items;
    emilcae::core::NamedSelectionEntityType entityType;
    QString error;
    if (!currentNamedSelectionItems(items, entityType, error)) {
        QMessageBox::information(this, tr("创建命名选择集"), error);
        return;
    }
    bool accepted = false;
    const QString name = QInputDialog::getText(
        this, tr("创建命名选择集"), tr("名称："), QLineEdit::Normal,
        uniqueNamedSelectionName(entityType), &accepted).trimmed();
    if (!accepted) {
        return;
    }
    const auto result = namedSelectionManager_->create(
        toUtf8(name), entityType, items);
    if (!result.success) {
        QMessageBox::warning(this, tr("创建命名选择集失败"),
                             namedSelectionErrorMessage(result.error));
        return;
    }
    const auto* selection =
        namedSelectionManager_->find(result.namedSelectionId);
    if (selection == nullptr) {
        return;
    }
    addNamedSelectionTreeItem(selection->id, fromUtf8(selection->name));
    selectedNamedSelectionId_ = selection->id;
    selectedConstraintId_ = -1;
    syncingTreeSelection_ = true;
    occViewWidget_->clearSelection();
    syncingTreeSelection_ = false;
    showNamedSelectionProperties(selection->id);
    messageLog_->appendPlainText(
        tr("已创建命名选择集：%1").arg(fromUtf8(selection->name)));
    statusBar()->showMessage(tr("命名选择集创建成功"), 3000);
}

void WorkbenchMainWindow::locateSelectedNamedSelection(
    bool notifyHiddenObjects) {
    const auto* selection =
        namedSelectionManager_->find(selectedNamedSelectionId_);
    if (selection == nullptr) {
        return;
    }
    const NamedSelectionResolveResult resolved =
        namedSelectionResolver_->resolve(*selection);
    std::vector<TopoDS_Shape> visibleShapes;
    std::size_t hiddenCount = 0;
    for (const ResolvedNamedSelectionItem& item : resolved.validItems) {
        if (item.sourceVisible) {
            visibleShapes.push_back(item.shape);
        } else {
            ++hiddenCount;
        }
    }
    syncingTreeSelection_ = true;
    occViewWidget_->clearSelection();
    if (!visibleShapes.empty()) {
        occViewWidget_->highlightNamedSelection(visibleShapes);
    }
    if (QStandardItem* item =
            namedSelectionItem(selectedNamedSelectionId_)) {
        projectTree_->setCurrentIndex(item->index());
    }
    syncingTreeSelection_ = false;
    showNamedSelectionProperties(selectedNamedSelectionId_);
    if (notifyHiddenObjects && hiddenCount > 0) {
        QMessageBox::information(
            this, tr("命名选择集定位"),
            tr("有 %1 个有效成员所属的几何对象当前处于隐藏状态，"
               "这些成员未在视窗中高亮。")
                .arg(hiddenCount));
    }
    statusBar()->showMessage(
        tr("已定位命名选择集：%1；有效 %2，失效 %3，隐藏 %4")
            .arg(fromUtf8(selection->name))
            .arg(resolved.validItems.size())
            .arg(resolved.invalidItems.size())
            .arg(hiddenCount),
        5000);
}

void WorkbenchMainWindow::renameSelectedNamedSelection() {
    const auto* selection =
        namedSelectionManager_->find(selectedNamedSelectionId_);
    if (selection == nullptr) {
        return;
    }
    bool accepted = false;
    const QString name = QInputDialog::getText(
        this, tr("重命名命名选择集"), tr("名称："), QLineEdit::Normal,
        fromUtf8(selection->name), &accepted).trimmed();
    if (!accepted) {
        return;
    }
    const auto result = namedSelectionManager_->rename(
        selection->id, toUtf8(name));
    if (!result.success) {
        QMessageBox::warning(this, tr("重命名失败"),
                             namedSelectionErrorMessage(result.error));
        return;
    }
    updateNamedSelectionDisplays();
    showNamedSelectionProperties(selection->id);
    messageLog_->appendPlainText(
        tr("已重命名命名选择集：%1").arg(name));
}

void WorkbenchMainWindow::replaceSelectedNamedSelectionItems() {
    const auto* selection =
        namedSelectionManager_->find(selectedNamedSelectionId_);
    if (selection == nullptr) {
        return;
    }
    std::vector<emilcae::core::NamedSelectionItem> items;
    emilcae::core::NamedSelectionEntityType entityType;
    QString error;
    if (!currentNamedSelectionItems(items, entityType, error)) {
        QMessageBox::information(this, tr("替换命名选择集"), error);
        return;
    }
    if (entityType != selection->entityType) {
        QMessageBox::warning(this, tr("类型不一致"),
                             tr("当前选择类型必须与命名选择集类型一致。"));
        return;
    }
    const auto result = namedSelectionManager_->replaceItems(
        selection->id, items);
    if (!result.success) {
        QMessageBox::warning(this, tr("替换失败"),
                             namedSelectionErrorMessage(result.error));
        return;
    }
    locateSelectedNamedSelection(false);
    updateNamedSelectionDisplays();
    messageLog_->appendPlainText(
        tr("已用当前选择替换命名选择集“%1”的成员。")
            .arg(fromUtf8(selection->name)));
}

void WorkbenchMainWindow::addSelectedNamedSelectionItems() {
    const auto* selection =
        namedSelectionManager_->find(selectedNamedSelectionId_);
    if (selection == nullptr) {
        return;
    }
    std::vector<emilcae::core::NamedSelectionItem> items;
    emilcae::core::NamedSelectionEntityType entityType;
    QString error;
    if (!currentNamedSelectionItems(items, entityType, error)) {
        QMessageBox::information(this, tr("添加当前选择"), error);
        return;
    }
    if (entityType != selection->entityType) {
        QMessageBox::warning(this, tr("类型不一致"),
                             tr("当前选择类型必须与命名选择集类型一致。"));
        return;
    }
    const auto result = namedSelectionManager_->addItems(selection->id, items);
    if (!result.success) {
        QMessageBox::warning(this, tr("添加失败"),
                             namedSelectionErrorMessage(result.error));
        return;
    }
    locateSelectedNamedSelection(false);
    updateNamedSelectionDisplays();
    messageLog_->appendPlainText(
        tr("已向命名选择集“%1”添加 %2 个成员。")
            .arg(fromUtf8(selection->name))
            .arg(result.changedItemCount));
}

void WorkbenchMainWindow::removeSelectedNamedSelectionItems() {
    const auto* selection =
        namedSelectionManager_->find(selectedNamedSelectionId_);
    if (selection == nullptr) {
        return;
    }
    std::vector<emilcae::core::NamedSelectionItem> items;
    emilcae::core::NamedSelectionEntityType entityType;
    QString error;
    if (!currentNamedSelectionItems(items, entityType, error)) {
        QMessageBox::information(this, tr("移除当前选择"), error);
        return;
    }
    if (entityType != selection->entityType) {
        QMessageBox::warning(this, tr("类型不一致"),
                             tr("当前选择类型必须与命名选择集类型一致。"));
        return;
    }
    const auto result = namedSelectionManager_->removeItems(
        selection->id, items);
    if (result.error ==
        emilcae::core::NamedSelectionError::WouldBecomeEmpty) {
        QMessageBox::information(
            this, tr("不能形成空选择集"),
            tr("移除后命名选择集将为空。请删除整个命名选择集。"));
        return;
    }
    if (!result.success) {
        QMessageBox::warning(this, tr("移除失败"),
                             namedSelectionErrorMessage(result.error));
        return;
    }
    locateSelectedNamedSelection(false);
    updateNamedSelectionDisplays();
    messageLog_->appendPlainText(
        tr("已从命名选择集“%1”移除 %2 个成员。")
            .arg(fromUtf8(selection->name))
            .arg(result.changedItemCount));
}

void WorkbenchMainWindow::deleteSelectedNamedSelection() {
    const auto* selection =
        namedSelectionManager_->find(selectedNamedSelectionId_);
    if (selection == nullptr) {
        return;
    }
    const int id = selection->id;
    const QString name = fromUtf8(selection->name);
    const auto referencingConstraints =
        constraintManager_->constraintsUsingNamedSelection(id);
    if (!referencingConstraints.empty()) {
        QStringList names;
        for (int constraintId : referencingConstraints) {
            if (const auto* constraint =
                    constraintManager_->findConstraint(constraintId)) {
                names.append(fromUtf8(constraint->name));
            }
        }
        QMessageBox::warning(
            this, tr("无法删除命名选择集"),
            tr("命名选择集“%1”正在被以下约束使用：\n- %2\n\n请先删除约束或修改约束的作用区域。")
                .arg(name, names.join(QStringLiteral("\n- "))));
        return;
    }
    if (QMessageBox::question(
            this, tr("删除命名选择集"),
            tr("确定要删除命名选择集“%1”吗？\n"
               "此操作不会删除任何几何对象。")
                .arg(name),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    if (!namedSelectionManager_->remove(id).success) {
        return;
    }
    QStandardItem* item = namedSelectionItem(id);
    namedSelectionItems_.remove(id);
    if (item != nullptr) {
        namedSelectionRootItem_->removeRow(item->row());
    }
    selectedNamedSelectionId_ = -1;
    occViewWidget_->clearNamedSelectionHighlight();
    showDefaultProperties();
    messageLog_->appendPlainText(tr("已删除命名选择集：%1").arg(name));
}

void WorkbenchMainWindow::addNamedSelectionTreeItem(
    int id, const QString& name) {
    auto* item = new QStandardItem(name);
    item->setData(id, NamedSelectionIdRole);
    item->setEditable(false);
    namedSelectionRootItem_->appendRow(item);
    namedSelectionItems_.insert(id, item);
    projectTree_->expand(namedSelectionRootItem_->index());
    projectTree_->setCurrentIndex(item->index());
}

bool WorkbenchMainWindow::currentNamedSelectionItems(
    std::vector<emilcae::core::NamedSelectionItem>& items,
    emilcae::core::NamedSelectionEntityType& entityType,
    QString& errorMessage) const {
    const std::vector<GeometrySelection> current =
        occViewWidget_->currentSelections();
    if (current.empty()) {
        errorMessage = tr("请先在三维视窗中选择点、边、面、实体或对象。");
        return false;
    }
    entityType = namedSelectionEntityType(current.front().mode);
    items.clear();
    items.reserve(current.size());
    for (const GeometrySelection& selection : current) {
        const auto currentType = namedSelectionEntityType(selection.mode);
        if (currentType != entityType) {
            errorMessage = tr("一个命名选择集只能包含同一种选择类型。");
            return false;
        }
        const int localIndex = selection.mode == SelectionMode::Object
            ? 0
            : selection.localIndex;
        if (selection.objectId < 0 || localIndex < 0 ||
            (selection.mode != SelectionMode::Object && localIndex == 0)) {
            errorMessage = tr("当前选择中存在无法确定局部序号的项目。");
            return false;
        }
        items.push_back(
            {selection.objectId, currentType, localIndex});
    }
    return true;
}

QString WorkbenchMainWindow::uniqueNamedSelectionName(
    emilcae::core::NamedSelectionEntityType entityType) const {
    const QString prefix = tr("%1选择集").arg(
        namedSelectionEntityName(entityType));
    const std::vector<emilcae::core::NamedSelection> selections =
        namedSelectionManager_->namedSelections();
    for (int suffix = 1;; ++suffix) {
        const QString candidate = tr("%1 %2").arg(prefix).arg(suffix);
        const bool exists = std::any_of(
            selections.cbegin(), selections.cend(),
            [&candidate](const emilcae::core::NamedSelection& selection) {
                return fromUtf8(selection.name) == candidate;
            });
        if (!exists) {
            return candidate;
        }
    }
}

QString WorkbenchMainWindow::namedSelectionErrorMessage(
    emilcae::core::NamedSelectionError error) const {
    using emilcae::core::NamedSelectionError;
    switch (error) {
    case NamedSelectionError::None:
        return tr("无错误。");
    case NamedSelectionError::NotFound:
        return tr("命名选择集不存在。");
    case NamedSelectionError::EmptyName:
        return tr("命名选择集名称不能为空。");
    case NamedSelectionError::DuplicateName:
        return tr("同名命名选择集已经存在。");
    case NamedSelectionError::EmptyItems:
        return tr("命名选择集成员不能为空。");
    case NamedSelectionError::MixedEntityTypes:
        return tr("所有成员必须具有相同选择类型。");
    case NamedSelectionError::InvalidGeometryObject:
        return tr("选择中包含不存在的几何对象。");
    case NamedSelectionError::InvalidLocalIndex:
        return tr("选择中包含无效的局部序号。");
    case NamedSelectionError::WouldBecomeEmpty:
        return tr("该操作会使命名选择集变为空集合。");
    }
    return tr("未知命名选择集错误。");
}

void WorkbenchMainWindow::addOrUpdateMeshTreeItem(
    int meshId, int geometryObjectId) {
    QStandardItem* item = meshItem(meshId);
    if (item == nullptr) {
        QStandardItem* geometry = geometryItem(geometryObjectId);
        if (geometry == nullptr) {
            return;
        }
        item = new QStandardItem(tr("网格"));
        item->setData(meshId, MeshObjectIdRole);
        item->setCheckable(true);
        item->setEditable(false);
        geometry->appendRow(item);
        meshItems_.insert(meshId, item);
        projectTree_->expand(geometry->index());
    }
    {
        const QSignalBlocker blocker(projectModel_);
        item->setCheckState(Qt::Checked);
    }
    projectTree_->setCurrentIndex(item->index());
    selectedMaterialId_ = -1;
    selectedSectionId_ = -1;
    selectedNamedSelectionId_ = -1;
    selectedConstraintId_ = -1;
    updateMaterialActionStates();
    updateSectionActionStates();
}

void WorkbenchMainWindow::addStandaloneMeshTreeItem(
    int meshId, const QString& name) {
    auto* item = new QStandardItem(name);
    item->setData(meshId, MeshObjectIdRole);
    item->setCheckable(true);
    item->setCheckState(Qt::Checked);
    item->setEditable(false);
    meshRootItem_->appendRow(item);
    meshItems_.insert(meshId, item);
    projectTree_->expand(meshRootItem_->index());
    projectTree_->setCurrentIndex(item->index());
    selectedMaterialId_ = -1;
    selectedSectionId_ = -1;
    selectedNamedSelectionId_ = -1;
    selectedConstraintId_ = -1;
    updateMaterialActionStates();
    updateSectionActionStates();
}

void WorkbenchMainWindow::switchSelectionMode(
    SelectionMode mode, const QString& displayName) {
    occViewWidget_->setSelectionMode(mode);
    statusBar()->showMessage(
        tr("已切换到%1选择模式").arg(displayName), 3000);
}

void WorkbenchMainWindow::handleViewSelectionChanged() {
    if (syncingTreeSelection_) {
        return;
    }
    selectedMaterialId_ = -1;
    selectedSectionId_ = -1;
    selectedNamedSelectionId_ = -1;
    selectedConstraintId_ = -1;
    occViewWidget_->clearNamedSelectionHighlight();
    updateMaterialActionStates();
    const std::vector<GeometrySelection> selections =
        occViewWidget_->currentSelections();
    if (selections.empty()) {
        if (selectedMeshObjectId_ >= 0) {
            return;
        }
        selectedGeometryObjectId_ = -1;
        updateSectionActionStates();
        syncingTreeSelection_ = true;
        projectTree_->clearSelection();
        syncingTreeSelection_ = false;
        showDefaultProperties();
        statusBar()->showMessage(tr("当前未选择几何项目"), 3000);
        return;
    }

    selectedGeometryObjectId_ = selections.front().objectId;
    selectedMeshObjectId_ = -1;
    updateSectionActionStates();
    if (QStandardItem* item = geometryItem(selectedGeometryObjectId_)) {
        syncingTreeSelection_ = true;
        projectTree_->setCurrentIndex(item->index());
        syncingTreeSelection_ = false;
    }

    if (selections.size() == 1) {
        showSelectionProperties(selections.front());
        const GeometryObject* object =
            occViewWidget_->findGeometryObject(selections.front().objectId);
        statusBar()->showMessage(
            object != nullptr
                ? tr("已选择：%1（%2）")
                      .arg(object->name,
                           selectionModeName(selections.front().mode))
                : tr("已选择 1 个项目"),
            3000);
        return;
    }

    setPropertyRows({
        {tr("当前选择"),
         tr("已选择 %1 个项目").arg(selections.size())}
    });
    statusBar()->showMessage(
        tr("已选择 %1 个项目").arg(selections.size()), 3000);
}

void WorkbenchMainWindow::showSelectionProperties(
    const GeometrySelection& selection) {
    const GeometryObject* object =
        occViewWidget_->findGeometryObject(selection.objectId);
    if (object == nullptr) {
        showDefaultProperties();
        return;
    }

    QVector<QPair<QString, QString>> rows = {
        {tr("所属对象"), object->name},
        {tr("对象 ID"), QString::number(object->id)},
        {tr("选择类型"), selectionModeName(selection.mode)},
        {tr("局部序号"), selection.localIndex > 0
                              ? QString::number(selection.localIndex)
                              : tr("—")}
    };
    if (selection.mode == SelectionMode::Vertex &&
        !selection.shape.IsNull() &&
        selection.shape.ShapeType() == TopAbs_VERTEX) {
        const gp_Pnt point = BRep_Tool::Pnt(TopoDS::Vertex(selection.shape));
        rows.append({tr("X"), QString::number(point.X(), 'g', 12)});
        rows.append({tr("Y"), QString::number(point.Y(), 'g', 12)});
        rows.append({tr("Z"), QString::number(point.Z(), 'g', 12)});
    }
    setPropertyRows(rows);
}

void WorkbenchMainWindow::showGeometryObjectProperties(int objectId) {
    const GeometryObject* object =
        occViewWidget_->findGeometryObject(objectId);
    if (object == nullptr) {
        showDefaultProperties();
        return;
    }
    QVector<QPair<QString, QString>> rows = {
        {tr("名称"), object->name},
        {tr("类型"), tr("几何对象")},
        {tr("格式"), geometryFormat(object->filePath)},
        {tr("路径"), object->filePath},
        {tr("可见"), object->visible ? tr("是") : tr("否")},
        {tr("对象 ID"), QString::number(object->id)}
    };
    appendAssignmentProperties(
        rows, emilcae::core::SectionAssignmentTargetType::GeometryObject,
        objectId);
    setPropertyRows(rows);
}

void WorkbenchMainWindow::showMeshProperties(int meshId) {
    const MeshObject* mesh = occViewWidget_->findMesh(meshId);
    if (mesh == nullptr) {
        showDefaultProperties();
        return;
    }
    const GeometryObject* geometry = mesh->geometryObjectId >= 0
        ? occViewWidget_->findGeometryObject(mesh->geometryObjectId)
        : nullptr;
    QVector<QPair<QString, QString>> rows;
    if (mesh->importedHmAscii) {
        rows = {
            {tr("名称"), mesh->name},
            {tr("类型"), tr("HMASCII 导入网格")},
            {tr("源文件"), mesh->sourceFilePath},
            {tr("节点数"), QString::number(mesh->data.nodes.size())},
            {tr("四面体数"), QString::number(mesh->data.tetrahedra.size())},
            {tr("表面三角形数"),
             QString::number(mesh->data.surfaceTriangles.size())},
            {tr("源几何"), tr("无")},
            {tr("可见"), mesh->visible ? tr("是") : tr("否")}
        };
    } else {
        rows = {
            {tr("名称"), mesh->name},
            {tr("类型"), tr("四面体网格")},
            {tr("源几何"), geometry != nullptr ? geometry->name : tr("未知")},
            {tr("节点数"), QString::number(mesh->data.nodes.size())},
            {tr("四面体数"), QString::number(mesh->data.tetrahedra.size())},
            {tr("表面三角形数"),
             QString::number(mesh->data.surfaceTriangles.size())},
            {tr("目标尺寸"), QString::number(mesh->targetSize, 'g', 12)},
            {tr("单元阶次"), tr("一阶")},
            {tr("可见"), mesh->visible ? tr("是") : tr("否")}
        };
    }
    appendAssignmentProperties(
        rows, emilcae::core::SectionAssignmentTargetType::MeshObject,
        meshId, mesh->geometryObjectId);
    setPropertyRows(rows);
}

void WorkbenchMainWindow::showMaterialProperties(int materialId) {
    const emilcae::core::Material* material =
        materialManager_.findMaterial(materialId);
    if (material == nullptr) {
        showDefaultProperties();
        return;
    }
    const double modulusGPa =
        emilcae::core::elasticModulusFromPascals(
            material->elasticity.youngsModulus,
            emilcae::core::ElasticModulusUnit::Gigapascal);
    setPropertyRows({
        {tr("名称"), fromUtf8(material->name)},
        {tr("类型"), tr("各向同性线弹性")},
        {tr("材料 ID"), QString::number(material->id)},
        {tr("密度"), tr("%1 kg/m³").arg(
                           QString::number(material->density, 'g', 12))},
        {tr("弹性模量"), tr("%1 GPa").arg(
                               QString::number(modulusGPa, 'g', 12))},
        {tr("泊松比"), QString::number(
                           material->elasticity.poissonRatio, 'f', 2)}
    });
}

void WorkbenchMainWindow::showNamedSelectionProperties(int id) {
    const auto* selection = namedSelectionManager_->find(id);
    if (selection == nullptr) {
        showDefaultProperties();
        return;
    }
    const NamedSelectionResolveResult resolved =
        namedSelectionResolver_->resolve(*selection);
    const std::size_t validCount = resolved.validItems.size();
    const std::size_t invalidCount = resolved.invalidItems.size();
    QString status = tr("有效");
    if (invalidCount > 0 && validCount > 0) {
        status = tr("部分失效");
    } else if (invalidCount > 0) {
        status = tr("失效");
    }
    QVector<QPair<QString, QString>> rows = {
        {tr("名称"), fromUtf8(selection->name)},
        {tr("类型"), tr("%1命名选择集").arg(
                           namedSelectionEntityName(selection->entityType))},
        {tr("选择集 ID"), QString::number(selection->id)},
        {tr("成员数量"), QString::number(selection->items.size())},
        {tr("状态"), status}
    };
    if (invalidCount > 0) {
        rows.append({tr("有效成员"), QString::number(validCount)});
        rows.append({tr("失效成员"), QString::number(invalidCount)});
    }
    setPropertyRows(rows);
}

void WorkbenchMainWindow::showConstraintProperties(int id) {
    const auto* constraint = constraintManager_->findConstraint(id);
    if (constraint == nullptr) {
        showDefaultProperties();
        return;
    }
    const auto* selection = namedSelectionManager_->find(
        constraint->namedSelectionId);
    auto dofText = [this](
        const emilcae::core::TranslationalDofConstraint& dof) {
        return dof.constrained
            ? tr("%1 mm").arg(QString::number(
                  emilcae::core::displacementFromMeters(
                      dof.value,
                      emilcae::core::DisplacementUnit::Millimeter),
                  'g', 12))
            : tr("自由");
    };
    setPropertyRows({
        {tr("名称"), fromUtf8(constraint->name)},
        {tr("类型"), constraintTypeName(constraint->type)},
        {tr("约束 ID"), QString::number(constraint->id)},
        {tr("作用区域"), selection != nullptr
                              ? fromUtf8(selection->name)
                              : tr("命名选择集已丢失")},
        {tr("选择集类型"), selection != nullptr
                              ? namedSelectionEntityName(selection->entityType)
                              : tr("未知")},
        {tr("状态"), constraintValidityName(constraintValidity(id))},
        {tr("X 位移"), dofText(constraint->ux)},
        {tr("Y 位移"), dofText(constraint->uy)},
        {tr("Z 位移"), dofText(constraint->uz)},
        {tr("坐标系"), tr("全局")}
    });
}

void WorkbenchMainWindow::updateConstraintDisplays() {
    for (const auto& constraint : constraintManager_->constraints()) {
        QStandardItem* item = constraintItem(constraint.id);
        if (item == nullptr) {
            continue;
        }
        const auto validity = constraintValidity(constraint.id);
        QString text = fromUtf8(constraint.name);
        if (validity == emilcae::core::ConstraintValidity::PartiallyInvalid) {
            text += tr("  [部分失效]");
        } else if (validity == emilcae::core::ConstraintValidity::Invalid) {
            text += tr("  [失效]");
        }
        item->setText(text);
        const int currentState = static_cast<int>(validity);
        if (constraintValidityStates_.contains(constraint.id) &&
            constraintValidityStates_.value(constraint.id) != currentState &&
            validity != emilcae::core::ConstraintValidity::Valid) {
            messageLog_->appendPlainText(
                tr("约束“%1”的作用区域存在失效成员。")
                    .arg(fromUtf8(constraint.name)));
        }
        constraintValidityStates_.insert(constraint.id, currentState);
    }
    if (selectedConstraintId_ >= 0) {
        showConstraintProperties(selectedConstraintId_);
    }
}

void WorkbenchMainWindow::updateNamedSelectionDisplays() {
    for (const emilcae::core::NamedSelection& selection :
         namedSelectionManager_->namedSelections()) {
        QStandardItem* item = namedSelectionItem(selection.id);
        if (item == nullptr) {
            continue;
        }
        const NamedSelectionResolveResult resolved =
            namedSelectionResolver_->resolve(selection);
        QString text = fromUtf8(selection.name);
        if (!resolved.invalidItems.empty()) {
            text += resolved.validItems.empty()
                ? tr("  [失效]")
                : tr("  [部分失效]");
        }
        item->setText(text);
    }
    if (selectedNamedSelectionId_ >= 0) {
        showNamedSelectionProperties(selectedNamedSelectionId_);
    }
    if (constraintManager_ != nullptr) {
        updateConstraintDisplays();
    }
}

void WorkbenchMainWindow::showSectionProperties(int id) {
    const emilcae::core::SolidSection* section =
        sectionManager_.findSection(id);
    if (section == nullptr) {
        showDefaultProperties();
        return;
    }
    const emilcae::core::Material* material =
        materialManager_.findMaterial(section->materialId);
    setPropertyRows({
        {tr("名称"), fromUtf8(section->name)},
        {tr("类型"), tr("实体截面")},
        {tr("截面 ID"), QString::number(section->id)},
        {tr("材料"), material != nullptr ? fromUtf8(material->name)
                                         : tr("未知材料")},
        {tr("材料 ID"), QString::number(section->materialId)}
    });
}

void WorkbenchMainWindow::appendAssignmentProperties(
    QVector<QPair<QString, QString>>& rows,
    emilcae::core::SectionAssignmentTargetType targetType,
    int targetId, int sourceGeometryId) const {
    std::optional<emilcae::core::ResolvedSolidSectionAssignment> resolved;
    if (targetType ==
        emilcae::core::SectionAssignmentTargetType::MeshObject) {
        resolved = assignmentManager_.resolvedForMesh(
            targetId, sourceGeometryId >= 0
                          ? std::optional<int>(sourceGeometryId)
                          : std::nullopt);
    } else if (const auto direct = assignmentManager_.assignedSection(
                   targetType, targetId)) {
        resolved = emilcae::core::ResolvedSolidSectionAssignment{
            *direct, false};
    }
    if (!resolved) {
        rows.append({tr("实体截面"), tr("未指派")});
        rows.append({tr("材料"), tr("未指派")});
        return;
    }
    const emilcae::core::SolidSection* section =
        sectionManager_.findSection(resolved->sectionId);
    const emilcae::core::Material* material = section != nullptr
        ? materialManager_.findMaterial(section->materialId)
        : nullptr;
    rows.append({tr("实体截面"),
                 section != nullptr ? fromUtf8(section->name)
                                    : tr("未知截面")});
    rows.append({tr("材料"), material != nullptr
                                    ? fromUtf8(material->name)
                                    : tr("未知材料")});
    rows.append({tr("指派方式"), resolved->inheritedFromGeometry
                                       ? tr("继承自源几何")
                                       : tr("直接指派")});
}

void WorkbenchMainWindow::showDefaultProperties() {
    const QString workspace = workspaceStack_ != nullptr &&
                                      workspaceStack_->currentIndex() == 1
        ? tr("后处理")
        : tr("前处理");
    setPropertyRows({
        {tr("当前工作区"), workspace},
        {tr("当前选择"), tr("无")}
    });
}

void WorkbenchMainWindow::showFixedNodeProperties(
    const QString& nodeName) {
    setPropertyRows({{tr("节点"), nodeName}});
}

void WorkbenchMainWindow::setPropertyRows(
    const QVector<QPair<QString, QString>>& rows) {
    propertiesModel_->setRowCount(rows.size());
    for (int row = 0; row < rows.size(); ++row) {
        propertiesModel_->setItem(
            row, 0, new QStandardItem(rows.at(row).first));
        propertiesModel_->setItem(
            row, 1, new QStandardItem(rows.at(row).second));
    }
}

int WorkbenchMainWindow::geometryObjectId(
    const QStandardItem* item) const {
    if (item == nullptr) {
        return -1;
    }
    bool valid = false;
    const int objectId = item->data(GeometryObjectIdRole).toInt(&valid);
    return valid ? objectId : -1;
}

int WorkbenchMainWindow::meshObjectId(const QStandardItem* item) const {
    if (item == nullptr) {
        return -1;
    }
    bool valid = false;
    const int id = item->data(MeshObjectIdRole).toInt(&valid);
    return valid ? id : -1;
}

int WorkbenchMainWindow::postMeshObjectId(
    const QStandardItem* item) const {
    if (item == nullptr) {
        return -1;
    }
    bool valid = false;
    const int id = item->data(PostMeshObjectIdRole).toInt(&valid);
    return valid ? id : -1;
}

int WorkbenchMainWindow::materialId(const QStandardItem* item) const {
    if (item == nullptr) {
        return -1;
    }
    bool valid = false;
    const int id = item->data(MaterialIdRole).toInt(&valid);
    return valid ? id : -1;
}

int WorkbenchMainWindow::sectionId(const QStandardItem* item) const {
    if (item == nullptr) {
        return -1;
    }
    bool valid = false;
    const int id = item->data(SolidSectionIdRole).toInt(&valid);
    return valid ? id : -1;
}

int WorkbenchMainWindow::namedSelectionId(
    const QStandardItem* item) const {
    if (item == nullptr) {
        return -1;
    }
    bool valid = false;
    const int id = item->data(NamedSelectionIdRole).toInt(&valid);
    return valid ? id : -1;
}

int WorkbenchMainWindow::constraintId(
    const QStandardItem* item) const {
    if (item == nullptr) {
        return -1;
    }
    bool valid = false;
    const int id = item->data(ConstraintIdRole).toInt(&valid);
    return valid ? id : -1;
}

QStandardItem* WorkbenchMainWindow::geometryItem(int objectId) const {
    return geometryItems_.value(objectId, nullptr);
}

QStandardItem* WorkbenchMainWindow::meshItem(int meshId) const {
    return meshItems_.value(meshId, nullptr);
}

QStandardItem* WorkbenchMainWindow::materialItem(int materialId) const {
    return materialItems_.value(materialId, nullptr);
}

QStandardItem* WorkbenchMainWindow::sectionItem(int id) const {
    return sectionItems_.value(id, nullptr);
}

QStandardItem* WorkbenchMainWindow::namedSelectionItem(int id) const {
    return namedSelectionItems_.value(id, nullptr);
}

QStandardItem* WorkbenchMainWindow::constraintItem(int id) const {
    return constraintItems_.value(id, nullptr);
}

void WorkbenchMainWindow::updateWorkspaceProperty(const QString& workspaceName) {
    Q_UNUSED(workspaceName)
    if (workspaceStack_ != nullptr &&
        workspaceStack_->currentIndex() == 1) {
        if (loadedResultGrid_ != nullptr) {
            showCurrentResultScalarProperties();
        } else if (currentPostMeshId_ >= 0) {
            showPostprocessingMeshProperties(currentPostMeshId_);
        } else {
            showDefaultProperties();
        }
        return;
    }
    if (selectedConstraintId_ >= 0) {
        showConstraintProperties(selectedConstraintId_);
    } else if (selectedNamedSelectionId_ >= 0) {
        showNamedSelectionProperties(selectedNamedSelectionId_);
    } else if (selectedSectionId_ >= 0) {
        showSectionProperties(selectedSectionId_);
    } else if (selectedMaterialId_ >= 0) {
        showMaterialProperties(selectedMaterialId_);
    } else if (selectedMeshObjectId_ >= 0) {
        showMeshProperties(selectedMeshObjectId_);
    } else if (selectedGeometryObjectId_ < 0) {
        showDefaultProperties();
    }
}

void WorkbenchMainWindow::setViewActionsEnabled(bool enabled) {
    fitAllAction_->setEnabled(enabled);
    axonometricViewAction_->setEnabled(enabled);
    frontViewAction_->setEnabled(enabled);
    backViewAction_->setEnabled(enabled);
    leftViewAction_->setEnabled(enabled);
    rightViewAction_->setEnabled(enabled);
    topViewAction_->setEnabled(enabled);
    bottomViewAction_->setEnabled(enabled);
    axonometricToolAction_->setEnabled(enabled);
    frontToolAction_->setEnabled(enabled);
    topToolAction_->setEnabled(enabled);
    rightToolAction_->setEnabled(enabled);
    generateMeshAction_->setEnabled(enabled);
    clearMeshAction_->setEnabled(enabled);
    importHmAsciiAction_->setEnabled(enabled);
    exportHmAsciiAction_->setEnabled(enabled);
}

void WorkbenchMainWindow::setSelectionActionsEnabled(bool enabled) {
    objectSelectionAction_->setEnabled(enabled);
    vertexSelectionAction_->setEnabled(enabled);
    edgeSelectionAction_->setEnabled(enabled);
    faceSelectionAction_->setEnabled(enabled);
    solidSelectionAction_->setEnabled(enabled);
}

void WorkbenchMainWindow::updateMaterialActionStates() {
    const bool hasSelection =
        materialManager_.findMaterial(selectedMaterialId_) != nullptr;
    editMaterialAction_->setEnabled(hasSelection);
    renameMaterialAction_->setEnabled(hasSelection);
    duplicateMaterialAction_->setEnabled(hasSelection);
    deleteMaterialAction_->setEnabled(hasSelection);
}

void WorkbenchMainWindow::updateSectionActionStates() {
    const bool hasMaterials = !materialManager_.materials().empty();
    const bool hasSection =
        sectionManager_.findSection(selectedSectionId_) != nullptr;
    newSectionAction_->setEnabled(hasMaterials);
    editSectionAction_->setEnabled(hasSection);
    renameSectionAction_->setEnabled(hasSection);
    duplicateSectionAction_->setEnabled(hasSection);
    deleteSectionAction_->setEnabled(hasSection);

    const MeshObject* mesh =
        occViewWidget_->findMesh(selectedMeshObjectId_);
    const GeometryObject* geometry = mesh == nullptr
        ? occViewWidget_->findGeometryObject(selectedGeometryObjectId_)
        : nullptr;
    const bool hasTarget = mesh != nullptr || geometry != nullptr;
    assignSectionAction_->setEnabled(
        hasTarget && !sectionManager_.sections().empty());
    bool hasDirectAssignment = false;
    if (mesh != nullptr) {
        hasDirectAssignment = assignmentManager_.assignedSection(
            emilcae::core::SectionAssignmentTargetType::MeshObject,
            mesh->id).has_value();
    } else if (geometry != nullptr) {
        hasDirectAssignment = assignmentManager_.assignedSection(
            emilcae::core::SectionAssignmentTargetType::GeometryObject,
            geometry->id).has_value();
    }
    unassignSectionAction_->setEnabled(hasDirectAssignment);
}

void WorkbenchMainWindow::updateAssignmentDisplays() {
    for (auto iterator = geometryItems_.cbegin();
         iterator != geometryItems_.cend(); ++iterator) {
        const GeometryObject* object =
            occViewWidget_->findGeometryObject(iterator.key());
        if (object == nullptr || iterator.value() == nullptr) {
            continue;
        }
        QString text = object->name;
        if (const auto sectionId = assignmentManager_.assignedSection(
                emilcae::core::SectionAssignmentTargetType::GeometryObject,
                object->id)) {
            if (const auto* section = sectionManager_.findSection(*sectionId)) {
                text += tr("  [截面：%1]").arg(fromUtf8(section->name));
            }
        }
        iterator.value()->setText(text);
    }
    for (auto iterator = meshItems_.cbegin();
         iterator != meshItems_.cend(); ++iterator) {
        const MeshObject* mesh = occViewWidget_->findMesh(iterator.key());
        if (mesh == nullptr || iterator.value() == nullptr) {
            continue;
        }
        QString text = mesh->importedHmAscii ? mesh->name : tr("网格");
        const auto resolved = assignmentManager_.resolvedForMesh(
            mesh->id, mesh->geometryObjectId >= 0
                          ? std::optional<int>(mesh->geometryObjectId)
                          : std::nullopt);
        if (resolved) {
            if (const auto* section =
                    sectionManager_.findSection(resolved->sectionId)) {
                text += resolved->inheritedFromGeometry
                    ? tr("  [截面：%1，继承]").arg(fromUtf8(section->name))
                    : tr("  [截面：%1]").arg(fromUtf8(section->name));
            }
        }
        iterator.value()->setText(text);
    }
}
