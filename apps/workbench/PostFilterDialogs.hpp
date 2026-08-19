#pragma once

#include "VtkPostViewWidget.hpp"

#include <QDialog>
#include <QPair>
#include <QVector>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;

class PlaneFilterDialog final : public QDialog {
    Q_OBJECT
public:
    explicit PlaneFilterDialog(bool clipping, QWidget* parent = nullptr);
    PostFilterAxis axis() const;
    double position() const;
    bool keepPositive() const;

private:
    QComboBox* axisCombo_{nullptr};
    QDoubleSpinBox* positionSpin_{nullptr};
    QCheckBox* keepPositiveCheck_{nullptr};
};

class ThresholdFilterDialog final : public QDialog {
    Q_OBJECT
public:
    ThresholdFilterDialog(const QStringList& fields,
                          const QVector<QPair<double, double>>& ranges,
                          int currentIndex, QWidget* parent = nullptr);
    int fieldIndex() const;
    double minimum() const;
    double maximum() const;

protected:
    void accept() override;

private:
    void updateRange(int index);
    QComboBox* fieldCombo_{nullptr};
    QDoubleSpinBox* minimumSpin_{nullptr};
    QDoubleSpinBox* maximumSpin_{nullptr};
    QVector<QPair<double, double>> ranges_;
};
