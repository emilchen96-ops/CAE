#include "HistoryCurveWidget.hpp"

#include <QPainter>
#include <QPaintEvent>

#include <algorithm>
#include <cmath>
#include <limits>

HistoryCurveWidget::HistoryCurveWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(420, 220);
}

void HistoryCurveWidget::setCurve(const HistoryCurve& curve) {
    curve_ = curve; update();
}

void HistoryCurveWidget::clearCurve() { curve_ = {}; update(); }

void HistoryCurveWidget::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);
    QPainter painter(this);
    painter.fillRect(rect(), QColor(250, 250, 250));
    const QRectF plot = rect().adjusted(64, 32, -24, -48);
    painter.setPen(QColor(70, 70, 70));
    painter.drawRect(plot);
    if (curve_.points.empty()) {
        painter.drawText(rect(), Qt::AlignCenter, tr("暂无时间历程曲线"));
        return;
    }
    double xMin = curve_.points.front().time, xMax = xMin;
    double yMin = curve_.points.front().value, yMax = yMin;
    for (const auto& point : curve_.points) {
        xMin = std::min(xMin, point.time); xMax = std::max(xMax, point.time);
        yMin = std::min(yMin, point.value); yMax = std::max(yMax, point.value);
    }
    if (xMin == xMax) xMax = xMin + 1.0;
    if (yMin == yMax) { const double d = std::max(std::abs(yMin) * 0.05, 1.0); yMin -= d; yMax += d; }
    painter.setPen(QColor(215, 215, 215));
    for (int i = 1; i < 5; ++i) {
        const double t = i / 5.0;
        painter.drawLine(QPointF(plot.left() + t * plot.width(), plot.top()),
                         QPointF(plot.left() + t * plot.width(), plot.bottom()));
        painter.drawLine(QPointF(plot.left(), plot.top() + t * plot.height()),
                         QPointF(plot.right(), plot.top() + t * plot.height()));
    }
    QPolygonF polyline;
    for (const auto& point : curve_.points) {
        const double x = plot.left() + (point.time - xMin) / (xMax - xMin) * plot.width();
        const double y = plot.bottom() - (point.value - yMin) / (yMax - yMin) * plot.height();
        polyline << QPointF(x, y);
    }
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(22, 105, 190), 2.0));
    painter.drawPolyline(polyline);
    painter.setPen(QColor(30, 30, 30));
    painter.drawText(QRectF(0, 4, width(), 24), Qt::AlignCenter,
                     QString::fromUtf8(curve_.name));
    painter.drawText(QRectF(plot.left(), plot.bottom() + 10, plot.width(), 24),
                     Qt::AlignCenter, tr("帧编号（结果未提供物理时间）"));
    painter.drawText(4, static_cast<int>(plot.top()) + 12,
                     QString::number(yMax, 'g', 6));
    painter.drawText(4, static_cast<int>(plot.bottom()),
                     QString::number(yMin, 'g', 6));
}
