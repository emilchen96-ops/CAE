#pragma once

#include "ResultHistory.hpp"

#include <QWidget>

class HistoryCurveWidget final : public QWidget {
    Q_OBJECT
public:
    explicit HistoryCurveWidget(QWidget* parent = nullptr);
    void setCurve(const HistoryCurve& curve);
    void clearCurve();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    HistoryCurve curve_;
};
