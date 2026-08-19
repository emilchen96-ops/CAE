#pragma once

#include <QPoint>
#include <QWidget>

#include "GeometryObjectManager.hpp"
#include "GeometrySelection.hpp"

#include <memory>
#include <vector>

class QPaintEngine;
class QPaintEvent;
class QFocusEvent;
class QMouseEvent;
class QResizeEvent;
class QShowEvent;
class QWheelEvent;
class TopoDS_Shape;

class OccViewWidget final : public QWidget {
    Q_OBJECT

public:
    explicit OccViewWidget(QWidget* parent = nullptr);
    ~OccViewWidget() override;

    void fitAll();
    int addGeometryObject(const QString& name,
                          const QString& filePath,
                          const TopoDS_Shape& shape);
    bool setGeometryObjectVisible(int objectId, bool visible);
    bool removeGeometryObject(int objectId);
    bool renameGeometryObject(int objectId, const QString& name);
    int replaceMesh(int geometryObjectId, const QString& name,
                    const MeshData& data, double targetSize);
    int addStandaloneMesh(const QString& name,
                          const QString& sourceFilePath,
                          const MeshData& data);
    bool setMeshVisible(int meshId, bool visible);
    bool removeMesh(int meshId);
    bool renameMesh(int meshId, const QString& name);
    void clearGeometryObjects();
    const GeometryObject* findGeometryObject(int objectId) const;
    const MeshObject* findMesh(int meshId) const;
    const MeshObject* findMeshForGeometry(int geometryObjectId) const;
    const std::vector<MeshObject>& meshes() const;
    std::size_t geometryObjectCount() const;
    std::size_t meshCount() const;
    void setSelectionMode(SelectionMode mode);
    void clearSelection();
    std::vector<GeometrySelection> currentSelections() const;
    bool selectGeometryObject(int objectId);
    bool highlightNamedSelection(
        const std::vector<TopoDS_Shape>& shapes);
    void clearNamedSelectionHighlight();
    void clearScene();
    void setFrontView();
    void setBackView();
    void setLeftView();
    void setRightView();
    void setTopView();
    void setBottomView();
    void setAxonometricView();

signals:
    void selectionChanged();

protected:
    void focusOutEvent(QFocusEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    QPaintEngine* paintEngine() const override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    enum class NavigationMode {
        None,
        Rotate,
        Pan
    };

    enum class StandardView {
        Front,
        Back,
        Left,
        Right,
        Top,
        Bottom,
        Axonometric
    };

    void applyStandardView(StandardView standardView);
    void endNavigation();
    QPoint toBackingPixels(const QPoint& logicalPosition) const;
    void initializeViewer();
    void notifySelectionChanged();

    struct Impl;
    std::unique_ptr<Impl> impl_;
    NavigationMode navigationMode_{NavigationMode::None};
    QPoint navigationStartPosition_;
    QPoint lastMousePosition_;
    SelectionMode selectionMode_{SelectionMode::Object};
    int forcedObjectSelectionId_{-1};
};
