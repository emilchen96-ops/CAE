#pragma once

#include <QMainWindow>
#include <QHash>
#include <QPair>
#include <QString>
#include <QVector>

#include <memory>
#include <vector>

#include "GeometrySelection.hpp"
#include "NamedSelectionResolver.hpp"
#include "emilcae/core/DisplacementConstraintManager.hpp"
#include "emilcae/core/MaterialManager.hpp"
#include "emilcae/core/NamedSelectionManager.hpp"
#include "emilcae/core/SolidSectionAssignmentManager.hpp"
#include "emilcae/core/SolidSectionManager.hpp"

class QAction;
class QActionGroup;
class QDockWidget;
class QStackedWidget;
class QStandardItemModel;
class QString;
class QPlainTextEdit;
class QModelIndex;
class QPoint;
class QStandardItem;
class QTreeView;
class OccViewWidget;
class VtkPostViewWidget;
enum class VtkMeshDisplayMode;

class WorkbenchMainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit WorkbenchMainWindow(QWidget* parent = nullptr);

private:
    void createActions();
    void createMenus();
    void createToolBar();
    void createDockWidgets();
    void createCentralWorkspace();
    void createStatusBar();
    void switchToPreprocessing();
    void switchToPostprocessing();
    void openGeometryFile();
    void addGeometryTreeItem(int objectId, const QString& name);
    void handleProjectItemChanged(QStandardItem* item);
    void handleProjectItemClicked(const QModelIndex& index);
    void handleProjectItemDoubleClicked(const QModelIndex& index);
    void showProjectContextMenu(const QPoint& position);
    void setGeometryObjectVisible(QStandardItem* item, bool visible);
    void renameGeometryObject(QStandardItem* item);
    void deleteGeometryObject(QStandardItem* item);
    void clearAllGeometryObjects();
    void generateTetrahedralMesh();
    void clearSelectedMesh();
    void importHmAsciiMesh();
    void exportHmAsciiMesh();
    void displayMeshInPostprocessing(int meshId = -1);
    void clearPostprocessingMeshIfMatches(int meshId);
    void showPostprocessingMeshProperties(int meshId);
    void setPostDisplayMode(VtkMeshDisplayMode mode);
    QString postDisplayModeName() const;
    void updatePostViewActionStates();
    void addOrUpdateMeshTreeItem(int meshId, int geometryObjectId);
    void addStandaloneMeshTreeItem(int meshId, const QString& name);
    void setMeshVisible(QStandardItem* item, bool visible);
    void showMeshProperties(int meshId);
    void createBlankMaterial();
    void createSteelMaterial();
    void createAluminumMaterial();
    void createMaterial(const emilcae::core::Material& initialMaterial);
    void editSelectedMaterial();
    void renameSelectedMaterial();
    void duplicateSelectedMaterial();
    void deleteSelectedMaterial();
    void addMaterialTreeItem(int materialId, const QString& name);
    void showMaterialProperties(int materialId);
    bool isMaterialNameAvailable(const QString& name,
                                 int excludedMaterialId) const;
    QString uniqueMaterialCopyName(const QString& sourceName) const;
    QString materialErrorMessage(emilcae::core::MaterialError error) const;
    void createSolidSection();
    void editSelectedSection();
    void renameSelectedSection();
    void duplicateSelectedSection();
    void deleteSelectedSection();
    void assignSectionToSelectedObject();
    void unassignSectionFromSelectedObject();
    void addSectionTreeItem(int sectionId, const QString& name);
    void showSectionProperties(int sectionId);
    bool isSectionNameAvailable(const QString& name,
                                int excludedSectionId) const;
    QString uniqueSectionCopyName(const QString& sourceName) const;
    int sectionId(const QStandardItem* item) const;
    QStandardItem* sectionItem(int sectionId) const;
    void updateSectionActionStates();
    void updateAssignmentDisplays();
    void appendAssignmentProperties(
        QVector<QPair<QString, QString>>& rows,
        emilcae::core::SectionAssignmentTargetType targetType,
        int targetId, int sourceGeometryId = -1) const;
    void createNamedSelection();
    void locateSelectedNamedSelection(bool notifyHiddenObjects = true);
    void renameSelectedNamedSelection();
    void replaceSelectedNamedSelectionItems();
    void addSelectedNamedSelectionItems();
    void removeSelectedNamedSelectionItems();
    void deleteSelectedNamedSelection();
    void addNamedSelectionTreeItem(int namedSelectionId,
                                   const QString& name);
    void showNamedSelectionProperties(int namedSelectionId);
    void updateNamedSelectionDisplays();
    bool currentNamedSelectionItems(
        std::vector<emilcae::core::NamedSelectionItem>& items,
        emilcae::core::NamedSelectionEntityType& entityType,
        QString& errorMessage) const;
    QString uniqueNamedSelectionName(
        emilcae::core::NamedSelectionEntityType entityType) const;
    QString namedSelectionErrorMessage(
        emilcae::core::NamedSelectionError error) const;
    int namedSelectionId(const QStandardItem* item) const;
    QStandardItem* namedSelectionItem(int namedSelectionId) const;
    void createFixedConstraint(int namedSelectionId = -1);
    void createDisplacementConstraint(int namedSelectionId = -1);
    void createConstraint(emilcae::core::ConstraintType type,
                          int namedSelectionId);
    void editSelectedConstraint();
    void renameSelectedConstraint();
    void duplicateSelectedConstraint();
    void locateSelectedConstraint(bool notify = true);
    void deleteSelectedConstraint();
    void addConstraintTreeItem(int constraintId, const QString& name);
    void showConstraintProperties(int constraintId);
    void updateConstraintDisplays();
    emilcae::core::NamedSelectionValiditySummary
    namedSelectionValiditySummary(int namedSelectionId) const;
    emilcae::core::ConstraintValidity constraintValidity(
        int constraintId) const;
    bool confirmUsableConstraintRegion(int namedSelectionId,
                                       bool allowPartial);
    bool isConstraintNameAvailable(const QString& name,
                                   int excludedConstraintId) const;
    QString uniqueConstraintName(emilcae::core::ConstraintType type) const;
    QString uniqueConstraintCopyName(const QString& sourceName) const;
    QString constraintErrorMessage(
        emilcae::core::ConstraintError error) const;
    int constraintId(const QStandardItem* item) const;
    QStandardItem* constraintItem(int constraintId) const;
    void switchSelectionMode(SelectionMode mode,
                             const QString& displayName);
    void handleViewSelectionChanged();
    void showSelectionProperties(const GeometrySelection& selection);
    void showGeometryObjectProperties(int objectId);
    void showDefaultProperties();
    void showFixedNodeProperties(const QString& nodeName);
    void setPropertyRows(
        const QVector<QPair<QString, QString>>& rows);
    int geometryObjectId(const QStandardItem* item) const;
    int meshObjectId(const QStandardItem* item) const;
    int postMeshObjectId(const QStandardItem* item) const;
    int materialId(const QStandardItem* item) const;
    QStandardItem* geometryItem(int objectId) const;
    QStandardItem* meshItem(int meshId) const;
    QStandardItem* materialItem(int materialId) const;
    void updateWorkspaceProperty(const QString& workspaceName);
    void setViewActionsEnabled(bool enabled);
    void setSelectionActionsEnabled(bool enabled);
    void updateMaterialActionStates();

    QAction* newProjectAction_{nullptr};
    QAction* openProjectAction_{nullptr};
    QAction* saveProjectAction_{nullptr};
    QAction* exitAction_{nullptr};
    QAction* preprocessingAction_{nullptr};
    QAction* postprocessingAction_{nullptr};
    QAction* fitAllAction_{nullptr};
    QAction* axonometricViewAction_{nullptr};
    QAction* frontViewAction_{nullptr};
    QAction* backViewAction_{nullptr};
    QAction* leftViewAction_{nullptr};
    QAction* rightViewAction_{nullptr};
    QAction* topViewAction_{nullptr};
    QAction* bottomViewAction_{nullptr};
    QAction* clearAllGeometryAction_{nullptr};
    QAction* generateMeshAction_{nullptr};
    QAction* clearMeshAction_{nullptr};
    QAction* importHmAsciiAction_{nullptr};
    QAction* exportHmAsciiAction_{nullptr};
    QAction* surfaceWithEdgesAction_{nullptr};
    QAction* surfaceOnlyAction_{nullptr};
    QAction* wireframeAction_{nullptr};
    QAction* newMaterialAction_{nullptr};
    QAction* steelMaterialAction_{nullptr};
    QAction* aluminumMaterialAction_{nullptr};
    QAction* editMaterialAction_{nullptr};
    QAction* renameMaterialAction_{nullptr};
    QAction* duplicateMaterialAction_{nullptr};
    QAction* deleteMaterialAction_{nullptr};
    QAction* newSectionAction_{nullptr};
    QAction* editSectionAction_{nullptr};
    QAction* renameSectionAction_{nullptr};
    QAction* duplicateSectionAction_{nullptr};
    QAction* deleteSectionAction_{nullptr};
    QAction* assignSectionAction_{nullptr};
    QAction* unassignSectionAction_{nullptr};
    QAction* createNamedSelectionAction_{nullptr};
    QAction* clearCurrentSelectionAction_{nullptr};
    QAction* newFixedConstraintAction_{nullptr};
    QAction* newDisplacementConstraintAction_{nullptr};
    QAction* objectSelectionAction_{nullptr};
    QAction* vertexSelectionAction_{nullptr};
    QAction* edgeSelectionAction_{nullptr};
    QAction* faceSelectionAction_{nullptr};
    QAction* solidSelectionAction_{nullptr};
    QAction* axonometricToolAction_{nullptr};
    QAction* frontToolAction_{nullptr};
    QAction* topToolAction_{nullptr};
    QAction* rightToolAction_{nullptr};
    QAction* aboutAction_{nullptr};
    QAction* aboutQtAction_{nullptr};
    QActionGroup* workspaceActionGroup_{nullptr};
    QActionGroup* selectionActionGroup_{nullptr};
    QActionGroup* postDisplayActionGroup_{nullptr};

    QDockWidget* projectDock_{nullptr};
    QDockWidget* propertiesDock_{nullptr};
    QDockWidget* messageLogDock_{nullptr};
    QDockWidget* taskMonitorDock_{nullptr};
    QStackedWidget* workspaceStack_{nullptr};
    QStandardItemModel* propertiesModel_{nullptr};
    QStandardItemModel* projectModel_{nullptr};
    QStandardItem* geometryRootItem_{nullptr};
    QStandardItem* meshRootItem_{nullptr};
    QStandardItem* materialRootItem_{nullptr};
    QStandardItem* sectionRootItem_{nullptr};
    QStandardItem* namedSelectionRootItem_{nullptr};
    QStandardItem* analysisRootItem_{nullptr};
    QStandardItem* boundaryConditionRootItem_{nullptr};
    QStandardItem* resultRootItem_{nullptr};
    QStandardItem* currentPostMeshRootItem_{nullptr};
    QStandardItem* currentPostMeshItem_{nullptr};
    QTreeView* projectTree_{nullptr};
    QPlainTextEdit* messageLog_{nullptr};
    OccViewWidget* occViewWidget_{nullptr};
    VtkPostViewWidget* vtkPostViewWidget_{nullptr};
    QHash<int, QStandardItem*> geometryItems_;
    QHash<int, QStandardItem*> meshItems_;
    QHash<int, QStandardItem*> materialItems_;
    emilcae::core::MaterialManager materialManager_;
    emilcae::core::SolidSectionManager sectionManager_{materialManager_};
    emilcae::core::SolidSectionAssignmentManager assignmentManager_{
        sectionManager_};
    QHash<int, QStandardItem*> sectionItems_;
    std::unique_ptr<emilcae::core::NamedSelectionManager>
        namedSelectionManager_;
    std::unique_ptr<NamedSelectionResolver> namedSelectionResolver_;
    QHash<int, QStandardItem*> namedSelectionItems_;
    std::unique_ptr<emilcae::core::DisplacementConstraintManager>
        constraintManager_;
    QHash<int, QStandardItem*> constraintItems_;
    QHash<int, int> constraintValidityStates_;
    int selectedGeometryObjectId_{-1};
    int selectedMeshObjectId_{-1};
    int selectedMaterialId_{-1};
    int selectedSectionId_{-1};
    int selectedNamedSelectionId_{-1};
    int selectedConstraintId_{-1};
    int currentPostMeshId_{-1};
    bool syncingTreeSelection_{false};
};
