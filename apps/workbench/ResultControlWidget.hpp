#pragma once

#include "ResultField.hpp"

#include <QWidget>

#include <optional>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;

class ResultControlWidget final : public QWidget {
    Q_OBJECT

public:
    explicit ResultControlWidget(QWidget* parent = nullptr);

    void setResultFields(const std::vector<ResultFieldInfo>& fields);
    void clearResult();
    void setSequenceFrames(const std::vector<int>& frameNumbers,
                           int currentFrame);
    void clearSequence();

    std::optional<ResultScalarOption> selectedScalarOption() const;
    std::string selectedDisplacementField() const;
    bool scalarVisible() const;
    bool deformationVisible() const;
    bool legendVisible() const;
    double deformationScale() const;

    bool selectScalarOption(const ResultScalarOption& option);
    bool selectDisplacementField(const std::string& name);
    void setScalarVisible(bool visible);
    void setDeformationChecked(bool checked);
    void setDeformationScaleValue(double scale);
    void setLegendChecked(bool checked);
    bool selectFrame(int frameNumber);

signals:
    void frameChangeRequested(int frameNumber);
    void scalarSelectionChanged();
    void scalarVisibilityChanged(bool visible);
    void displacementFieldChanged();
    void deformationVisibilityChanged(bool visible);
    void deformationScaleChanged(double scale);
    void legendVisibilityChanged(bool visible);

private:
    void updateComponents();

    QComboBox* fieldCombo_{nullptr};
    QComboBox* frameCombo_{nullptr};
    QPushButton* previousFrameButton_{nullptr};
    QPushButton* nextFrameButton_{nullptr};
    QComboBox* componentCombo_{nullptr};
    QCheckBox* scalarCheck_{nullptr};
    QComboBox* displacementCombo_{nullptr};
    QCheckBox* deformationCheck_{nullptr};
    QDoubleSpinBox* deformationScaleSpin_{nullptr};
    QCheckBox* legendCheck_{nullptr};
    std::vector<ResultFieldInfo> fields_;
    std::vector<std::vector<ResultScalarOption>> scalarOptions_;
};
