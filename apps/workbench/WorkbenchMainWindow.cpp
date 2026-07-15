#include "WorkbenchMainWindow.hpp"

#include "OccViewWidget.hpp"
#include "GeometryImporter.hpp"
#include "GmshMesher.hpp"
#include "HmAsciiMeshIo.hpp"
#include "MaterialEditorDialog.hpp"
#include "SectionAssignmentDialog.hpp"
#include "SolidSectionEditorDialog.hpp"

#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <BRep_Tool.hxx>
#include <TopoDS.hxx>

#include <QAction>
#include <QActionGroup>
#include <QAbstractItemView>
#include <QApplication>
#include <QDockWidget>
#include <QEventLoop>
#include <QHeaderView>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QInputDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QTableView>
#include <QToolBar>
#include <QTreeView>

#include <algorithm>
#include <cmath>

namespace {

constexpr int GeometryObjectIdRole = Qt::UserRole + 1;
constexpr int MeshObjectIdRole = Qt::UserRole + 2;
constexpr int MaterialIdRole = Qt::UserRole + 3;
constexpr int SolidSectionIdRole = Qt::UserRole + 4;

QString fromUtf8(const std::string& value) {
    return QString::fromUtf8(value.data(),
                             static_cast<qsizetype>(value.size()));
}

std::string toUtf8(const QString& value) {
    return value.toUtf8().toStdString();
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

} // namespace

WorkbenchMainWindow::WorkbenchMainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle(tr("QTCAE 仿真工作台"));
    resize(1440, 900);

    createCentralWorkspace();
    createDockWidgets();
    createActions();
    createMenus();
    createToolBar();
    createStatusBar();

    switchToPreprocessing();
}

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
        occViewWidget_->fitAll();
        statusBar()->showMessage(tr("已执行适合窗口"), 3000);
    });
    connect(axonometricViewAction_, &QAction::triggered, this, [this] {
        occViewWidget_->setAxonometricView();
        statusBar()->showMessage(tr("已切换到轴测视图"), 3000);
    });
    connect(frontViewAction_, &QAction::triggered, this, [this] {
        occViewWidget_->setFrontView();
        statusBar()->showMessage(tr("已切换到前视图"), 3000);
    });
    connect(backViewAction_, &QAction::triggered, this, [this] {
        occViewWidget_->setBackView();
        statusBar()->showMessage(tr("已切换到后视图"), 3000);
    });
    connect(leftViewAction_, &QAction::triggered, this, [this] {
        occViewWidget_->setLeftView();
        statusBar()->showMessage(tr("已切换到左视图"), 3000);
    });
    connect(rightViewAction_, &QAction::triggered, this, [this] {
        occViewWidget_->setRightView();
        statusBar()->showMessage(tr("已切换到右视图"), 3000);
    });
    connect(topViewAction_, &QAction::triggered, this, [this] {
        occViewWidget_->setTopView();
        statusBar()->showMessage(tr("已切换到顶视图"), 3000);
    });
    connect(bottomViewAction_, &QAction::triggered, this, [this] {
        occViewWidget_->setBottomView();
        statusBar()->showMessage(tr("已切换到底视图"), 3000);
    });
    connect(aboutAction_, &QAction::triggered, this, [this] {
        QMessageBox::about(
            this,
            tr("关于 QTCAE"),
            tr("QTCAE\n\n计算机辅助工程仿真工作台\n\n当前状态：界面架构预览版\nCAE 后端功能尚未接入"));
    });
    connect(aboutQtAction_, &QAction::triggered, qApp, &QApplication::aboutQt);
    updateMaterialActionStates();
    updateSectionActionStates();
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

    auto* meshMenu = menuBar()->addMenu(tr("网格"));
    meshMenu->addAction(generateMeshAction_);
    meshMenu->addAction(clearMeshAction_);
    meshMenu->addSeparator();
    meshMenu->addAction(importHmAsciiAction_);
    meshMenu->addAction(exportHmAsciiAction_);

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
    connect(openToolAction, &QAction::triggered,
            this, &WorkbenchMainWindow::openGeometryFile);
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
    modelRoot->appendRow(geometryRootItem_);
    modelRoot->appendRow(meshRootItem_);
    projectRoot->appendRow(modelRoot);
    materialRootItem_ = new QStandardItem(tr("材料"));
    projectRoot->appendRow(materialRootItem_);
    sectionRootItem_ = new QStandardItem(tr("截面"));
    projectRoot->appendRow(sectionRootItem_);
    projectRoot->appendRow(new QStandardItem(tr("分析")));
    projectRoot->appendRow(new QStandardItem(tr("结果")));
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
    auto* taskModel = new QStandardItemModel(0, 4, taskTable);
    taskModel->setHorizontalHeaderLabels(
        {tr("任务"), tr("求解器"), tr("状态"), tr("进度")});
    taskTable->setModel(taskModel);
    taskTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    taskTable->horizontalHeader()->setStretchLastSection(true);
    taskTable->verticalHeader()->setVisible(false);
    taskMonitorDock_->setWidget(taskTable);
    addDockWidget(Qt::BottomDockWidgetArea, taskMonitorDock_);

    tabifyDockWidget(messageLogDock_, taskMonitorDock_);
    messageLogDock_->raise();
}

void WorkbenchMainWindow::createCentralWorkspace() {
    workspaceStack_ = new QStackedWidget(this);

    auto createPlaceholder = [this](const QString& text) {
        auto* label = new QLabel(text, workspaceStack_);
        label->setAlignment(Qt::AlignCenter);
        return label;
    };

    occViewWidget_ = new OccViewWidget(workspaceStack_);
    workspaceStack_->addWidget(occViewWidget_);
    workspaceStack_->addWidget(
        createPlaceholder(tr("VTK 后处理视窗尚未接入")));
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
    statusBar()->showMessage(tr("已切换到前处理工作区"), 3000);
}

void WorkbenchMainWindow::switchToPostprocessing() {
    workspaceStack_->setCurrentIndex(1);
    postprocessingAction_->setChecked(true);
    updateWorkspaceProperty(tr("后处理"));
    setViewActionsEnabled(false);
    setSelectionActionsEnabled(false);
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
    const GeometryImporter::ImportResult result = importer.importFile(filePath);
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
    selectedMeshObjectId_ = -1;
    selectedGeometryObjectId_ = objectId;
    updateMaterialActionStates();
    updateSectionActionStates();
    showGeometryObjectProperties(objectId);
}

void WorkbenchMainWindow::handleProjectItemChanged(QStandardItem* item) {
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
    const int currentSectionId = sectionId(item);
    if (currentSectionId >= 0) {
        selectedGeometryObjectId_ = -1;
        selectedMeshObjectId_ = -1;
        selectedMaterialId_ = -1;
        selectedSectionId_ = currentSectionId;
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
        selectedMaterialId_ = currentMaterialId;
        selectedSectionId_ = -1;
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
    const int currentMaterialId =
        materialId(projectModel_->itemFromIndex(index));
    if (currentMaterialId >= 0) {
        selectedMaterialId_ = currentMaterialId;
        updateMaterialActionStates();
        editSelectedMaterial();
        return;
    }
    const int currentSectionId =
        sectionId(projectModel_->itemFromIndex(index));
    if (currentSectionId >= 0) {
        selectedSectionId_ = currentSectionId;
        updateSectionActionStates();
        editSelectedSection();
    }
}

void WorkbenchMainWindow::showProjectContextMenu(
    const QPoint& position) {
    const QModelIndex index = projectTree_->indexAt(position);
    QStandardItem* item = projectModel_->itemFromIndex(index);
    if (item == sectionRootItem_) {
        QMenu menu(this);
        menu.addAction(newSectionAction_);
        menu.exec(projectTree_->viewport()->mapToGlobal(position));
        return;
    }
    const int currentSectionId = sectionId(item);
    if (currentSectionId >= 0) {
        selectedSectionId_ = currentSectionId;
        selectedMaterialId_ = -1;
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
        const MeshObject* mesh = occViewWidget_->findMesh(currentMeshId);
        if (mesh == nullptr) {
            return;
        }
        QMenu menu(this);
        QAction* showAction = menu.addAction(tr("显示"));
        QAction* hideAction = menu.addAction(tr("隐藏"));
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
    if (QMessageBox::question(
            this, tr("删除几何对象"),
            tr("确定要从当前场景中删除对象“%1”吗？").arg(name),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    if (!occViewWidget_->removeGeometryObject(objectId)) {
        QMessageBox::warning(this, tr("删除失败"),
                             tr("无法从当前场景删除该对象。"));
        return;
    }

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
    updateSectionActionStates();
    messageLog_->appendPlainText(tr("已从场景删除几何对象：%1").arg(name));
    statusBar()->showMessage(tr("几何对象已删除"), 3000);
}

void WorkbenchMainWindow::clearAllGeometryObjects() {
    if (occViewWidget_->geometryObjectCount() == 0) {
        return;
    }
    if (QMessageBox::question(
            this, tr("清空几何对象"),
            tr("确定要清空当前场景中的所有几何对象吗？\n"
               "此操作不会删除磁盘上的原始文件。"),
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
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
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
    selectedMaterialId_ = -1;
    selectedSectionId_ = -1;
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

void WorkbenchMainWindow::updateWorkspaceProperty(const QString& workspaceName) {
    Q_UNUSED(workspaceName)
    if (selectedSectionId_ >= 0) {
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
