#pragma once

#include "SphJobDefinition.hpp"

#include <QDialog>

#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QTableWidget;

class SphMultiBodyAnalysisDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SphMultiBodyAnalysisDialog(
        std::vector<SphBodyJobInput> availableBodies,
        int initiallySelectedMeshId,
        QWidget* parent = nullptr);

    SphMultiBodyJobInput jobInput() const;

protected:
    void accept() override;

private:
    void browseJobDirectory();
    void browseSolverExecutable();
    void changeCurrentBody(int row);
    void storeCurrentBody();
    void loadCurrentBody();
    void editCurrentMaterial();
    std::vector<SphRigidPlaneDefinition> rigidPlanes() const;
    static std::vector<SphNodeSetConstraintDefinition>
    defaultNodeSetConstraints();

    std::vector<SphBodyJobInput> bodies_;
    std::vector<bool> bodyEnabled_;
    int currentBodyRow_{-1};

    QLineEdit* jobNameEdit_{nullptr};
    QLineEdit* jobDirectoryEdit_{nullptr};
    QLineEdit* solverExecutableEdit_{nullptr};
    QDoubleSpinBox* simulationTimeSpin_{nullptr};
    QDoubleSpinBox* timeStepRatioSpin_{nullptr};
    QSpinBox* outputIntervalSpin_{nullptr};
    QSpinBox* maximumStepsSpin_{nullptr};
    QSpinBox* threadCountSpin_{nullptr};
    QDoubleSpinBox* searchRangeSpin_{nullptr};
    QDoubleSpinBox* searchExtensionSpin_{nullptr};
    QDoubleSpinBox* gammaSpin_{nullptr};
    QDoubleSpinBox* massFactorSpin_{nullptr};
    QSpinBox* criticalNeighborCountSpin_{nullptr};
    QDoubleSpinBox* criticalStrainSpin_{nullptr};
    QCheckBox* lockingFreeCheck_{nullptr};
    QCheckBox* adaptiveTimeStepCheck_{nullptr};
    QCheckBox* updateNeighborhoodCheck_{nullptr};
    QComboBox* contactTypeCombo_{nullptr};
    QTableWidget* rigidPlaneTable_{nullptr};

    QListWidget* bodyList_{nullptr};
    QLineEdit* bodyNameEdit_{nullptr};
    QLineEdit* materialSummaryEdit_{nullptr};
    QDoubleSpinBox* translationXSpin_{nullptr};
    QDoubleSpinBox* translationYSpin_{nullptr};
    QDoubleSpinBox* translationZSpin_{nullptr};
    QDoubleSpinBox* velocityXSpin_{nullptr};
    QDoubleSpinBox* velocityYSpin_{nullptr};
    QDoubleSpinBox* velocityZSpin_{nullptr};
    QDoubleSpinBox* energyReleaseRateSpin_{nullptr};
    QDoubleSpinBox* artificialAlphaSpin_{nullptr};
    QDoubleSpinBox* artificialBetaSpin_{nullptr};
    QDoubleSpinBox* hourglassSpin_{nullptr};
    QCheckBox* preserveNodeSetsCheck_{nullptr};
    QCheckBox* nodalContactCheck_{nullptr};
    QDoubleSpinBox* contactSearchDistanceSpin_{nullptr};
    QDoubleSpinBox* contactScaleFactorSpin_{nullptr};
    QDoubleSpinBox* contactPenaltySpin_{nullptr};
};

