#include "OccViewWidget.hpp"

#include "GeometryObjectManager.hpp"

#include <AIS_DisplayMode.hxx>
#include <AIS_InteractiveContext.hxx>
#include <AIS_SelectionScheme.hxx>
#include <AIS_Shape.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Aspect_GradientFillMethod.hxx>
#include <Aspect_Window.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Quantity_Color.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>

#include <QDebug>
#include <QFocusEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {

TopAbs_ShapeEnum shapeTypeForSelectionMode(SelectionMode mode) {
    switch (mode) {
    case SelectionMode::Object:
        return TopAbs_SHAPE;
    case SelectionMode::Vertex:
        return TopAbs_VERTEX;
    case SelectionMode::Edge:
        return TopAbs_EDGE;
    case SelectionMode::Face:
        return TopAbs_FACE;
    case SelectionMode::Solid:
        return TopAbs_SOLID;
    }
    return TopAbs_SHAPE;
}

Standard_Integer occSelectionMode(SelectionMode mode) {
    return AIS_Shape::SelectionMode(shapeTypeForSelectionMode(mode));
}

int localShapeIndex(const TopoDS_Shape& parentShape,
                    const TopoDS_Shape& selectedShape,
                    TopAbs_ShapeEnum shapeType) {
    if (parentShape.IsNull() || selectedShape.IsNull() ||
        shapeType == TopAbs_SHAPE) {
        return -1;
    }

    TopTools_IndexedMapOfShape shapes;
    TopExp::MapShapes(parentShape, shapeType, shapes);
    for (Standard_Integer index = 1; index <= shapes.Extent(); ++index) {
        if (shapes(index).IsSame(selectedShape)) {
            return index;
        }
    }
    return -1;
}

} // namespace

class QtOccWindow final : public Aspect_Window {
    DEFINE_STANDARD_RTTIEXT(QtOccWindow, Aspect_Window)

public:
    explicit QtOccWindow(QWidget* widget)
        : widget_(widget), previousRect_(widget->rect()) {}

    Standard_Boolean IsMapped() const override {
        return widget_ != nullptr && widget_->isVisible()
            ? Standard_True
            : Standard_False;
    }

    void Map() const override {
        if (widget_ != nullptr) {
            widget_->show();
            widget_->update();
        }
    }

    void Unmap() const override {
        if (widget_ != nullptr) {
            widget_->hide();
        }
    }

    Aspect_TypeOfResize DoResize() override {
        if (widget_ == nullptr || widget_->isMinimized()) {
            return Aspect_TOR_UNKNOWN;
        }

        const QRect currentRect = widget_->rect();
        int changedBorders = 0;
        if (qAbs(currentRect.left() - previousRect_.left()) > 2) {
            changedBorders |= 1;
        }
        if (qAbs(currentRect.right() - previousRect_.right()) > 2) {
            changedBorders |= 2;
        }
        if (qAbs(currentRect.top() - previousRect_.top()) > 2) {
            changedBorders |= 4;
        }
        if (qAbs(currentRect.bottom() - previousRect_.bottom()) > 2) {
            changedBorders |= 8;
        }
        previousRect_ = currentRect;

        switch (changedBorders) {
        case 0:
            return Aspect_TOR_NO_BORDER;
        case 1:
            return Aspect_TOR_LEFT_BORDER;
        case 2:
            return Aspect_TOR_RIGHT_BORDER;
        case 4:
            return Aspect_TOR_TOP_BORDER;
        case 5:
            return Aspect_TOR_LEFT_AND_TOP_BORDER;
        case 6:
            return Aspect_TOR_TOP_AND_RIGHT_BORDER;
        case 8:
            return Aspect_TOR_BOTTOM_BORDER;
        case 9:
            return Aspect_TOR_BOTTOM_AND_LEFT_BORDER;
        case 10:
            return Aspect_TOR_RIGHT_AND_BOTTOM_BORDER;
        default:
            return Aspect_TOR_UNKNOWN;
        }
    }

    Standard_Boolean DoMapping() const override {
        return Standard_True;
    }

    Standard_Real Ratio() const override {
        if (widget_ == nullptr || widget_->height() <= 0) {
            return 1.0;
        }
        return static_cast<Standard_Real>(widget_->width()) /
               static_cast<Standard_Real>(widget_->height());
    }

    void Position(Standard_Integer& x1, Standard_Integer& y1,
                  Standard_Integer& x2, Standard_Integer& y2) const override {
        const QRect rect = widget_ != nullptr ? widget_->rect() : QRect();
        const qreal scale = widget_ != nullptr
            ? widget_->devicePixelRatioF()
            : 1.0;
        x1 = qRound(rect.left() * scale);
        y1 = qRound(rect.top() * scale);
        x2 = qRound((rect.right() + 1) * scale) - 1;
        y2 = qRound((rect.bottom() + 1) * scale) - 1;
    }

    void Size(Standard_Integer& width,
              Standard_Integer& height) const override {
        const QSize size = widget_ != nullptr ? widget_->size() : QSize();
        const qreal scale = widget_ != nullptr
            ? widget_->devicePixelRatioF()
            : 1.0;
        width = qRound(size.width() * scale);
        height = qRound(size.height() * scale);
    }

    Aspect_Drawable NativeHandle() const override {
        return widget_ != nullptr
            ? reinterpret_cast<Aspect_Drawable>(
                  static_cast<quintptr>(widget_->winId()))
            : 0;
    }

    Aspect_Drawable NativeParentHandle() const override {
        const QWidget* parent = widget_ != nullptr
            ? widget_->parentWidget()
            : nullptr;
        return parent != nullptr
            ? reinterpret_cast<Aspect_Drawable>(
                  static_cast<quintptr>(parent->winId()))
            : 0;
    }

    Aspect_FBConfig NativeFBConfig() const override {
        return nullptr;
    }

    Standard_Real DevicePixelRatio() const override {
        return widget_ != nullptr ? widget_->devicePixelRatioF() : 1.0;
    }

private:
    QWidget* widget_{nullptr};
    QRect previousRect_;
};

IMPLEMENT_STANDARD_RTTIEXT(QtOccWindow, Aspect_Window)

struct OccViewWidget::Impl {
    Handle(Aspect_DisplayConnection) displayConnection;
    Handle(OpenGl_GraphicDriver) graphicDriver;
    Handle(V3d_Viewer) viewer;
    Handle(V3d_View) view;
    Handle(AIS_InteractiveContext) context;
    std::unique_ptr<GeometryObjectManager> objectManager;
    bool initialized{false};
    bool initializationFailed{false};
};

OccViewWidget::OccViewWidget(QWidget* parent)
    : QWidget(parent), impl_(std::make_unique<Impl>()) {
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

OccViewWidget::~OccViewWidget() {
    impl_->objectManager.reset();
    impl_->context.Nullify();
    impl_->view.Nullify();
    impl_->viewer.Nullify();
    impl_->graphicDriver.Nullify();
    impl_->displayConnection.Nullify();
}

void OccViewWidget::fitAll() {
    initializeViewer();
    if (!impl_->view.IsNull()) {
        impl_->view->FitAll(0.05, Standard_False);
        impl_->view->ZFitAll();
        impl_->view->Redraw();
    }
}

int OccViewWidget::addGeometryObject(const QString& name,
                                     const QString& filePath,
                                     const TopoDS_Shape& shape) {
    initializeViewer();
    return impl_->objectManager != nullptr
        ? impl_->objectManager->addObject(name, filePath, shape)
        : -1;
}

bool OccViewWidget::setGeometryObjectVisible(int objectId, bool visible) {
    if (!visible) {
        clearSelection();
    }
    return impl_->objectManager != nullptr &&
           impl_->objectManager->setVisible(objectId, visible);
}

bool OccViewWidget::removeGeometryObject(int objectId) {
    clearSelection();
    return impl_->objectManager != nullptr &&
           impl_->objectManager->removeObject(objectId);
}

bool OccViewWidget::renameGeometryObject(int objectId,
                                         const QString& name) {
    return impl_->objectManager != nullptr &&
           impl_->objectManager->renameObject(objectId, name);
}

int OccViewWidget::replaceMesh(int geometryObjectId, const QString& name,
                               const MeshData& data, double targetSize) {
    initializeViewer();
    clearSelection();
    return impl_->objectManager != nullptr
        ? impl_->objectManager->replaceMesh(geometryObjectId, name, data,
                                            targetSize)
        : -1;
}

int OccViewWidget::addStandaloneMesh(const QString& name,
                                     const QString& sourceFilePath,
                                     const MeshData& data) {
    initializeViewer();
    clearSelection();
    return impl_->objectManager != nullptr
        ? impl_->objectManager->addStandaloneMesh(name, sourceFilePath, data)
        : -1;
}

bool OccViewWidget::setMeshVisible(int meshId, bool visible) {
    return impl_->objectManager != nullptr &&
           impl_->objectManager->setMeshVisible(meshId, visible);
}

bool OccViewWidget::removeMesh(int meshId) {
    return impl_->objectManager != nullptr &&
           impl_->objectManager->removeMesh(meshId);
}

bool OccViewWidget::renameMesh(int meshId, const QString& name) {
    return impl_->objectManager != nullptr &&
           impl_->objectManager->renameMesh(meshId, name);
}

void OccViewWidget::clearGeometryObjects() {
    clearSelection();
    if (impl_->objectManager != nullptr) {
        impl_->objectManager->clearGeometryObjects();
    }
}

const GeometryObject*
OccViewWidget::findGeometryObject(int objectId) const {
    return impl_->objectManager != nullptr
        ? impl_->objectManager->findObject(objectId)
        : nullptr;
}

const MeshObject* OccViewWidget::findMesh(int meshId) const {
    return impl_->objectManager != nullptr
        ? impl_->objectManager->findMesh(meshId)
        : nullptr;
}

const MeshObject* OccViewWidget::findMeshForGeometry(
    int geometryObjectId) const {
    return impl_->objectManager != nullptr
        ? impl_->objectManager->findMeshForGeometry(geometryObjectId)
        : nullptr;
}

std::size_t OccViewWidget::geometryObjectCount() const {
    return impl_->objectManager != nullptr
        ? impl_->objectManager->objectCount()
        : 0;
}

std::size_t OccViewWidget::meshCount() const {
    return impl_->objectManager != nullptr
        ? impl_->objectManager->meshCount()
        : 0;
}

void OccViewWidget::setSelectionMode(SelectionMode mode) {
    initializeViewer();
    if (impl_->objectManager == nullptr || impl_->context.IsNull()) {
        return;
    }
    if (!impl_->objectManager->setSelectionMode(occSelectionMode(mode))) {
        return;
    }
    selectionMode_ = mode;
    clearSelection();
}

void OccViewWidget::clearSelection() {
    forcedObjectSelectionId_ = -1;
    if (!impl_->context.IsNull()) {
        impl_->context->ClearDetected(Standard_False);
        impl_->context->ClearSelected(Standard_False);
        if (!impl_->view.IsNull()) {
            impl_->view->Redraw();
        }
    }
    notifySelectionChanged();
}

std::vector<GeometrySelection> OccViewWidget::currentSelections() const {
    std::vector<GeometrySelection> selections;
    if (impl_->context.IsNull() || impl_->objectManager == nullptr) {
        return selections;
    }

    for (impl_->context->InitSelected(); impl_->context->MoreSelected();
         impl_->context->NextSelected()) {
        const Handle(AIS_InteractiveObject) presentation =
            impl_->context->SelectedInteractive();
        const GeometryObject* object =
            impl_->objectManager->findObjectByPresentation(presentation);
        if (object == nullptr || !object->visible ||
            !impl_->context->HasSelectedShape()) {
            continue;
        }

        const TopoDS_Shape selectedShape =
            impl_->context->SelectedShape();
        if (selectedShape.IsNull()) {
            continue;
        }
        const bool forcedObject = object->id == forcedObjectSelectionId_;
        const SelectionMode resultMode = forcedObject
            ? SelectionMode::Object
            : selectionMode_;
        selections.push_back({
            object->id,
            resultMode,
            selectedShape,
            forcedObject
                ? -1
                : localShapeIndex(object->shape, selectedShape,
                                  shapeTypeForSelectionMode(resultMode))
        });
    }
    return selections;
}

bool OccViewWidget::selectGeometryObject(int objectId) {
    initializeViewer();
    if (impl_->context.IsNull() || impl_->objectManager == nullptr) {
        return false;
    }
    const GeometryObject* object =
        impl_->objectManager->findObject(objectId);
    if (object == nullptr || !object->visible ||
        object->presentation.IsNull()) {
        return false;
    }

    try {
        impl_->context->ClearDetected(Standard_False);
        impl_->context->SetSelected(object->presentation, Standard_True);
        forcedObjectSelectionId_ = objectId;
        notifySelectionChanged();
        return true;
    } catch (const Standard_Failure& failure) {
        qWarning() << "OpenCASCADE tree object selection failed:"
                   << failure.GetMessageString();
        return false;
    }
}

void OccViewWidget::clearScene() {
    clearGeometryObjects();
}

void OccViewWidget::setFrontView() {
    applyStandardView(StandardView::Front);
}

void OccViewWidget::setBackView() {
    applyStandardView(StandardView::Back);
}

void OccViewWidget::setLeftView() {
    applyStandardView(StandardView::Left);
}

void OccViewWidget::setRightView() {
    applyStandardView(StandardView::Right);
}

void OccViewWidget::setTopView() {
    applyStandardView(StandardView::Top);
}

void OccViewWidget::setBottomView() {
    applyStandardView(StandardView::Bottom);
}

void OccViewWidget::setAxonometricView() {
    applyStandardView(StandardView::Axonometric);
}

void OccViewWidget::focusOutEvent(QFocusEvent* event) {
    endNavigation();
    if (!impl_->context.IsNull()) {
        impl_->context->ClearDetected(Standard_True);
    }
    QWidget::focusOutEvent(event);
}

void OccViewWidget::leaveEvent(QEvent* event) {
    endNavigation();
    if (!impl_->context.IsNull()) {
        impl_->context->ClearDetected(Standard_True);
    }
    QWidget::leaveEvent(event);
}

void OccViewWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        endNavigation();
        fitAll();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void OccViewWidget::mouseMoveEvent(QMouseEvent* event) {
    if (navigationMode_ == NavigationMode::None) {
        initializeViewer();
        if (!impl_->context.IsNull() && !impl_->view.IsNull() &&
            event->buttons() == Qt::NoButton) {
            const QPoint position =
                toBackingPixels(event->position().toPoint());
            try {
                impl_->context->MoveTo(position.x(), position.y(),
                                       impl_->view, Standard_True);
            } catch (const Standard_Failure& failure) {
                qWarning() << "OpenCASCADE detection failed:"
                           << failure.GetMessageString();
                impl_->context->ClearDetected(Standard_True);
            }
            event->accept();
            return;
        }
        QWidget::mouseMoveEvent(event);
        return;
    }
    if (impl_->view.IsNull()) {
        endNavigation();
        QWidget::mouseMoveEvent(event);
        return;
    }

    const QPoint currentPosition =
        toBackingPixels(event->position().toPoint());
    if (!(event->buttons() & Qt::MiddleButton)) {
        endNavigation();
        event->ignore();
        return;
    }

    try {
        if (navigationMode_ == NavigationMode::Rotate) {
            impl_->view->Rotation(currentPosition.x(), currentPosition.y());
        } else if (navigationMode_ == NavigationMode::Pan) {
            const QPoint offset = currentPosition - navigationStartPosition_;
            impl_->view->Pan(offset.x(), -offset.y(), 1.0, Standard_False);
        }
        impl_->view->Redraw();
    } catch (const Standard_Failure& failure) {
        qWarning() << "OpenCASCADE navigation failed:"
                   << failure.GetMessageString();
        endNavigation();
    }

    lastMousePosition_ = currentPosition;
    event->accept();
}

void OccViewWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        initializeViewer();
        if (impl_->context.IsNull() || impl_->view.IsNull()) {
            event->ignore();
            return;
        }
        const QPoint position =
            toBackingPixels(event->position().toPoint());
        try {
            const AIS_StatusOfDetection detectionStatus =
                impl_->context->MoveTo(position.x(), position.y(),
                                       impl_->view, Standard_False);
            forcedObjectSelectionId_ = -1;
            if (detectionStatus == AIS_SOD_Selected ||
                detectionStatus == AIS_SOD_OnlyOneDetected ||
                detectionStatus == AIS_SOD_OnlyOneGood ||
                detectionStatus == AIS_SOD_SeveralGood) {
                const AIS_SelectionScheme scheme =
                    event->modifiers().testFlag(Qt::ControlModifier)
                    ? AIS_SelectionScheme_XOR
                    : AIS_SelectionScheme_Replace;
                impl_->context->SelectDetected(scheme);
            } else {
                impl_->context->ClearSelected(Standard_False);
            }
            impl_->view->Redraw();
            notifySelectionChanged();
            event->accept();
        } catch (const Standard_Failure& failure) {
            qWarning() << "OpenCASCADE selection failed:"
                       << failure.GetMessageString();
            event->ignore();
        }
        return;
    }
    if (event->button() != Qt::MiddleButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    initializeViewer();
    if (impl_->view.IsNull()) {
        event->ignore();
        return;
    }

    setFocus(Qt::MouseFocusReason);
    navigationStartPosition_ =
        toBackingPixels(event->position().toPoint());
    lastMousePosition_ = navigationStartPosition_;

    if (event->modifiers().testFlag(Qt::ShiftModifier)) {
        navigationMode_ = NavigationMode::Pan;
        impl_->view->Pan(0, 0, 1.0, Standard_True);
    } else {
        navigationMode_ = NavigationMode::Rotate;
        impl_->view->StartRotation(navigationStartPosition_.x(),
                                   navigationStartPosition_.y());
    }
    event->accept();
}

void OccViewWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        endNavigation();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

QPaintEngine* OccViewWidget::paintEngine() const {
    return nullptr;
}

void OccViewWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event)
    initializeViewer();
    if (!impl_->view.IsNull()) {
        impl_->view->Redraw();
    }
}

void OccViewWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (!impl_->view.IsNull()) {
        impl_->view->MustBeResized();
        impl_->view->Redraw();
    }
}

void OccViewWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    initializeViewer();
    if (!impl_->view.IsNull()) {
        impl_->view->MustBeResized();
        impl_->view->Redraw();
    }
}

void OccViewWidget::wheelEvent(QWheelEvent* event) {
    initializeViewer();
    if (impl_->view.IsNull() || event->angleDelta().y() == 0) {
        event->ignore();
        return;
    }

    const double wheelSteps = std::clamp(
        static_cast<double>(event->angleDelta().y()) / 120.0, -4.0, 4.0);
    const double zoomFactor = std::pow(1.12, wheelSteps);

    try {
        impl_->view->SetZoom(zoomFactor, Standard_True);
        impl_->view->Redraw();
        event->accept();
    } catch (const Standard_Failure& failure) {
        qWarning() << "OpenCASCADE zoom failed:"
                   << failure.GetMessageString();
        event->ignore();
    }
}

void OccViewWidget::applyStandardView(StandardView standardView) {
    initializeViewer();
    if (impl_->view.IsNull()) {
        return;
    }

    V3d_TypeOfOrientation orientation =
        V3d_TypeOfOrientation_Zup_AxoRight;
    switch (standardView) {
    case StandardView::Front:
        orientation = V3d_TypeOfOrientation_Zup_Front;
        break;
    case StandardView::Back:
        orientation = V3d_TypeOfOrientation_Zup_Back;
        break;
    case StandardView::Left:
        orientation = V3d_TypeOfOrientation_Zup_Left;
        break;
    case StandardView::Right:
        orientation = V3d_TypeOfOrientation_Zup_Right;
        break;
    case StandardView::Top:
        orientation = V3d_TypeOfOrientation_Zup_Top;
        break;
    case StandardView::Bottom:
        orientation = V3d_TypeOfOrientation_Zup_Bottom;
        break;
    case StandardView::Axonometric:
        orientation = V3d_TypeOfOrientation_Zup_AxoRight;
        break;
    }

    impl_->view->SetProj(orientation, Standard_False);
    fitAll();
}

void OccViewWidget::endNavigation() {
    navigationMode_ = NavigationMode::None;
}

QPoint OccViewWidget::toBackingPixels(const QPoint& logicalPosition) const {
    const qreal scale = devicePixelRatioF();
    return {qRound(logicalPosition.x() * scale),
            qRound(logicalPosition.y() * scale)};
}

void OccViewWidget::initializeViewer() {
    if (impl_->initialized || impl_->initializationFailed) {
        return;
    }

    try {
        impl_->displayConnection = new Aspect_DisplayConnection();
        impl_->graphicDriver =
            new OpenGl_GraphicDriver(impl_->displayConnection);
        impl_->viewer = new V3d_Viewer(impl_->graphicDriver);
        impl_->viewer->SetDefaultLights();
        impl_->viewer->SetLightOn();

        impl_->view = impl_->viewer->CreateView();
        impl_->context = new AIS_InteractiveContext(impl_->viewer);
        impl_->objectManager = std::make_unique<GeometryObjectManager>(
            impl_->context, impl_->view);
        impl_->objectManager->setSelectionMode(
            occSelectionMode(selectionMode_));

        Handle(Aspect_Window) window = new QtOccWindow(this);
        impl_->view->SetWindow(window);
        if (!window->IsMapped()) {
            window->Map();
        }

        const Quantity_Color upperColor(0.08, 0.11, 0.16,
                                        Quantity_TOC_RGB);
        const Quantity_Color lowerColor(0.20, 0.24, 0.30,
                                        Quantity_TOC_RGB);
        impl_->view->SetBgGradientColors(upperColor, lowerColor,
                                         Aspect_GFM_VER, Standard_False);
        impl_->view->SetProj(V3d_TypeOfOrientation_Zup_AxoRight,
                             Standard_False);
        impl_->view->MustBeResized();

        impl_->initialized = true;
    } catch (const Standard_Failure& failure) {
        impl_->initializationFailed = true;
        qCritical() << "OpenCASCADE viewer initialization failed:"
                    << failure.GetMessageString();
    }
}

void OccViewWidget::notifySelectionChanged() {
    emit selectionChanged();
}
