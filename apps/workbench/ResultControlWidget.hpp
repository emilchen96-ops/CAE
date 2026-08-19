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
class QLabel;

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
    ResultScalarRangeMode scalarRangeMode() const;
    double manualScalarMinimum() const;
    double manualScalarMaximum() const;

    bool selectScalarOption(const ResultScalarOption& option);
    bool selectDisplacementField(const std::string& name);
    void setScalarVisible(bool visible);
    void setDeformationChecked(bool checked);
    void setDeformationScaleValue(double scale);
    void setLegendChecked(bool checked);
    void setAutomaticScalarRange(double minimum, double maximum);
    void setScalarRangeMode(ResultScalarRangeMode mode);
    void setManualScalarRange(double minimum, double maximum);
    bool selectFrame(int frameNumber);
    int currentFrame() const;
    int firstFrame() const;
    int nextFrame(bool loop) const;
    double playbackSpeed() const;
    bool loopPlayback() const;
    void setPlaybackActive(bool active);
    void updateCurrentTime(int frameNumber);

signals:
    void frameChangeRequested(int frameNumber);
    void scalarSelectionChanged();
    void scalarVisibilityChanged(bool visible);
    void displacementFieldChanged();
    void deformationVisibilityChanged(bool visible);
    void deformationScaleChanged(double scale);
    void legendVisibilityChanged(bool visible);
    void scalarRangeChangeRequested();
    void playRequested();
    void pauseRequested();
    void stopRequested();

private:
    void updateComponents();
    void updateScalarRangeControls();

    QComboBox* fieldCombo_{nullptr};
    QComboBox* frameCombo_{nullptr};
    QPushButton* previousFrameButton_{nullptr};
    QPushButton* nextFrameButton_{nullptr};
    QPushButton* playButton_{nullptr};
    QPushButton* pauseButton_{nullptr};
    QPushButton* stopButton_{nullptr};
    QComboBox* playbackSpeedCombo_{nullptr};
    QCheckBox* loopPlaybackCheck_{nullptr};
    QLabel* currentTimeLabel_{nullptr};
    bool playbackActive_{false};
    QComboBox* componentCombo_{nullptr};
    QCheckBox* scalarCheck_{nullptr};
    QComboBox* displacementCombo_{nullptr};
    QCheckBox* deformationCheck_{nullptr};
    QDoubleSpinBox* deformationScaleSpin_{nullptr};
    QCheckBox* legendCheck_{nullptr};
    QComboBox* scalarRangeModeCombo_{nullptr};
    QDoubleSpinBox* scalarMinimumSpin_{nullptr};
    QDoubleSpinBox* scalarMaximumSpin_{nullptr};
    QPushButton* applyScalarRangeButton_{nullptr};
    bool automaticScalarRangeAvailable_{false};
    bool manualScalarRangeInitialized_{false};
    double automaticScalarMinimum_{0.0};
    double automaticScalarMaximum_{1.0};
    std::vector<ResultFieldInfo> fields_;
    std::vector<std::vector<ResultScalarOption>> scalarOptions_;
};
